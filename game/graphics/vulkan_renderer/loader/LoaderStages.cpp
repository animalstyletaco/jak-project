#include "LoaderStages.h"

#include "Loader.h"

constexpr float LOAD_BUDGET = 2.5f;

/*!
 * Upload a texture to the GPU, and give it to the pool.
 */
void vk_loader_stage::update_texture(TexturePool& pool,
                                     const tfrag3::Texture& tex,
                                     TextureInfo& texture_info,
                                     bool is_common) {
  VkExtent3D extents{tex.w, tex.h, 1};
  texture_info.CreateImage(extents, 1, VK_IMAGE_TYPE_2D, texture_info.getMsaaCount(),
                           VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_LINEAR,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  texture_info.map();
  texture_info.writeToBuffer((u32*)tex.data.data());
  texture_info.unmap();

  //TODO: Get Mipmap Level here
  unsigned mipLevels = 1;

  texture_info.CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_A8B8G8R8_SINT_PACK32,
                               VK_IMAGE_ASPECT_COLOR_BIT, 1);
  // Max Anisotropy is set in vulkan renderer sampler info;

  if (tex.load_to_pool) {
    TextureInput in;
    in.debug_page_name = tex.debug_tpage_name;
    in.debug_name = tex.debug_name;
    in.w = tex.w;
    in.h = tex.h;
    in.gpu_texture = &texture_info;
    in.common = is_common;
    in.id = PcTextureId::from_combo_id(tex.combo_id);
    in.src_data = (const u8*)tex.data.data();
    pool.give_texture(in);
  }
}

class TextureLoaderStage : public LoaderStage {
 public:
  TextureLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStage(device, "texture") {}
  bool run(Timer& timer, LoaderInput& data) override {
    constexpr int MAX_TEX_BYTES_PER_FRAME = 1024 * 512;

    int bytes_this_run = 0;
    int tex_this_run = 0;
    if (data.lev_data->textures.size() < data.lev_data->level->textures.size()) {
      std::unique_lock<std::mutex> tpool_lock(data.tex_pool->mutex());
      while (data.lev_data->textures.size() < data.lev_data->level->textures.size()) {
        auto& level_texture = data.lev_data->level->textures[data.lev_data->textures.size()];
        data.lev_data->textures.emplace_back(TextureInfo{m_device});
        TextureInfo& texture_to_be_loaded = data.lev_data->textures.back();

        vk_loader_stage::update_texture(*data.tex_pool, level_texture, texture_to_be_loaded, false);
        bytes_this_run += level_texture.w * level_texture.h * 4;
        tex_this_run++;
        if (tex_this_run > 20) {
          break;
        }
        if (bytes_this_run > MAX_TEX_BYTES_PER_FRAME || timer.getMs() > LOAD_BUDGET) {
          break;
        }
      }
    }
    return data.lev_data->textures.size() == data.lev_data->level->textures.size();
  }
  void reset() override {}
};

