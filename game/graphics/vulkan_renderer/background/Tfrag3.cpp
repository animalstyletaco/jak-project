#include "Tfrag3.h"

#include "third-party/imgui/imgui.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

Tfrag3Vulkan::Tfrag3Vulkan(VulkanInitializationInfo& vulkan_info,
       PipelineConfigInfo& pipeline_config_info,
       GraphicsPipelineLayout& pipeline_layout,
       std::unique_ptr<DescriptorWriter>& vertex_description_writer,
       std::unique_ptr<DescriptorWriter>& fragment_description_writer,
       std::unique_ptr<BackgroundCommonVertexUniformBuffer>& vertex_shader_uniform_buffer,
       std::unique_ptr<BackgroundCommonFragmentUniformBuffer>& fragment_shader_uniform_buffer) :
  m_pipeline_config_info(pipeline_config_info), m_pipeline_layout(pipeline_layout), m_vulkan_info(vulkan_info),
  m_vertex_descriptor_writer(vertex_description_writer), m_fragment_descriptor_writer(fragment_description_writer),
  m_vertex_shader_uniform_buffer(vertex_shader_uniform_buffer), m_time_of_day_color(fragment_shader_uniform_buffer){

  m_sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  m_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  m_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  m_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  m_sampler_info.anisotropyEnable = VK_TRUE;
  // m_sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  m_sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  m_sampler_info.unnormalizedCoordinates = VK_FALSE;
  m_sampler_info.compareEnable = VK_FALSE;
  m_sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
  m_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  m_sampler_info.minLod = 0.0f;
  // m_sampler_info.maxLod = static_cast<float>(mipLevels);
  m_sampler_info.mipLodBias = 0.0f;

  m_sampler_info.minFilter = VK_FILTER_NEAREST;
  m_sampler_info.magFilter = VK_FILTER_NEAREST;

  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);
}

Tfrag3Vulkan::~Tfrag3Vulkan() {
  discard_tree_cache();
}

void Tfrag3Vulkan::update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                         const BaseLevelData* loader_data) {
  const auto* lev_data = loader_data->level.get();
  discard_tree_cache();
  for (int geom = 0; geom < GEOM_MAX; ++geom) {
    m_cached_trees[geom].clear();
  }

  size_t time_of_day_count = 0;
  size_t vis_temp_len = 0;
  size_t max_draws = 0;
  size_t max_num_grps = 0;
  size_t max_inds = 0;

  std::vector<VkImage> textures[GEOM_MAX];
  std::vector<VkDeviceMemory> texture_memories[GEOM_MAX];

  for (int geom = 0; geom < GEOM_MAX; ++geom) {
    for (size_t tree_idx = 0; tree_idx < lev_data->tfrag_trees[geom].size(); tree_idx++) {
      const auto& tree = lev_data->tfrag_trees[geom][tree_idx];

      if (std::find(tree_kinds.begin(), tree_kinds.end(), tree.kind) != tree_kinds.end()) {
        auto& tree_cache = m_cached_trees[geom].emplace_back();
        tree_cache.kind = tree.kind;
        max_draws = std::max(tree.draws.size(), max_draws);
        size_t num_grps = 0;
        for (auto& draw : tree.draws) {
          num_grps += draw.vis_groups.size();
        }
        max_num_grps = std::max(max_num_grps, num_grps);
        max_inds = std::max(tree.unpacked.indices.size(), max_inds);
        time_of_day_count = std::max(tree.colors.size(), time_of_day_count);
        u32 verts = tree.packed_vertices.vertices.size();

        tree_cache.vert_count = verts;
        tree_cache.draws = &tree.draws;  // todo - should we just copy this?
        tree_cache.colors = &tree.colors;
        tree_cache.vis = &tree.bvh;
        tree_cache.tod_cache = background_common::swizzle_time_of_day(tree.colors);
        tree_cache.draw_mode = tree.use_strips ? GL_TRIANGLE_STRIP : GL_TRIANGLES;
        vis_temp_len = std::max(vis_temp_len, tree.bvh.vis_nodes.size());

        //CreateIndexBuffer(tree.unpacked.indices);
        //CreateImage(0, 0, VK_IMAGE_TYPE_1D, 1, VK_SAMPLE_COUNT_1_BIT,
        //            VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
        //            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        //            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textures[geom][tree_idx], texture_memories[geom][tree_idx]);
      }
    }
  }

  m_cache.vis_temp.resize(vis_temp_len);
  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multidraw_count_buffer.resize(max_num_grps);
  m_cache.multidraw_index_offset_buffer.resize(max_num_grps);
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  ASSERT(time_of_day_count <= TIME_OF_DAY_COLOR_COUNT);
}

