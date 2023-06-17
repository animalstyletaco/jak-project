#include "LoaderStages.h"

#include "Loader.h"

constexpr float LOAD_BUDGET = 2.5f;

/*!
 * Upload a texture to the GPU, and give it to the pool.
 */
void vk_loader_stage::update_texture(VulkanTexturePool& pool,
                                     const tfrag3::Texture& tex,
                                     VulkanTexture* texture_info,
                                     bool is_common) {
  VkExtent3D extents{tex.w, tex.h, 1};
  texture_info->createImage(extents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                           VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  texture_info->writeToImage((u32*)tex.data.data());

  //TODO: Get Mipmap Level here
  unsigned mipLevels = 1;

  texture_info->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_ASPECT_COLOR_BIT, 1);

  if (tex.load_to_pool) {
    VulkanTextureInput in;
    in.debug_page_name = tex.debug_tpage_name;
    in.debug_name = tex.debug_name;
    in.texture = texture_info;
    in.common = is_common;
    in.id = PcTextureId::from_combo_id(tex.combo_id);
    pool.give_texture(in);
  }
}

class TextureVulkanLoaderStage : public LoaderStageVulkan {
 public:
  TextureVulkanLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStageVulkan(device, "texture") {}
  bool run(Timer& timer, LoaderInputVulkan& data) override {
    constexpr int MAX_TEX_BYTES_PER_FRAME = 1024 * 512;

    int bytes_this_run = 0;
    int tex_this_run = 0;
    if (data.lev_data->textures_map.size() < data.lev_data->level->textures.size()) {
      std::unique_lock<std::mutex> tpool_lock(data.tex_pool->mutex());

      u32 texture_id = data.lev_data->textures_map.size();
      while (data.lev_data->textures_map.size() < data.lev_data->level->textures.size()) {
        auto& level_texture = data.lev_data->level->textures[data.lev_data->textures_map.size()];

        data.lev_data->textures_map.insert(std::pair<u32, VulkanTexture>(texture_id, VulkanTexture{m_device}));
        vk_loader_stage::update_texture(*data.tex_pool, level_texture, &data.lev_data->textures_map.at(texture_id), false);
        texture_id++;

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
    return data.lev_data->textures_map.size() == data.lev_data->level->textures.size();
  }
  void reset() override {}
};

class TfragVulkanLoadStage : public LoaderStageVulkan {
 public:
  TfragVulkanLoadStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStageVulkan(device, "tfrag") {}
  bool run(Timer& timer, LoaderInputVulkan& data) override {
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
          data.lev_data->tfrag_vertex_data[geo].push_back(
              {m_device, sizeof(tfrag3::PreloadedVertex), in_tree.unpacked.vertices.size(), 1});
          data.lev_data->tfrag_indices_data[geo].push_back(
              {m_device, sizeof(u32), in_tree.unpacked.indices.size(), 1});
          data.lev_data->tfrag_indices_data[geo].back().writeToGpuBuffer(
              in_tree.unpacked.indices.data(),
              sizeof(u32) * in_tree.unpacked.indices.size());
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
        size_t end_vert_in_tree = tree.unpacked.vertices.size();
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
        tree_vertex_buffer.writeToGpuBuffer(
            (tfrag3::PreloadedVertex*)tree.unpacked.vertices.data() + start_vert_for_chunk,
            upload_size, start_vert_for_chunk * sizeof(tfrag3::PreloadedVertex));

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

class ShrubVulkanLoadStage : public LoaderStageVulkan {
 public:
  ShrubVulkanLoadStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStageVulkan(device, "shrub") {}
  bool run(Timer& timer, LoaderInputVulkan& data) override {
    if (m_done) {
      return true;
    }

    if (data.lev_data->level->shrub_trees.empty()) {
      m_done = true;
      return true;
    }

    if (!m_vulkan_created) {
      for (auto& in_tree : data.lev_data->level->shrub_trees) {
        data.lev_data->shrub_vertex_data.push_back(VertexBuffer{m_device, sizeof(tfrag3::ShrubGpuVertex),
                                                   in_tree.unpacked.vertices.size(), 1
      });
      }
      m_vulkan_created = true;
      return false;
    }

    constexpr u32 CHUNK_SIZE = 32768;
    u32 uploaded_bytes = 0;

    while (true) {
      const auto& tree = data.lev_data->level->shrub_trees[m_next_tree];
      size_t end_vert_in_tree = tree.unpacked.vertices.size();
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
      data.lev_data->shrub_vertex_data[m_next_tree].writeToGpuBuffer(
        (tfrag3::ShrubGpuVertex*)tree.unpacked.vertices.data(), upload_size, 
        start_vert_for_chunk * sizeof(tfrag3::PreloadedVertex));
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

class TieVulkanLoadStage : public LoaderStageVulkan {
 public:
  TieVulkanLoadStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStageVulkan(device, "tie") {}
  bool run(Timer& timer, LoaderInputVulkan& data) override {
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
          LevelDataVulkan::TieVulkan& tree_out = data.lev_data->tie_data[geo].emplace_back();
          tree_out.vertex_buffer = std::make_unique<VertexBuffer>(
              m_device, sizeof(tfrag3::PreloadedVertex), in_tree.unpacked.vertices.size(), 1);
          tree_out.index_buffer = std::make_unique<IndexBuffer>(
              m_device, sizeof(u32), in_tree.unpacked.indices.size(), 1);
          tree_out.index_buffer->writeToGpuBuffer(in_tree.unpacked.indices.data());
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
        data.lev_data->tie_data[m_next_geo][m_next_tree].vertex_buffer->writeToGpuBuffer(
            (tfrag3::PreloadedVertex*)tree.unpacked.vertices.data() + start_vert_for_chunk,
            upload_size, start_vert_for_chunk * sizeof(tfrag3::PreloadedVertex));

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
            out_tree.wind_vertices =
                std::make_unique<VertexBuffer>(m_device, sizeof(u32), wind_idx_buffer_len, 1);
            out_tree.wind_indices = std::make_unique<IndexBuffer>(
                m_device, sizeof(u32), wind_idx_buffer_len, 1);

            out_tree.wind_vertices->writeToGpuBuffer((u32*)temp.data());
            out_tree.wind_indices->writeToGpuBuffer((u32*)temp.data());
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

class CollideVulkanLoaderStage : public LoaderStageVulkan {
 public:
  CollideVulkanLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStageVulkan(device, "collide") {}
  bool run(Timer& /*timer*/, LoaderInputVulkan& data) override {
    if (m_done) {
      return true;
    }
    if (!m_vulkan_created) {
      m_collide_vertex_buffer = std::make_unique<VertexBuffer>(m_device, sizeof(tfrag3::CollisionMesh::Vertex),
                                                               data.lev_data->level->collision.vertices.size(),
                                                               1);
      m_vulkan_created = true;
      return false;
    }

    u32 start = m_vtx;
    u32 end = std::min((u32)data.lev_data->level->collision.vertices.size(), start + 32768);
    m_collide_vertex_buffer->writeToGpuBuffer(data.lev_data->level->collision.vertices.data() + start,
                                           (end - start) * sizeof(tfrag3::CollisionMesh::Vertex), 0);
    m_vtx = end;

    m_done = (m_vtx == data.lev_data->level->collision.vertices.size());
    return m_done;
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

class StallVulkanLoaderStage : public LoaderStageVulkan {
 public:
  StallVulkanLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device) : LoaderStageVulkan(device, "stall") {}
  bool run(Timer&, LoaderInputVulkan& /*data*/) override {
    return m_count++ > 10;
  }

  void reset() override { m_count = 0; }

 private:
  int m_count = 0;
};

MercVulkanLoaderStage::MercVulkanLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device)
    : LoaderStageVulkan(device, "merc") {}
void MercVulkanLoaderStage::reset() {
  m_done = false;
  m_vulkan = false;
  m_vtx_uploaded = false;
  m_idx = 0;
}

bool MercVulkanLoaderStage::run(Timer& /*timer*/, LoaderInputVulkan& data) {
  if (m_done) {
    return true;
  }

  if (!m_vulkan) {
    data.lev_data->merc_indices = std::make_unique<IndexBuffer>(
        m_device, sizeof(u32), data.lev_data->level->merc_data.indices.size(), 1);

    data.lev_data->merc_vertices = std::make_unique<VertexBuffer>(
        m_device, sizeof(tfrag3::MercVertex), data.lev_data->level->merc_data.vertices.size(), 1);

    m_vulkan = true;
  }

  if (!m_vtx_uploaded) {
    u32 start = m_idx;
    m_idx = std::min(start + 32768, (u32)data.lev_data->level->merc_data.indices.size());
    data.lev_data->merc_indices->writeToGpuBuffer((u32*)data.lev_data->level->merc_data.indices.data() + start,
      (m_idx - start) * sizeof(u32),
      start * sizeof(u32));
    if (m_idx != data.lev_data->level->merc_data.indices.size()) {
      return false;
    }

    m_idx = 0;
    m_vtx_uploaded = true;
  }

  u32 start = m_idx;
  m_idx = std::min(start + 32768, (u32)data.lev_data->level->merc_data.vertices.size());
  data.lev_data->merc_vertices->writeToGpuBuffer(
      data.lev_data->level->merc_data.vertices.data(),
      (m_idx - start) * sizeof(tfrag3::MercVertex), start * sizeof(tfrag3::MercVertex));

  if (m_idx != data.lev_data->level->merc_data.vertices.size()) {
    return false;
  }

  m_done = true;
  for (auto& model : data.lev_data->level->merc_data.models) {
    data.lev_data->merc_model_lookup[model.name] = &model;
    (*data.mercs)[model.name].push_back({&model, data.lev_data->load_id, data.lev_data});
  }
  return true;
}

std::vector<std::unique_ptr<LoaderStageVulkan>> vk_loader_stage::make_loader_stages(std::unique_ptr<GraphicsDeviceVulkan>& device) {
  std::vector<std::unique_ptr<LoaderStageVulkan>> ret;
  ret.push_back(std::make_unique<TieVulkanLoadStage>(device));
  ret.push_back(std::make_unique<TextureVulkanLoaderStage>(device));
  ret.push_back(std::make_unique<TfragVulkanLoadStage>(device));
  ret.push_back(std::make_unique<ShrubVulkanLoadStage>(device));
  ret.push_back(std::make_unique<CollideVulkanLoaderStage>(device));
  ret.push_back(std::make_unique<StallVulkanLoaderStage>(device));
  ret.push_back(std::make_unique<MercVulkanLoaderStage>(device));
  return ret;
}