class TfragLoadStage : public LoaderStage {
 public:
  TfragLoadStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStage(device, "tfrag") {}
  bool run(Timer& timer, LoaderInput& data) override {
    if (m_done) {
      return true;
    }

    if (data.lev_data->level->tfrag_trees.front().empty()) {
      m_done = true;
      return true;
    }

    if (!m_vulkan_created) {
      for (int geo = 0; geo < tfrag3::TFRAG_GEOS; geo++) {
        auto& in_trees = data.lev_data->level->tfrag_trees[geo];
        for (auto& in_tree : in_trees) {
          std::unique_ptr<VertexBuffer>& tree_vertex_buffer = data.lev_data->tfrag_vertex_data[geo].emplace_back();

          tree_vertex_buffer = std::make_unique<VertexBuffer>(
              m_device, sizeof(tfrag3::PreloadedVertex), in_tree.unpacked.vertices.size(),
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);
        }
      }
      m_vulkan_created = true;
      return false;
    }

    constexpr u32 CHUNK_SIZE = 32768;
    u32 uploaded_bytes = 0;
    u32 unique_buffers = 0;

    while (true) {
      bool complete_tree;

      if (data.lev_data->level->tfrag_trees[m_next_geo].empty()) {
        complete_tree = true;
      } else {
        const auto& tree = data.lev_data->level->tfrag_trees[m_next_geo][m_next_tree];
        u32 end_vert_in_tree = tree.unpacked.vertices.size();
        // the number of vertices we'd need to finish the tree right now
        size_t num_verts_left_in_tree = end_vert_in_tree - m_next_vert;
        size_t start_vert_for_chunk;
        size_t end_vert_for_chunk;

        if (num_verts_left_in_tree > CHUNK_SIZE) {
          complete_tree = false;
          // should only do partial
          start_vert_for_chunk = m_next_vert;
          end_vert_for_chunk = start_vert_for_chunk + CHUNK_SIZE;
          m_next_vert += CHUNK_SIZE;
        } else {
          // should do all!
          start_vert_for_chunk = m_next_vert;
          end_vert_for_chunk = end_vert_in_tree;
          complete_tree = true;
        }

        auto& tree_vertex_buffer = data.lev_data->tfrag_vertex_data[m_next_geo][m_next_tree];
        u32 upload_size =
            (end_vert_for_chunk - start_vert_for_chunk) * sizeof(tfrag3::PreloadedVertex);

        tree_vertex_buffer->map(upload_size, start_vert_for_chunk * sizeof(tfrag3::PreloadedVertex));
        tree_vertex_buffer->writeToBuffer((tfrag3::PreloadedVertex*)tree.unpacked.vertices.data() + start_vert_for_chunk,
                                          upload_size, 0);
        tree_vertex_buffer->unmap();
        uploaded_bytes += upload_size;
      }

      if (complete_tree) {
        unique_buffers++;
        // and move on to next tree
        m_next_vert = 0;
        m_next_tree++;
        if (m_next_tree >= data.lev_data->level->tfrag_trees[m_next_geo].size()) {
          m_next_tree = 0;
          m_next_geo++;
          if (m_next_geo >= tfrag3::TFRAG_GEOS) {
            m_next_tree = true;
            m_next_tree = 0;
            m_next_geo = 0;
            m_next_vert = 0;
            m_done = true;
            return true;
          }
        }

        return false;
      }

      if (timer.getMs() > LOAD_BUDGET || (uploaded_bytes / 1024) > 2048) {
        return false;
      }
    }
  }

  void reset() override {
    m_done = false;
    m_vulkan_created = false;
    m_next_geo = 0;
    m_next_tree = 0;
    m_next_vert = 0;
  }

 private:
  bool m_done = false;
  bool m_vulkan_created = false;
  u32 m_next_geo = 0;
  u32 m_next_tree = 0;
  u32 m_next_vert = 0;
};

class ShrubLoadStage : public LoaderStage {
 public:
  ShrubLoadStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStage(device, "shrub") {}
  bool run(Timer& timer, LoaderInput& data) override {
    if (m_done) {
      return true;
    }

    if (data.lev_data->level->shrub_trees.empty()) {
      m_done = true;
      return true;
    }

    if (!m_vulkan_created) {
      for (auto& in_tree : data.lev_data->level->shrub_trees) {
        std::unique_ptr<VertexBuffer>& tree_out = data.lev_data->shrub_vertex_data.emplace_back();
        tree_out = std::make_unique<VertexBuffer>(
            m_device, sizeof(tfrag3::ShrubGpuVertex), in_tree.unpacked.vertices.size(),
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);
      }
      m_vulkan_created = true;
      return false;
    }

    constexpr u32 CHUNK_SIZE = 32768;
    u32 uploaded_bytes = 0;

    while (true) {
      const auto& tree = data.lev_data->level->shrub_trees[m_next_tree];
      u32 end_vert_in_tree = tree.unpacked.vertices.size();
      // the number of vertices we'd need to finish the tree right now
      size_t num_verts_left_in_tree = end_vert_in_tree - m_next_vert;
      size_t start_vert_for_chunk;
      size_t end_vert_for_chunk;

      bool complete_tree;

      if (num_verts_left_in_tree > CHUNK_SIZE) {
        complete_tree = false;
        // should only do partial
        start_vert_for_chunk = m_next_vert;
        end_vert_for_chunk = start_vert_for_chunk + CHUNK_SIZE;
        m_next_vert += CHUNK_SIZE;
      } else {
        // should do all!
        start_vert_for_chunk = m_next_vert;
        end_vert_for_chunk = end_vert_in_tree;
        complete_tree = true;
      }

      u32 upload_size =
          (end_vert_for_chunk - start_vert_for_chunk) * sizeof(tfrag3::ShrubGpuVertex);
      data.lev_data->shrub_vertex_data[m_next_tree]->map(upload_size, start_vert_for_chunk *
                                                         sizeof(tfrag3::ShrubGpuVertex));
      data.lev_data->shrub_vertex_data[m_next_tree]->writeToBuffer((tfrag3::ShrubGpuVertex*)tree.unpacked.vertices.data() +
                                                                   start_vert_for_chunk);
      data.lev_data->shrub_vertex_data[m_next_tree]->unmap();
      uploaded_bytes += upload_size;

      if (complete_tree) {
        // and move on to next tree
        m_next_vert = 0;
        m_next_tree++;
        if (m_next_tree >= data.lev_data->level->shrub_trees.size()) {
          m_done = true;
          return true;
        }
      }

      if (timer.getMs() > LOAD_BUDGET || (uploaded_bytes / 128) > 2048) {
        return false;
      }
    }
  }