bool Tfrag3Vulkan::setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                             const std::string& level,
                             BaseSharedRenderState* render_state) {
  // make sure we have the level data.
  Timer tfrag3_setup_timer;
  auto lev_data = m_vulkan_info.loader->get_tfrag3_level(level);
  if (!lev_data || (m_has_level && lev_data->load_id != m_load_id)) {
    m_has_level = false;
    m_textures = nullptr;
    m_level_name = "";
    discard_tree_cache();
    return false;
  }
  m_load_id = lev_data->load_id;

  if (m_level_name != level) {
    update_load(tree_kinds, lev_data);
    m_has_level = true;
    m_textures = &lev_data->textures;
    m_level_name = level;
  } else {
    m_has_level = true;
  }

  if (tfrag3_setup_timer.getMs() > 5) {
    lg::info("TFRAG setup: {:.1f}ms", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}


void Tfrag3Vulkan::render_tree(int geom,
                         const TfragRenderSettings& settings,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof) {
  if (!m_has_level) {
    return;
  }
  auto& tree = m_cached_trees.at(geom).at(settings.tree_idx);
  ASSERT(tree.kind != tfrag3::TFragmentTreeKind::INVALID);

  if (m_color_result.size() < tree.colors->size()) {
    m_color_result.resize(tree.colors->size());
  }
  if (m_use_fast_time_of_day) {
    background_common::interp_time_of_day_fast(settings.itimes, tree.tod_cache, m_color_result.data());
  } else {
    background_common::interp_time_of_day_slow(settings.itimes, *tree.colors, m_color_result.data());
  }

  VulkanTexture timeOfDayTexture{m_vertex_shader_uniform_buffer->getDevice()};
  VkDeviceSize size = m_color_result.size() * sizeof(m_color_result[0]);

  VkExtent3D extents{tree.colors->size(), 1, 1};
  timeOfDayTexture.createImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT,
                               VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  timeOfDayTexture.createImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SINT_PACK32,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1);

  timeOfDayTexture.writeToImage(m_color_result.data());

  background_common::first_tfrag_draw_setup(settings, render_state,
                                                        m_vertex_shader_uniform_buffer);

  background_common::cull_check_all_slow(settings.planes, tree.vis->vis_nodes, settings.occlusion_culling,
                      m_cache.vis_temp.data());

  u32 total_tris;
  if (render_state->no_multidraw) {
    u32 idx_buffer_size = background_common::make_index_list_from_vis_string(
        m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, m_cache.vis_temp,
        tree.index_data, &total_tris);
    //CreateIndexBuffer(m_cache.index_temp);
    //glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size * sizeof(u32), m_cache.index_temp.data(),
    //             GL_STREAM_DRAW);
  } else {
    total_tris = background_common::make_multidraws_from_vis_string(
        m_cache.multidraw_offset_per_stripdraw.data(), m_cache.multidraw_count_buffer.data(),
        m_cache.multidraw_index_offset_buffer.data(), *tree.draws, m_cache.vis_temp);
  }

  prof.add_tri(total_tris);

  for (size_t draw_idx = 0; draw_idx < tree.draws->size(); draw_idx++) {
    const auto& draw = tree.draws->operator[](draw_idx);
    const auto& multidraw_indices = m_cache.multidraw_offset_per_stripdraw[draw_idx];
    const auto& singledraw_indices = m_cache.draw_idx_temp[draw_idx];

    if (render_state->no_multidraw) {
      if (singledraw_indices.second == 0) {
        continue;
      }
    } else {
      if (multidraw_indices.second == 0) {
        continue;
      }
    }

    ASSERT(m_textures);
    auto double_draw = background_common::setup_tfrag_shader(render_state, draw.mode, (VulkanTexture*)&m_textures->at(draw.tree_tex_id), m_pipeline_config_info,
        m_time_of_day_color);
    tree.tris_this_frame += draw.num_triangles;
    tree.draws_this_frame++;

    prof.add_draw_call();
    if (render_state->no_multidraw) {
      //glDrawElements(tree.draw_mode, singledraw_indices.second, GL_UNSIGNED_INT,
      //               (void*)(singledraw_indices.first * sizeof(u32)));
    } else {
      //glMultiDrawElements(tree.draw_mode, &m_cache.multidraw_count_buffer[multidraw_indices.first],
      //                    GL_UNSIGNED_INT,
      //                    &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
      //                    multidraw_indices.second);
    }

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        prof.add_draw_call();
        m_time_of_day_color->SetUniform1f("alpha_min", -10.f);
        m_time_of_day_color->SetUniform1f("alpha_max", double_draw.aref_second);
        //glDepthMask(GL_FALSE);
        if (render_state->no_multidraw) {
          //glDrawElements(tree.draw_mode, singledraw_indices.second, GL_UNSIGNED_INT,
          //               (void*)(singledraw_indices.first * sizeof(u32)));
        } else {
          //glMultiDrawElements(
          //    tree.draw_mode, &m_cache.multidraw_count_buffer[multidraw_indices.first],
          //    GL_UNSIGNED_INT, &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
          //    multidraw_indices.second);
        }
        break;
      default:
        ASSERT(false);
    }
  }
}

