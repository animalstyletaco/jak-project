#include "Tfrag3.h"

#include "third-party/imgui/imgui.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

Tfrag3Vulkan::Tfrag3Vulkan(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VulkanInitializationInfo& vulkan_info)
    : m_device(device), m_vulkan_info(vulkan_info), m_pipeline_layout(device) {
  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);
  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_debug_pipeline_config_info);

  m_vertex_shader_uniform_buffer =
      std::make_unique<BackgroundCommonVertexUniformBuffer>(device, TIME_OF_DAY_COLOR_COUNT, 1);
  m_time_of_day_color_uniform_buffer =
      std::make_unique<BackgroundCommonFragmentUniformBuffer>(device, TIME_OF_DAY_COLOR_COUNT, 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT, TIME_OF_DAY_COLOR_COUNT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, TIME_OF_DAY_COLOR_COUNT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, m_vulkan_info.descriptor_pool);

  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);
  m_vertex_shader_descriptor_sets.resize(TIME_OF_DAY_COLOR_COUNT);
  m_fragment_shader_descriptor_sets.resize(TIME_OF_DAY_COLOR_COUNT);

  m_vertex_shader_buffer_descriptor_info =
      VkDescriptorBufferInfo{m_vertex_shader_uniform_buffer->getBuffer(), 0,
                             sizeof(BackgroundCommonVertexUniformShaderData)};
  m_fragment_buffer_descriptor_info =
      VkDescriptorBufferInfo{m_time_of_day_color_uniform_buffer->getBuffer(), 0,
                             sizeof(BackgroundCommonVertexUniformShaderData)};

}