  void reset() override {
    m_done = false;
    m_vulkan_created = false;
    m_next_tree = 0;
    m_next_vert = 0;
  }

 private:
  bool m_done = false;
  bool m_vulkan_created = false;
  u32 m_next_tree = 0;
  u32 m_next_vert = 0;
};

class TieLoadStage : public LoaderStage {
 public:
  TieLoadStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStage(device, "tie") {}
  bool run(Timer& timer, LoaderInput& data) override {
    if (m_done) {
      return true;
    }

    if (data.lev_data->level->tie_trees.front().empty()) {
      m_done = true;
      return true;
    }

    if (!m_vulkan_created) {
      for (int geo = 0; geo < tfrag3::TIE_GEOS; geo++) {
        auto& in_trees = data.lev_data->level->tie_trees[geo];
        for (auto& in_tree : in_trees) {
          LevelData::TieVulkan& tree_out = data.lev_data->tie_data[geo].emplace_back();
          tree_out.vertex_buffer = std::make_unique<VertexBuffer>(
              m_device, sizeof(tfrag3::PreloadedVertex), in_tree.unpacked.vertices.size(),
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);
        }
      }
      m_vulkan_created = true;
      return false;
    }

    if (!m_verts_done) {
      constexpr u32 CHUNK_SIZE = 32768;
      u32 uploaded_bytes = 0;

      while (true) {
        const auto& tree = data.lev_data->level->tie_trees[m_next_geo][m_next_tree];
        u32 end_vert_in_tree = tree.unpacked.vertices.size();
        // the number of vertices we'd need to finish the tree right now
        size_t num_verts_left_in_tree = end_vert_in_tree - m_next_vert;
        size_t start_vert_for_chunk;
        size_t end_vert_for_chunk;

        bool complete_tree;

        if (num_verts_left_in_tree > CHUNK_SIZE) {
          complete_tree = false;
          // should only do partial
          start_vert_for_chunk = m_next_vert;
          end_vert_for_chunk = start_vert_for_chunk + CHUNK_SIZE;
          m_next_vert += CHUNK_SIZE;
        } else {
          // should do all!
          start_vert_for_chunk = m_next_vert;
          end_vert_for_chunk = end_vert_in_tree;
          complete_tree = true;
        }

        u32 upload_size =
            (end_vert_for_chunk - start_vert_for_chunk) * sizeof(tfrag3::PreloadedVertex);

        data.lev_data->tie_data[m_next_geo][m_next_tree].vertex_buffer->map(
            upload_size,
            start_vert_for_chunk * sizeof(tfrag3::PreloadedVertex));
        data.lev_data->tie_data[m_next_geo][m_next_tree].vertex_buffer->writeToBuffer(
            (tfrag3::PreloadedVertex*)tree.unpacked.vertices.data() + start_vert_for_chunk);
        data.lev_data->tie_data[m_next_geo][m_next_tree].vertex_buffer->unmap();
        uploaded_bytes += upload_size;

        if (complete_tree) {
          // and move on to next tree
          m_next_vert = 0;
          m_next_tree++;
          if (m_next_tree >= data.lev_data->level->tie_trees[m_next_geo].size()) {
            m_next_tree = 0;
            m_next_geo++;
            if (m_next_geo >= tfrag3::TIE_GEOS) {
              m_verts_done = true;
              m_next_tree = 0;
              m_next_geo = 0;
              m_next_vert = 0;
              return false;
            }
          }
        }

        if (timer.getMs() > LOAD_BUDGET || (uploaded_bytes / 1024) > 2048) {
          return false;
        }
      }
    }

    if (!m_wind_indices_done) {
      bool abort = false;
      for (; m_next_geo < tfrag3::TIE_GEOS; m_next_geo++) {
        auto& geo_trees = data.lev_data->level->tie_trees[m_next_geo];
        for (; m_next_tree < geo_trees.size(); m_next_tree++) {
          if (abort) {
            return false;
          }
          auto& in_tree = geo_trees[m_next_tree];
          auto& out_tree = data.lev_data->tie_data[m_next_geo][m_next_tree];
          size_t wind_idx_buffer_len = 0;
          for (auto& draw : in_tree.instanced_wind_draws) {
            wind_idx_buffer_len += draw.vertex_index_stream.size();
          }
          if (wind_idx_buffer_len > 0) {
            out_tree.has_wind = true;
            std::vector<u32> temp;
            temp.resize(wind_idx_buffer_len);
            u32 off = 0;
            for (auto& draw : in_tree.instanced_wind_draws) {
              memcpy(temp.data() + off, draw.vertex_index_stream.data(),
                     draw.vertex_index_stream.size() * sizeof(u32));
              off += draw.vertex_index_stream.size();
            }
            out_tree.wind_indices = std::make_unique<IndexBuffer>(
                m_device, sizeof(u32), wind_idx_buffer_len,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

            out_tree.wind_indices->map();
            out_tree.wind_indices->writeToBuffer((u32*)temp.data());
            out_tree.wind_indices->unmap();
            abort = true;
          }
        }
        m_next_tree = 0;
      }

      m_indices_done = true;
      m_done = true;
      return true;
    }

    return false;
  }

  void reset() override {
    m_done = false;
    m_vulkan_created = false;
    m_next_geo = 0;
    m_next_tree = 0;
    m_next_vert = 0;
    m_verts_done = false;
    m_indices_done = false;
    m_wind_indices_done = false;
    m_tie_index_buffer.reset();
    m_tie_vertex_buffer.reset();
  }

 private:
  std::unique_ptr<VertexBuffer> m_tie_vertex_buffer;
  std::unique_ptr<IndexBuffer> m_tie_index_buffer;
  bool m_done = false;
  bool m_vulkan_created = false;
  bool m_verts_done = false;
  bool m_indices_done = false;
  bool m_wind_indices_done = false;
  u32 m_next_geo = 0;
  u32 m_next_tree = 0;
  u32 m_next_vert = 0;
};

class CollideLoaderStage : public LoaderStage {
 public:
  CollideLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStage(device, "collide") {}
  bool run(Timer& /*timer*/, LoaderInput& data) override {
    if (m_done) {
      return true;
    }
    if (!m_vulkan_created) {
      m_collide_vertex_buffer = std::make_unique<VertexBuffer>(m_device, sizeof(tfrag3::CollisionMesh::Vertex),
                                                               data.lev_data->level->collision.vertices.size(),
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                                               1);
      m_vulkan_created = true;
      return false;
    }

    u32 start = m_vtx;
    u32 end = std::min((u32)data.lev_data->level->collision.vertices.size(), start + 32768);
    m_collide_vertex_buffer->map((end - start) * sizeof(tfrag3::CollisionMesh::Vertex),
                                 start * sizeof(tfrag3::CollisionMesh::Vertex));
    m_collide_vertex_buffer->writeToBuffer(data.lev_data->level->collision.vertices.data() + start,
                                           (end - start) * sizeof(tfrag3::CollisionMesh::Vertex), 0);
    m_collide_vertex_buffer->unmap();
    m_vtx = end;

    if (m_vtx == data.lev_data->level->collision.vertices.size()) {
      m_done = true;
      return true;
    } else {
      return false;
    }
  }
  void reset() override {
    m_vulkan_created = false;
    m_vtx = 0;
    m_done = false;
    m_collide_vertex_buffer.reset();
  }