void Tfrag3Vulkan::render_matching_trees(int geom,
                                   const std::vector<tfrag3::TFragmentTreeKind>& trees,
                                   const TfragRenderSettings& settings,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < m_cached_trees[geom].size(); i++) {
    auto& tree = m_cached_trees[geom][i];
    tree.reset_stats();
    if (!tree.allowed) {
      continue;
    }
    if (std::find(trees.begin(), trees.end(), tree.kind) != trees.end() || tree.forced) {
      tree.rendered_this_frame = true;
      settings_copy.tree_idx = i;
      render_tree(geom, settings_copy, render_state, prof);
      if (tree.cull_debug) {
        render_tree_cull_debug(settings_copy, render_state, prof);
      }
    }
  }
}

void Tfrag3Vulkan::discard_tree_cache() {
  m_textures = nullptr;
  for (int geom = 0; geom < GEOM_MAX; ++geom) {
    for (auto& tree : m_cached_trees[geom]) {
      if (tree.kind != tfrag3::TFragmentTreeKind::INVALID) {
        //glDeleteTextures(1, &tree.time_of_day_texture);
      }
    }
    m_cached_trees[geom].clear();
  }
}

void Tfrag3Vulkan::render_tree_cull_debug(const TfragRenderSettings& settings,
                                    BaseSharedRenderState* render_state,
                                    ScopedProfilerNode& prof) {
  // generate debug verts:
  m_debug_vert_data.clear();
  auto& tree = m_cached_trees.at(settings.tree_idx).at(lod());

  debug_vis_draw(tree.vis->first_root, tree.vis->first_root, tree.vis->num_roots, 1,
                 tree.vis->vis_nodes, m_debug_vert_data);

   m_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "camera", 1,
      GL_FALSE, (float*)settings.math_camera.data());
  m_vertex_shader_uniform_buffer->SetUniform4f("hvdf_offset",
      settings.hvdf_offset[0], settings.hvdf_offset[1], settings.hvdf_offset[2],
      settings.hvdf_offset[3]);
   m_vertex_shader_uniform_buffer->SetUniform1f("fog_constant", settings.fog.x());

  //FIXME: Add depth test for vulkan
  //glEnable(GL_DEPTH_TEST);
  //glDepthFunc(GL_GEQUAL);
  //glEnable(GL_BLEND);
  //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // ?
  //glDepthMask(GL_FALSE);

  //glBindVertexArray(m_debug_vao);
  //glBindBuffer(GL_ARRAY_BUFFER, m_debug_verts);

  int remaining = m_debug_vert_data.size();
  int start = 0;

  while (remaining > 0) {
    int to_do = std::min(DEBUG_TRI_COUNT * 3, remaining);

    //SetShaderModule(render_state->shaders[ShaderId::TFRAG3_NO_TEX]);
    //void* data;
    //vkMapMemory(device, device_memory, 0, to_do * sizeof(DebugVertex), 0, &data);
    //::memcpy(data, m_debug_vert_data.data() + start, to_do * sizeof(DebugVertex));
    //vkUnmapMemory(device, device_memory, nullptr);

    //TODO: Add draw function here
    
    prof.add_draw_call();
    prof.add_tri(to_do / 3);

    remaining -= to_do;
    start += to_do;
  }
}