void Tfrag3Vulkan::InitializeDebugInputVertexAttribute() {
  m_debug_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_debug_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(BaseTfrag3::DebugVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_debug_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(BaseTfrag3::DebugVertex, position);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(BaseTfrag3::DebugVertex, rgba);
  m_debug_pipeline_config_info.attributeDescriptions.insert(
      m_debug_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void Tfrag3Vulkan::InitializeInputVertexAttribute() {
  //            glBufferData(GL_ARRAY_BUFFER, verts * sizeof(tfrag3::PreloadedVertex),
  //            nullptr,
  //                         GL_STREAM_DRAW);
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::PreloadedVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::PreloadedVertex, x);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::PreloadedVertex, s);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R16_UINT;
  attributeDescriptions[2].offset = offsetof(tfrag3::PreloadedVertex, color_index);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

Tfrag3Vulkan::~Tfrag3Vulkan() {
  discard_tree_cache();
}

BaseTfrag3::TreeCache& Tfrag3Vulkan::get_cached_tree(int bucket_index, int cache_index) {
  return m_cached_trees[bucket_index][cache_index];
}

size_t Tfrag3Vulkan::get_total_cached_trees_count(int bucket_index) {
  return m_cached_trees[bucket_index].size();
}

void Tfrag3Vulkan::update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                         const LevelDataVulkan* loader_data) {
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
        tree_cache.draw_mode = tree.use_strips ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        vis_temp_len = std::max(vis_temp_len, tree.bvh.vis_nodes.size());
        tree_cache.vertex_buffer = std::make_unique<VertexBuffer>(
            get_logical_device(), sizeof(tfrag3::PreloadedVertex), verts, 1);
        tree_cache.index_buffer = std::make_unique<IndexBuffer>(get_logical_device(), sizeof(u32),
                                                                tree.unpacked.indices.size(), 1);

        tree_cache.time_of_day_texture = std::make_unique<VulkanTexture>(get_logical_device());
        tree_cache.time_of_day_texture->createImage(
            {TIME_OF_DAY_COLOR_COUNT, 0, 0}, 1, VK_IMAGE_TYPE_1D, 
            VK_FORMAT_A8B8G8R8_SRGB_PACK32, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        tree_cache.time_of_day_texture->createImageView(
            VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SRGB_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, 1);
      }
    }
  }

  m_cache.vis_temp.resize(vis_temp_len);
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multi_draw_indexed_infos.resize(max_draws);
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
    m_textures = &lev_data->textures_map;
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
                               VK_FORMAT_A8B8G8R8_SRGB_PACK32, VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  timeOfDayTexture.createImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SRGB_PACK32,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1);

  timeOfDayTexture.writeToImage(m_color_result.data());

  vulkan_background_common::first_tfrag_draw_setup(settings, render_state,
                                                        m_vertex_shader_uniform_buffer);

  background_common::cull_check_all_slow(settings.planes, tree.vis->vis_nodes, settings.occlusion_culling,
                      m_cache.vis_temp.data());

  u32 total_tris;
  if (render_state->no_multidraw) {
    u32 idx_buffer_size = vulkan_background_common::make_index_list_from_vis_string(
        m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, m_cache.vis_temp,
        tree.index_data, &total_tris);
    //CreateIndexBuffer(m_cache.index_temp);
    //glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size * sizeof(u32), m_cache.index_temp.data(),
    //             GL_STREAM_DRAW);
  } else {
    total_tris = vulkan_background_common::make_multidraws_from_vis_string(
        m_cache.multi_draw_indexed_infos, *tree.draws, m_cache.vis_temp);
  }

  prof.add_tri(total_tris);

  for (size_t draw_idx = 0; draw_idx < tree.draws->size(); draw_idx++) {
    const auto& draw = tree.draws->operator[](draw_idx);
    const auto& multidraw_indices = m_cache.multidraw_offset_per_stripdraw[draw_idx];
    const auto& singledraw_indices = m_cache.draw_idx_temp[draw_idx];

    if (render_state->no_multidraw) {
      if (singledraw_indices.number_of_draws == 0) {
        continue;
      }
    } else {
      if (multidraw_indices.number_of_draws == 0) {
        continue;
      }
    }

    ASSERT(m_textures);
    VulkanTexture& vulkanTexture = m_textures->at(draw.tree_tex_id);

    auto double_draw = vulkan_background_common::setup_tfrag_shader(
        render_state, draw.mode, &vulkanTexture,
        m_time_of_day_samplers[draw.tree_tex_id], m_pipeline_config_info,
        m_time_of_day_color_uniform_buffer);
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
        m_time_of_day_color_uniform_buffer->SetUniform1f("alpha_min", -10.f, draw_idx);
        m_time_of_day_color_uniform_buffer->SetUniform1f("alpha_max", double_draw.aref_second,
                                                         draw_idx);
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

  for (uint32_t instanceIdx = 0; instanceIdx < m_vertex_shader_uniform_buffer->getInstanceCount();
       instanceIdx++) {
    m_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
        "camera", 1, GL_FALSE, (float*)settings.math_camera.data(), instanceIdx);
    m_vertex_shader_uniform_buffer->SetUniform4f("hvdf_offset", settings.hvdf_offset[0],
                                                 settings.hvdf_offset[1], settings.hvdf_offset[2],
                                                 settings.hvdf_offset[3], instanceIdx);
    m_vertex_shader_uniform_buffer->SetUniform1f("fog_constant", settings.fog.x(), instanceIdx);
  }

  m_debug_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_debug_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
   
  m_debug_pipeline_config_info.colorBlendAttachment.colorWriteMask =
       VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
       VK_COLOR_COMPONENT_A_BIT;
   m_debug_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

   m_debug_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
   m_debug_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
   
   m_debug_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   m_debug_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
         VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   
   m_debug_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor =
       VK_BLEND_FACTOR_SRC_ALPHA;
   m_debug_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor =
       VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  int vertex_size = (m_debug_vert_data.size() < DEBUG_TRI_COUNT * 3) ? m_debug_vert_data.size() : DEBUG_TRI_COUNT * 3;

  VertexBuffer m_debug_vertex_buffer(m_device, sizeof(BaseTfrag3::DebugVertex), vertex_size, 1);
  m_debug_vertex_buffer.writeToGpuBuffer(m_debug_vert_data.data(), vertex_size, 0);
  // TODO: Add draw function here

}

void Tfrag3Vulkan::initialize_debug_pipeline() {
  m_pipeline_layout.createGraphicsPipeline(m_debug_pipeline_config_info);

  auto& shader = m_vulkan_info.shaders[ShaderId::TFRAG3_NO_TEX];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "main";

  m_debug_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(BaseTfrag3::DebugVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions = {bindingDescription};

  std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(BaseTfrag3::DebugVertex, position);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(BaseTfrag3::DebugVertex, rgba);

  m_debug_pipeline_config_info.attributeDescriptions.insert(
      m_debug_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

std::unique_ptr<GraphicsDeviceVulkan>& Tfrag3Vulkan::get_logical_device() {
  return m_vulkan_info.swap_chain->getLogicalDevice();
}