 private:
  std::unique_ptr<VertexBuffer> m_collide_vertex_buffer;
  bool m_vulkan_created = false;
  u32 m_vtx = 0;
  bool m_done = false;
};

class StallLoaderStage : public LoaderStage {
 public:
  StallLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStage(device, "stall") {}
  bool run(Timer&, LoaderInput& /*data*/) override {
    m_count++;
    if (m_count > 10) {
      return true;
    }
    return false;
  }

  void reset() override { m_count = 0; }

 private:
  int m_count = 0;
};

MercLoaderStage::MercLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device)
    : LoaderStage(device, "merc") {}
void MercLoaderStage::reset() {
  m_done = false;
  m_vulkan = false;
  m_vtx_uploaded = false;
  m_idx = 0;
}

bool MercLoaderStage::run(Timer& /*timer*/, LoaderInput& data) {
  if (m_done) {
    return true;
  }

  if (!m_vulkan) {
    data.lev_data->merc_indices = std::make_unique<IndexBuffer>(
        m_device, sizeof(u32), data.lev_data->level->merc_data.indices.size(),
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

    data.lev_data->merc_vertices = std::make_unique<VertexBuffer>(
        m_device, sizeof(tfrag3::MercVertex), data.lev_data->level->merc_data.vertices.size(),
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

    m_vulkan = true;
  }

  if (!m_vtx_uploaded) {
    u32 start = m_idx;
    m_idx = std::min(start + 32768, (u32)data.lev_data->level->merc_data.indices.size());
    data.lev_data->merc_indices->map((m_idx - start) * sizeof(u32), start * sizeof(u32));
    data.lev_data->merc_indices->writeToBuffer((u32*)data.lev_data->level->merc_data.indices.data() +
                                               start);
    data.lev_data->merc_indices->unmap();
    if (m_idx != data.lev_data->level->merc_data.indices.size()) {
      return false;
    } else {
      m_idx = 0;
      m_vtx_uploaded = true;
    }
  }

  u32 start = m_idx;
  m_idx = std::min(start + 32768, (u32)data.lev_data->level->merc_data.vertices.size());
  data.lev_data->merc_vertices->map((m_idx - start) * sizeof(tfrag3::MercVertex),
                                    start * sizeof(tfrag3::MercVertex));
  data.lev_data->merc_vertices->writeToBuffer(
      (tfrag3::MercVertex*)data.lev_data->level->merc_data.indices.data() +
                                             start);
  data.lev_data->merc_vertices->unmap();

  if (m_idx != data.lev_data->level->merc_data.vertices.size()) {
    return false;
  } else {
    m_done = true;
    for (auto& model : data.lev_data->level->merc_data.models) {
      data.lev_data->merc_model_lookup[model.name] = &model;
      (*data.mercs)[model.name].push_back({&model, data.lev_data->load_id, data.lev_data});
    }
    return true;
  }
  return true;
}

std::vector<std::unique_ptr<LoaderStage>> vk_loader_stage::make_loader_stages(std::unique_ptr<GraphicsDeviceVulkan>& device) {
  std::vector<std::unique_ptr<LoaderStage>> ret;
  ret.push_back(std::make_unique<TieLoadStage>(device));
  ret.push_back(std::make_unique<TextureLoaderStage>(device));
  ret.push_back(std::make_unique<TfragLoadStage>(device));
  ret.push_back(std::make_unique<ShrubLoadStage>(device));
  ret.push_back(std::make_unique<CollideLoaderStage>(device));
  ret.push_back(std::make_unique<StallLoaderStage>(device));
  ret.push_back(std::make_unique<MercLoaderStage>(device));
  return ret;
}
