#include "Tie3.h"

#include "third-party/imgui/imgui.h"

Tie3Vulkan::Tie3Vulkan(const std::string& name,
                       int my_id,
                       std::shared_ptr<GraphicsDeviceVulkan> device,
                       VulkanInitializationInfo& vulkan_info,
                       int level_id,
                       tfrag3::TieCategory category)
    : BaseTie3(name, my_id, level_id, category), BucketVulkanRenderer(device, vulkan_info) {
  m_tie_vertex_push_constant.height_scale = m_push_constant.height_scale;
  m_tie_vertex_push_constant.scissor_adjust = m_push_constant.scissor_adjust;

  m_etie_base_vertex_shader_uniform_buffer =
      std::make_unique<BackgroundCommonEtieBaseVertexUniformBuffer>(m_device, 1, 1);

  m_etie_vertex_shader_uniform_buffer =
      std::make_unique<BackgroundCommonEtieVertexUniformBuffer>(m_device, 1, 1);

  m_etie_base_descriptor_buffer_info = {m_etie_base_vertex_shader_uniform_buffer->getBuffer(), 0,
                                        sizeof(BackgroundCommonEtieBaseVertexUniformShaderData)};
  m_etie_descriptor_buffer_info = {m_etie_vertex_shader_uniform_buffer->getBuffer(), 0,
                                   sizeof(BackgroundCommonEtieVertexUniformShaderData)};

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_etie_base_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_etie_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  create_pipeline_layout();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_etie_base_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_etie_base_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_etie_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_etie_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_vertex_descriptor_writer->writeImage(0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());
  m_fragment_descriptor_writer->writeImage(0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());
  m_etie_base_vertex_descriptor_writer->writeImage(0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());
  m_etie_base_vertex_descriptor_writer->writeBuffer(1, &m_etie_base_descriptor_buffer_info);
  m_etie_vertex_descriptor_writer->writeBuffer(0, &m_etie_descriptor_buffer_info);

  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
  InitializeInputAttributes();
  
  auto vertexDescriptorSetLayout = m_vertex_descriptor_layout->getDescriptorSetLayout();
  auto etieVertexDescriptorSetLayout = m_etie_vertex_descriptor_layout->getDescriptorSetLayout();
  auto etieBaseVertexDescriptorSetLayout = m_etie_base_vertex_descriptor_layout->getDescriptorSetLayout();
  auto fragmentDescriptorSetLayout = m_fragment_descriptor_layout->getDescriptorSetLayout();

  AllocateDescriptorSets(m_global_vertex_shader_descriptor_sets, vertexDescriptorSetLayout,
                         background_common::TIME_OF_DAY_COLOR_COUNT);
  AllocateDescriptorSets(m_global_fragment_shader_descriptor_sets, fragmentDescriptorSetLayout,
                         background_common::TIME_OF_DAY_COLOR_COUNT);
  AllocateDescriptorSets(m_global_instanced_wind_vertex_shader_descriptor_sets,
                         vertexDescriptorSetLayout, 1000);
  AllocateDescriptorSets(m_global_instanced_wind_fragment_shader_descriptor_sets,
                         fragmentDescriptorSetLayout, 1000);
  AllocateDescriptorSets(m_global_etie_base_vertex_shader_descriptor_sets, etieBaseVertexDescriptorSetLayout,
                         background_common::TIME_OF_DAY_COLOR_COUNT);
  AllocateDescriptorSets(m_global_etie_vertex_shader_descriptor_sets, etieVertexDescriptorSetLayout,
                         background_common::TIME_OF_DAY_COLOR_COUNT);

  setup_shader(ShaderId::TFRAG3);
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
}

Tie3Vulkan::~Tie3Vulkan() {
  discard_tree_cache();

  vkFreeDescriptorSets(
      m_device->getLogicalDevice(), m_vulkan_info.descriptor_pool->getDescriptorPool(),
      m_global_vertex_shader_descriptor_sets.size(), m_global_vertex_shader_descriptor_sets.data());
  vkFreeDescriptorSets(
      m_device->getLogicalDevice(), m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_global_fragment_shader_descriptor_sets.size(),
                       m_global_fragment_shader_descriptor_sets.data());
  vkFreeDescriptorSets(m_device->getLogicalDevice(),
                       m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_global_etie_base_vertex_shader_descriptor_sets.size(),
                       m_global_etie_base_vertex_shader_descriptor_sets.data());
  vkFreeDescriptorSets(m_device->getLogicalDevice(),
                       m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_global_etie_vertex_shader_descriptor_sets.size(),
                       m_global_etie_vertex_shader_descriptor_sets.data());
  vkFreeDescriptorSets(m_device->getLogicalDevice(),
                       m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_global_instanced_wind_vertex_shader_descriptor_sets.size(),
                       m_global_instanced_wind_vertex_shader_descriptor_sets.data());
  vkFreeDescriptorSets(m_device->getLogicalDevice(),
                       m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_global_instanced_wind_fragment_shader_descriptor_sets.size(),
                       m_global_instanced_wind_fragment_shader_descriptor_sets.data());
}

void Tie3Vulkan::render(DmaFollower& dma,
                        SharedVulkanRenderState* render_state,
                        ScopedProfilerNode& prof) {
  BaseTie3::render(dma, render_state, prof);
}

void Tie3Vulkan::create_pipeline_layout() {
  // If push constants are needed put them here
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_vertex_descriptor_layout->getDescriptorSetLayout(),
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  std::array<VkPushConstantRange, 2> pushConstantRanges{};
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(m_tie_vertex_push_constant);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset = pushConstantRanges[0].size;
  pushConstantRanges[1].size = sizeof(m_time_of_day_color_push_constant);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_tie_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  VkPipelineLayoutCreateInfo etieBasePipelineLayoutInfo = pipelineLayoutInfo;
  std::vector<VkDescriptorSetLayout> etieBaseDescriptorSetLayouts{
    m_etie_base_vertex_descriptor_layout->getDescriptorSetLayout(),
    m_fragment_descriptor_layout->getDescriptorSetLayout()};

  etieBasePipelineLayoutInfo.pSetLayouts = etieBaseDescriptorSetLayouts.data();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &etieBasePipelineLayoutInfo, nullptr,
                             &m_etie_base_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  VkPipelineLayoutCreateInfo etiePipelineLayoutInfo = pipelineLayoutInfo;
  std::vector<VkDescriptorSetLayout> etieDescriptorSetLayouts{
      m_etie_vertex_descriptor_layout->getDescriptorSetLayout(),
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  etiePipelineLayoutInfo.pSetLayouts = etieDescriptorSetLayouts.data();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &etiePipelineLayoutInfo, nullptr,
                             &m_etie_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void Tie3Vulkan::load_from_fr3_data(const LevelDataVulkan* loader_data) {
  const tfrag3::Level* lev_data = loader_data->level.get();
  m_wind_vectors.clear();
  // We changed level!
  discard_tree_cache();
  for (int geo = 0; geo < 4; ++geo) {
    m_trees[geo].resize(lev_data->tie_trees[geo].size());
    unsigned max_sampler_count = 0;
    for (auto& tie_tree : lev_data->tie_trees[geo]) {
      if (tie_tree.instanced_wind_draws.size() > max_sampler_count) {
        max_sampler_count = tie_tree.instanced_wind_draws.size();
      }
    }
  }

  size_t vis_temp_len = 0;
  size_t max_draws = 0;
  size_t max_num_grps = 0;
  u16 max_wind_idx = 0;
  size_t time_of_day_count = 0;
  size_t max_inds = 0;

  u32 descriptor_set_index = 0;
  u32 wind_descriptor_set_index = 0;

  for (u32 l_geo = 0; l_geo < tfrag3::TIE_GEOS; l_geo++) {
    for (u32 l_tree = 0; l_tree < lev_data->tie_trees[l_geo].size(); l_tree++) {
      size_t wind_idx_buffer_len = 0;
      size_t num_grps = 0;
      const auto& tree = lev_data->tie_trees[l_geo][l_tree];
      max_draws = std::max(tree.static_draws.size(), max_draws);

      u32 static_draw_count = tree.static_draws.size();
      u32 wind_draw_count = tree.instanced_wind_draws.size();

      for (auto& draw : tree.static_draws) {
        num_grps += draw.vis_groups.size();
      }
      max_num_grps = std::max(max_num_grps, num_grps);
      for (auto& draw : tree.instanced_wind_draws) {
        wind_idx_buffer_len += draw.vertex_index_stream.size();
      }
      for (auto& inst : tree.wind_instance_info) {
        max_wind_idx = std::max(max_wind_idx, inst.wind_idx);
      }
      time_of_day_count = std::max(tree.colors.size(), time_of_day_count);
      max_inds = std::max(tree.unpacked.indices.size(), max_inds);
      u32 verts = tree.packed_vertices.color_indices.size();
      auto& lod_tree = m_trees.at(l_geo);

      // openGL vertex buffer from loader
      lod_tree[l_tree].vertex_buffer = loader_data->tie_data[l_geo][l_tree].vertex_buffer.get();
      // draw array from FR3 data
      lod_tree[l_tree].draws = &tree.static_draws;
      // base TOD colors from FR3
      lod_tree[l_tree].colors = &tree.colors;
      // visibility BVH from FR3
      lod_tree[l_tree].vis = &tree.bvh;
      // indices from FR3 (needed on CPU for culling)
      lod_tree[l_tree].index_data = tree.unpacked.indices.data();
      // wind metadata
      lod_tree[l_tree].instance_info = &tree.wind_instance_info;
      lod_tree[l_tree].wind_draws = &tree.instanced_wind_draws;
      // preprocess colors for faster interpolation (TODO: move to loader)
      lod_tree[l_tree].tod_cache = background_common::swizzle_time_of_day(tree.colors);
      // OpenGL index buffer (fixed index buffer for multidraw system)
      lod_tree[l_tree].index_buffer = loader_data->tie_data[l_geo][l_tree].index_buffer.get();
      lod_tree[l_tree].category_draw_indices = tree.category_draw_indices;
      lod_tree[l_tree].time_of_day_sampler_helper = std::make_unique<VulkanSamplerHelper>(m_device);
      lod_tree[l_tree].time_of_day_sampler_helper->CreateSampler();

      lod_tree[l_tree].sampler_helpers_categories.resize(
          tree.category_draw_indices[tfrag3::kNumTieCategories], m_device);

      if (wind_idx_buffer_len > 0) {
        lod_tree[l_tree].wind_matrix_cache.resize(tree.wind_instance_info.size());
        lod_tree[l_tree].has_wind = true;
        lod_tree[l_tree].wind_index_buffer =
            loader_data->tie_data[l_geo][l_tree].wind_indices.get();
        u32 off = 0;
        lod_tree[l_tree].wind_vertex_index_offsets.clear();
        for (auto& draw : tree.instanced_wind_draws) {
          lod_tree[l_tree].wind_vertex_index_offsets.push_back(off);
          off += draw.vertex_index_stream.size();
        }
      }

      lod_tree[l_tree].time_of_day_texture = std::make_unique<VulkanTexture>(m_device);
      VkExtent3D extents{background_common::TIME_OF_DAY_COLOR_COUNT, 1, 1};
      lod_tree[l_tree].time_of_day_texture->createImage(
          extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT);
      lod_tree[l_tree].time_of_day_texture->createImageView(
          VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

      lod_tree[l_tree].instanced_wind_sampler_helpers.resize(tree.instanced_wind_draws.size(),
                                                             m_device);
      lod_tree[l_tree].vis_temp.resize(tree.bvh.vis_nodes.size());

      lod_tree[l_tree].draw_idx_temp.resize(static_draw_count);
      lod_tree[l_tree].index_temp.resize(tree.unpacked.indices.size());
      lod_tree[l_tree].multidraw_idx_temp.resize(static_draw_count);
      lod_tree[l_tree].time_of_day_descriptor_image_infos.resize(static_draw_count);
      lod_tree[l_tree].descriptor_image_infos.resize(static_draw_count);

      lod_tree[l_tree].time_of_day_instanced_wind_descriptor_image_infos.resize(
          tree.instanced_wind_draws.size());
      lod_tree[l_tree].instanced_wind_descriptor_image_infos.resize(
          tree.instanced_wind_draws.size());

      // TODO: Add check to make sure tfrag3 descriptor sets are not all used
      lod_tree[l_tree].etie_base_vertex_shader_descriptor_sets = {
          m_global_etie_base_vertex_shader_descriptor_sets.begin() + descriptor_set_index,
          m_global_etie_base_vertex_shader_descriptor_sets.begin() + descriptor_set_index +
              static_draw_count};
      lod_tree[l_tree].etie_vertex_shader_descriptor_sets = {
          m_global_etie_vertex_shader_descriptor_sets.begin() + descriptor_set_index,
          m_global_etie_vertex_shader_descriptor_sets.begin() + descriptor_set_index +
              static_draw_count};
      lod_tree[l_tree].vertex_shader_descriptor_sets = {
          m_global_vertex_shader_descriptor_sets.begin() + descriptor_set_index,
          m_global_vertex_shader_descriptor_sets.begin() + descriptor_set_index +
              static_draw_count};
      lod_tree[l_tree].fragment_shader_descriptor_sets = {
          m_global_fragment_shader_descriptor_sets.begin() + descriptor_set_index,
          m_global_fragment_shader_descriptor_sets.begin() + descriptor_set_index +
              static_draw_count};

      if (!tree.instanced_wind_draws.empty()) {
        lod_tree[l_tree].instanced_wind_vertex_shader_descriptor_sets = {
            m_global_instanced_wind_vertex_shader_descriptor_sets.begin() +
                wind_descriptor_set_index,
            m_global_instanced_wind_vertex_shader_descriptor_sets.begin() +
                wind_descriptor_set_index + wind_draw_count};
        lod_tree[l_tree].instanced_wind_fragment_shader_descriptor_sets = {
            m_global_instanced_wind_fragment_shader_descriptor_sets.begin() +
                wind_descriptor_set_index,
            m_global_instanced_wind_fragment_shader_descriptor_sets.begin() +
                wind_descriptor_set_index + wind_draw_count};
      }

      descriptor_set_index += tree.static_draws.size();
      wind_descriptor_set_index += tree.instanced_wind_draws.size();
    }
  }

  m_wind_vectors.resize(4 * max_wind_idx + 4);  // 4x u32's per wind.
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  ASSERT(time_of_day_count <= background_common::TIME_OF_DAY_COLOR_COUNT);
}


void Tie3Vulkan::AllocateDescriptorSets(std::vector<VkDescriptorSet>& descriptorSets, VkDescriptorSetLayout& layout, u32 descriptorSetCount) {
  if (!descriptorSetCount) {
    return;
  }
  VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
  descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocInfo.descriptorPool = m_vulkan_info.descriptor_pool->getDescriptorPool();

  descriptorSets.resize(descriptorSetCount);
  std::vector<VkDescriptorSetLayout> fragmentDescriptorSetLayouts{descriptorSetCount, layout};
  descriptorSetAllocInfo.pSetLayouts = fragmentDescriptorSetLayouts.data();
  descriptorSetAllocInfo.descriptorSetCount = fragmentDescriptorSetLayouts.size();

  if (vkAllocateDescriptorSets(m_device->getLogicalDevice(), &descriptorSetAllocInfo,
                               descriptorSets.data())) {
    throw std::exception("Failed to allocated descriptor set in Shrub");
  }
}

/*!
 * Set up all Vulkan and temporary buffers for a given level name.
 * The level name should be the 3 character short name.
 */
bool Tie3Vulkan::try_loading_level(const std::string& level, BaseSharedRenderState* render_state) {
  // make sure we have the level data.
  Timer tfrag3_setup_timer;
  auto lev_data = m_vulkan_info.loader->get_tfrag3_level(level);
  if (!lev_data) {
    // not loaded
    m_has_level = false;
    m_textures = nullptr;
    m_level_name = "";
    discard_tree_cache();
    return false;
  }

  if (m_has_level && lev_data->load_id != m_load_id) {
    m_has_level = false;
    m_textures = nullptr;
    m_level_name = "";
    discard_tree_cache();
    return try_loading_level(level, render_state);
  }
  m_textures = &lev_data->textures_map;
  m_load_id = lev_data->load_id;

  if (m_level_name != level) {
    load_from_fr3_data(lev_data);
    m_has_level = true;
    m_level_name = level;
  } else {
    m_has_level = true;
  }

  if (tfrag3_setup_timer.getMs() > 5) {
    lg::info("TIE setup: {:.1f}ms", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}

void Tie3Vulkan::discard_tree_cache() {
  for (int geo = 0; geo < 4; ++geo) {
    m_trees[geo].clear();
  }
}

void Tie3Vulkan::render_tree_wind(int idx,
                            int geom,
                            const TfragRenderSettings& settings,
                            BaseSharedRenderState* render_state,
                            ScopedProfilerNode& prof) {
  auto& tree = m_trees.at(geom).at(idx);
  if (tree.wind_draws->empty()) {
    return;
  }

  // note: this isn't the most efficient because we might compute wind matrices for invisible
  // instances. TODO: add vis ids to the instance info to avoid this
  memset(tree.wind_matrix_cache.data(), 0, sizeof(float) * 16 * tree.wind_matrix_cache.size());
  auto& cam_bad = settings.camera;
  std::array<math::Vector4f, 4> cam;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      cam[i][j] = cam_bad.data()[i * 4 + j];
    }
  }

  for (size_t inst_id = 0; inst_id < tree.instance_info->size(); inst_id++) {
    auto& info = tree.instance_info->operator[](inst_id);
    auto& out = tree.wind_matrix_cache[inst_id];
    // auto& mat = tree.instance_info->operator[](inst_id).matrix;
    auto mat = info.matrix;

    ASSERT(info.wind_idx * 4 <= m_wind_vectors.size());
    do_wind_math(info.wind_idx, m_wind_vectors.data(), m_wind_data,
                 info.stiffness * m_wind_multiplier, mat);

    // vmulax.xyzw acc, vf20, vf10
    // vmadday.xyzw acc, vf21, vf10
    // vmaddz.xyzw vf10, vf22, vf10
    out[0] = cam[0] * mat[0].x() + cam[1] * mat[0].y() + cam[2] * mat[0].z();

    // vmulax.xyzw acc, vf20, vf11
    // vmadday.xyzw acc, vf21, vf11
    // vmaddz.xyzw vf11, vf22, vf11
    out[1] = cam[0] * mat[1].x() + cam[1] * mat[1].y() + cam[2] * mat[1].z();

    // vmulax.xyzw acc, vf20, vf12
    // vmadday.xyzw acc, vf21, vf12
    // vmaddz.xyzw vf12, vf22, vf12
    out[2] = cam[0] * mat[2].x() + cam[1] * mat[2].y() + cam[2] * mat[2].z();

    // vmulax.xyzw acc, vf20, vf13
    // vmadday.xyzw acc, vf21, vf13
    // vmaddaz.xyzw acc, vf22, vf13
    // vmaddw.xyzw vf13, vf23, vf0
    out[3] = cam[0] * mat[3].x() + cam[1] * mat[3].y() + cam[2] * mat[3].z() + cam[3];
  }

  int last_texture = -1;

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffers[] = {tree.vertex_buffer->getBuffer()};
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, tree.wind_index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

  for (size_t draw_idx = 0; draw_idx < tree.wind_draws->size(); draw_idx++) {
    const auto& draw = tree.wind_draws->at(draw_idx);

    auto& time_of_day_texture = m_textures->at(draw.tree_tex_id);
    auto& sampler_helper = tree.instanced_wind_sampler_helpers[draw_idx];

    auto double_draw = vulkan_background_common::setup_tfrag_shader(
        render_state, draw.mode,
        sampler_helper, m_pipeline_config_info,
        m_time_of_day_color_push_constant);
    
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_tie_vertex_push_constant),
                       sizeof(m_time_of_day_color_push_constant),
                       (void*)&m_time_of_day_color_push_constant);

    m_graphics_pipeline_layout.updateGraphicsPipeline(m_vulkan_info.render_command_buffer, m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    PrepareVulkanDraw(tree, time_of_day_texture.getImageView(), sampler_helper.GetSampler(),
                      tree.time_of_day_instanced_wind_descriptor_image_infos[draw_idx],
                      tree.instanced_wind_descriptor_image_infos[draw_idx],
                      tree.instanced_wind_vertex_shader_descriptor_sets[draw_idx],
                      tree.instanced_wind_fragment_shader_descriptor_sets[draw_idx],
                      m_vertex_descriptor_writer, m_fragment_descriptor_writer);

    int off = 0;
    for (auto& grp : draw.instance_groups) {
      if (!m_debug_all_visible && !tree.vis_temp.at(grp.vis_idx)) {
        off += grp.num;
        continue;  // invisible, skip.
      }

      ::memcpy(&m_tie_vertex_push_constant.camera, &tree.wind_matrix_cache.at(grp.instance_idx), sizeof(math::Matrix4f));

      prof.add_draw_call();
      prof.add_tri(grp.num);

      tree.perf.draws++;
      tree.perf.wind_draws++;

      //TODO: See if we need to update descriptor set if values of same uniform buffer has changed
      vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_tie_vertex_push_constant),
                         &m_tie_vertex_push_constant);

      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, grp.num, 1,
                       tree.wind_vertex_index_offsets.at(draw_idx), 0, 0);
      off += grp.num;

      switch (double_draw.kind) {
        case DoubleDrawKind::NONE:
          break;
        case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
          tree.perf.draws++;
          tree.perf.wind_draws++;
          prof.add_draw_call();
          prof.add_tri(grp.num);

          m_time_of_day_color_push_constant.alpha_min = -10.f;
          m_time_of_day_color_push_constant.alpha_max = double_draw.aref_second;

          vkCmdPushConstants(
              m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
              VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_tie_vertex_push_constant),
              sizeof(m_time_of_day_color_push_constant), (void*)&m_time_of_day_color_push_constant);
          //glDepthMask(GL_FALSE);
          vkCmdDrawIndexed(m_vulkan_info.render_command_buffer,
                           draw.vertex_index_stream.size(), 1,
                           0, 0, 0);
          break;
        default:
          ASSERT(false);
      }
    }
  }
}


void Tie3Vulkan::draw_matching_draws_for_tree(int idx,
                                        int geom,
                                        const TfragRenderSettings& settings,
                                        BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof,
                                        tfrag3::TieCategory category) {
  auto& tree = m_trees.at(geom).at(idx);

  // don't render if we haven't loaded
  if (!m_has_level) {
    return;
  }
  bool use_envmap = tfrag3::is_envmap_first_draw_category(category);
  if (use_envmap && m_draw_envmap_second_draw) {
    setup_shader(ShaderId::ETIE_BASE);
    vulkan_background_common::first_tfrag_draw_setup(settings, &m_tie_vertex_push_constant);
    // if we use envmap, use the envmap-style math for the base draw to avoid rounding issue.
    init_etie_cam_uniforms(render_state);
  } else {
    setup_shader(ShaderId::TFRAG3);
    vulkan_background_common::first_tfrag_draw_setup(settings, &m_tie_vertex_push_constant);
  }

  if (render_state->no_multidraw) {
    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                         tree.single_draw_index_buffer->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);
  } else {
    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                         tree.index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
  }

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;
  
  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = tree.vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  int last_texture = -1;
  for (size_t draw_idx = tree.category_draw_indices[(int)category];
       draw_idx < tree.category_draw_indices[(int)category + 1]; draw_idx++) {
    const auto& draw = tree.draws->operator[](draw_idx);
    const auto& multidraw_indices = tree.multi_draw_indexed_infos_collection[draw_idx];
    const auto& singledraw_indices = m_cache.draw_idx_temp[draw_idx];

    if (render_state->no_multidraw) {
      if (singledraw_indices.number_of_draws == 0) {
        continue;
      }
    } else {
      if (multidraw_indices.empty()) {
        continue;
      }
    }

    auto& texture = m_textures->at(draw.tree_tex_id);
    auto& time_of_day_sampler = tree.sampler_helpers_categories[draw_idx];

    auto double_draw = vulkan_background_common::setup_tfrag_shader(
        render_state, draw.mode, time_of_day_sampler, m_pipeline_config_info,
        m_time_of_day_color_push_constant);

    //Hack to make sure we don't exceed 128 push constant memory limit
    if (draw.mode.get_decal()) {
      m_tie_vertex_push_constant.settings |= 1;
    } else {
      m_tie_vertex_push_constant.settings &= ~0x1;
    }
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_tie_vertex_push_constant),
                       (void*)&m_tie_vertex_push_constant);

    VkDescriptorSet& vertex_descriptor_set = (use_envmap && m_draw_envmap_second_draw)
                                                 ? tree.etie_base_vertex_shader_descriptor_sets[draw_idx]
                                                 : tree.vertex_shader_descriptor_sets[draw_idx];
    std::unique_ptr<DescriptorWriter>& vertex_descriptor_writer =
        (use_envmap && m_draw_envmap_second_draw) ? m_etie_base_vertex_descriptor_writer
                                                  : m_vertex_descriptor_writer;

    PrepareVulkanDraw(tree, texture.getImageView(), time_of_day_sampler.GetSampler(),
                      tree.time_of_day_descriptor_image_infos[draw_idx],
                      tree.descriptor_image_infos[draw_idx],
                      vertex_descriptor_set,
                      tree.fragment_shader_descriptor_sets[draw_idx],
                      vertex_descriptor_writer, m_fragment_descriptor_writer);

    m_graphics_pipeline_layout.updateGraphicsPipeline(
        m_vulkan_info.render_command_buffer, m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    prof.add_draw_call();

    if (render_state->no_multidraw) {
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws, 1,
                       singledraw_indices.draw_index, 0, 0);
    } else {
      //Checks to see if Multi draw module is loaded otherwise run vkCmdDrawIndexed multiple times
      if (vkCmdDrawMultiIndexedEXT) {
        vkCmdDrawMultiIndexedEXT(m_vulkan_info.render_command_buffer, multidraw_indices.size(),
                                 multidraw_indices.data(), 1, 0, sizeof(VkMultiDrawIndexedInfoEXT),
                                 NULL);
      } else {
        for (auto& indexInfo : multidraw_indices) {
           vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, indexInfo.indexCount, 1,
                            indexInfo.firstIndex, 0, 0);
        }
      }
    }

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        ASSERT(false);
        m_time_of_day_color_push_constant.alpha_min = -10.f;
        m_time_of_day_color_push_constant.alpha_max = double_draw.aref_second;

        vkCmdPushConstants(m_vulkan_info.render_command_buffer,
                           m_pipeline_config_info.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(m_tie_vertex_push_constant),
                           sizeof(m_time_of_day_color_push_constant),
                           (void*)&m_time_of_day_color_push_constant);

        if (render_state->no_multidraw) {
          vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws,
                           1, singledraw_indices.draw_index, 0, 0);
        } else {
          // Checks to see if Multi draw module is loaded otherwise run vkCmdDrawIndexed multiple
          // times
          if (vkCmdDrawMultiIndexedEXT) {
            vkCmdDrawMultiIndexedEXT(m_vulkan_info.render_command_buffer, multidraw_indices.size(),
                                     multidraw_indices.data(), 1, 0,
                                     sizeof(VkMultiDrawIndexedInfoEXT), NULL);
          } else {
            for (const auto& indexInfo : multidraw_indices) {
              vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, indexInfo.indexCount, 1,
                               indexInfo.firstIndex, 0, 0);
            }
          }
        }
        break;
      default:
        ASSERT(false);
    }
  }

  if (!m_hide_wind && category == tfrag3::TieCategory::NORMAL) {
    auto wind_prof = prof.make_scoped_child("wind");
    render_tree_wind(idx, geom, settings, render_state, wind_prof);
  }

  if (use_envmap) {
    auto& vertex_write_descriptors_sets = m_vertex_descriptor_writer->getWriteDescriptorSets();
    vertex_write_descriptors_sets[0] =
        m_vertex_descriptor_writer->writeBufferDescriptorSet(0, &m_etie_vertex_buffer_descriptor_info);
    envmap_second_pass_draw(tree, settings, render_state, prof,
                            tfrag3::get_second_draw_category(category), idx, geom);
  }
}

void Tie3Vulkan::setup_tree(int idx,
                            int geom,
                            const TfragRenderSettings& settings,
                            const u8* proto_vis_data,
                            size_t proto_vis_data_size,
                            bool use_multidraw,
                            ScopedProfilerNode& prof) {
  // reset perf
  auto& tree = m_trees.at(geom).at(idx);
  // don't render if we haven't loaded
  if (!m_has_level) {
    return;
  }

  // update time of day
  if (m_color_result.size() < tree.colors->size()) {
    m_color_result.resize(tree.colors->size());
  }

#ifndef __aarch64__
  if (m_use_fast_time_of_day) {
    background_common::interp_time_of_day_fast(settings.camera.itimes, tree.tod_cache,
                                               m_color_result.data());
  } else {
    background_common::interp_time_of_day_slow(settings.camera.itimes, *tree.colors,
                                               m_color_result.data());
  }
#else
  background_common::interp_time_of_day_slow(settings.camera.itimes, *tree.colors, m_color_result.data());
#endif

  tree.time_of_day_texture->writeToImage((tfrag3::TimeOfDayColor*)tree.colors->data(),
                                         tree.colors->size() * sizeof(tfrag3::TimeOfDayColor));

  // update proto vis mask
  if (proto_vis_data) {
    tree.proto_visibility.update(proto_vis_data, proto_vis_data_size);
  }

  if (!m_debug_all_visible) {
    // need culling data
    background_common::cull_check_all_slow(settings.camera.planes, tree.vis->vis_nodes,
                                           settings.occlusion_culling, tree.vis_temp.data());
  }

  u32 num_tris = 0;
  if (use_multidraw) {
    if (m_debug_all_visible) {
      num_tris = vulkan_background_common::make_all_visible_multidraws(
          tree.multi_draw_indexed_infos_collection, *tree.draws);
    } else {
      Timer index_timer;
      if (tree.has_proto_visibility) {
        num_tris = vulkan_background_common::make_multidraws_from_vis_and_proto_string(
            tree.multi_draw_indexed_infos_collection, *tree.draws, tree.vis_temp,
            tree.proto_visibility.vis_flags);
      } else {
        num_tris = vulkan_background_common::make_multidraws_from_vis_string(
            tree.multi_draw_indexed_infos_collection, *tree.draws, tree.vis_temp);
      }
    }
  } else {
    u32 idx_buffer_size;
    if (m_debug_all_visible) {
      idx_buffer_size = vulkan_background_common::make_all_visible_index_list(
          tree.draw_idx_temp.data(), tree.index_temp.data(), *tree.draws, tree.index_data,
          &num_tris);
    } else {
      if (tree.has_proto_visibility) {
        idx_buffer_size = vulkan_background_common::make_index_list_from_vis_and_proto_string(
            tree.draw_idx_temp.data(), tree.index_temp.data(), *tree.draws, tree.vis_temp,
            tree.proto_visibility.vis_flags, tree.index_data, &num_tris);
      } else {
        idx_buffer_size = vulkan_background_common::make_index_list_from_vis_string(
            tree.draw_idx_temp.data(), tree.index_temp.data(), *tree.draws, tree.vis_temp,
            tree.index_data, &num_tris);
      }
    }

    tree.single_draw_index_buffer->writeToGpuBuffer(tree.index_temp.data(), idx_buffer_size * sizeof(u32));
  }

  prof.add_tri(num_tris);
}

void Tie3Vulkan::envmap_second_pass_draw(TreeVulkan& tree,
                                         const TfragRenderSettings& settings,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof,
                                         tfrag3::TieCategory category, int index, int geom) {
  setup_shader(ShaderId::ETIE);
  if (render_state->no_multidraw) {
    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                         tree.single_draw_index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
  } else {
    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, tree.index_buffer->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);
  }

  init_etie_cam_uniforms(render_state);
  //m_etie_vertex_push_constant.envmap_tod_tint = m_common_data.envmap_color;

  int last_texture = -1;
  for (size_t draw_idx = tree.category_draw_indices[(int)category];
       draw_idx < tree.category_draw_indices[(int)category + 1]; draw_idx++) {
    const auto& draw = tree.draws->at(draw_idx);
    const auto& multidraw_indices = tree.multi_draw_indexed_infos_collection[draw_idx];
    const auto& singledraw_indices = m_cache.draw_idx_temp[draw_idx];

    if (render_state->no_multidraw) {
      if (singledraw_indices.number_of_draws == 0) {
        continue;
      }
    } else {
      if (multidraw_indices.empty()) {
        continue;
      }
    }

    auto& texture = m_textures->at(draw.tree_tex_id);
    auto& time_of_day_sampler = tree.sampler_helpers_categories[draw_idx];

    VkDescriptorImageInfo& tree_time_of_day_descriptor_image_info = tree.time_of_day_descriptor_image_infos[index];
    VkDescriptorSet& vertex_descriptor_set = tree.etie_vertex_shader_descriptor_sets[index];

    std::vector<VkDescriptorSet> descriptor_sets{vertex_descriptor_set,
                                                 tree.fragment_shader_descriptor_sets[index]};

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, descriptor_sets.size(),
                            descriptor_sets.data(), 0, NULL);

    m_graphics_pipeline_layout.updateGraphicsPipeline(
        m_vulkan_info.render_command_buffer, m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    prof.add_draw_call();

    if (render_state->no_multidraw) {
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws, 1,
                       singledraw_indices.draw_index, 0, 0);
    } else {
      // Checks to see if Multi draw module is loaded otherwise run vkCmdDrawIndexed multiple times
      if (vkCmdDrawMultiIndexedEXT) {
        vkCmdDrawMultiIndexedEXT(m_vulkan_info.render_command_buffer, multidraw_indices.size(),
                                 multidraw_indices.data(), 1, 0, sizeof(VkMultiDrawIndexedInfoEXT),
                                 NULL);
      } else {
        for (const auto& indexInfo : multidraw_indices) {
          vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, indexInfo.indexCount, 1,
                           indexInfo.firstIndex, 0, 0);
        }
      }
    }
  }
}

void Tie3Vulkan::init_etie_cam_uniforms(const BaseSharedRenderState* render_state) {
  m_etie_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "camera_no_presp", 1, VK_FALSE, (float*)render_state->camera_no_persp[0].data());
  m_etie_base_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "camera_no_presp", 1, VK_FALSE, (float*)render_state->camera_no_persp[0].data());

  math::Vector4f perspective[2] = {};
  float inv_fog = 1.f / render_state->camera_fog[0];
  auto& hvdf_off = render_state->camera_hvdf_off;
  float pxx = render_state->camera_planes[0].x();
  float pyy = render_state->camera_planes[1].y();
  float pzz = render_state->camera_planes[2].z();
  float pzw = render_state->camera_planes[2].w();
  float pwz = render_state->camera_planes[3].z();
  float scale = pzw * inv_fog;
  perspective[0].x() = scale * hvdf_off.x();
  perspective[0].y() = scale * hvdf_off.y();
  perspective[0].z() = scale * hvdf_off.z() + pzz;
  perspective[0].w() = scale;

  perspective[1].x() = pxx;
  perspective[1].y() = pyy;
  perspective[1].z() = pwz;
  perspective[1].w() = 0;

  m_etie_vertex_shader_uniform_buffer->SetUniformMathVector4f("perspective0", perspective[0]);
  m_etie_vertex_shader_uniform_buffer->SetUniformMathVector4f("perspective1", perspective[1]);

  m_etie_base_vertex_shader_uniform_buffer->SetUniformMathVector4f("perspective0", perspective[0]);
  m_etie_base_vertex_shader_uniform_buffer->SetUniformMathVector4f("perspective1", perspective[1]);

  m_etie_base_vertex_shader_uniform_buffer->map();
  m_etie_base_vertex_shader_uniform_buffer->flush();
  m_etie_base_vertex_shader_uniform_buffer->unmap();

  m_etie_vertex_shader_uniform_buffer->map();
  m_etie_vertex_shader_uniform_buffer->flush();
  m_etie_vertex_shader_uniform_buffer->unmap();
}

void Tie3Vulkan::setup_shader(ShaderId id) {
  auto& shader = m_vulkan_info.shaders[id];

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

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

  if (id == ShaderId::ETIE_BASE) {
    m_pipeline_config_info.pipelineLayout = m_etie_base_pipeline_layout;
    m_pipeline_config_info.attributeDescriptions = {tfrag_attribute_descriptions.begin(),
                                                    tfrag_attribute_descriptions.end()};
  } else if (id == ShaderId::ETIE) {
    m_pipeline_config_info.pipelineLayout = m_etie_pipeline_layout;
    m_pipeline_config_info.attributeDescriptions = {etie_attribute_descriptions.begin(),
                                                    etie_attribute_descriptions.end()};
  } else {
    m_pipeline_config_info.pipelineLayout = m_tie_pipeline_layout;
    m_pipeline_config_info.attributeDescriptions = {tfrag_attribute_descriptions.begin(),
                                                    tfrag_attribute_descriptions.end()};
  }
}

void Tie3Vulkan::InitializeInputAttributes() {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::PreloadedVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions = {bindingDescription};

  tfrag_attribute_descriptions[0].binding = 0;
  tfrag_attribute_descriptions[0].location = 0;
  tfrag_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  tfrag_attribute_descriptions[0].offset = offsetof(tfrag3::PreloadedVertex, x);

  tfrag_attribute_descriptions[1].binding = 0;
  tfrag_attribute_descriptions[1].location = 1;
  tfrag_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  tfrag_attribute_descriptions[1].offset = offsetof(tfrag3::PreloadedVertex, s);

  tfrag_attribute_descriptions[2].binding = 0;
  tfrag_attribute_descriptions[2].location = 2;
  tfrag_attribute_descriptions[2].format = VK_FORMAT_R16G16_SINT;
  tfrag_attribute_descriptions[2].offset = offsetof(tfrag3::PreloadedVertex, color_index);
  m_pipeline_config_info.attributeDescriptions = {tfrag_attribute_descriptions.begin(),
                                                  tfrag_attribute_descriptions.end()};

  etie_attribute_descriptions[0].binding = 0;
  etie_attribute_descriptions[0].location = 0;
  etie_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  etie_attribute_descriptions[0].offset = offsetof(tfrag3::PreloadedVertex, x);

  etie_attribute_descriptions[1].binding = 0;
  etie_attribute_descriptions[1].location = 1;
  etie_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  etie_attribute_descriptions[1].offset = offsetof(tfrag3::PreloadedVertex, s);

  etie_attribute_descriptions[2].binding = 0;
  etie_attribute_descriptions[2].location = 2;
  etie_attribute_descriptions[2].format = VK_FORMAT_R16G16_SINT;
  etie_attribute_descriptions[2].offset = offsetof(tfrag3::PreloadedVertex, color_index);

  etie_attribute_descriptions[3].binding = 0;
  etie_attribute_descriptions[3].location = 3;
  etie_attribute_descriptions[3].format = VK_FORMAT_A2B10G10R10_SNORM_PACK32;
  etie_attribute_descriptions[3].offset = offsetof(tfrag3::PreloadedVertex, nor);

  etie_attribute_descriptions[4].binding = 0;
  etie_attribute_descriptions[4].location = 4;
  etie_attribute_descriptions[4].format = VK_FORMAT_R8G8B8A8_UNORM;
  etie_attribute_descriptions[4].offset = offsetof(tfrag3::PreloadedVertex, r);
}

void Tie3Vulkan::PrepareVulkanDraw(TreeVulkan& tree,
                                   VkImageView textureImageView,
                                   VkSampler sampler,
                                   VkDescriptorImageInfo& time_of_day_descriptor_info,
                                   VkDescriptorImageInfo& descriptor_info,
                                   VkDescriptorSet vertex_descriptor_set,
                                   VkDescriptorSet fragment_descriptor_set,
                                   std::unique_ptr<DescriptorWriter>& vertex_descriptor_writer,
                                   std::unique_ptr<DescriptorWriter>& fragment_descriptor_writer) {
  time_of_day_descriptor_info = VkDescriptorImageInfo{
      tree.time_of_day_sampler_helper->GetSampler(), tree.time_of_day_texture->getImageView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  descriptor_info = VkDescriptorImageInfo{sampler, textureImageView,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  auto& vertex_write_descriptors_sets = vertex_descriptor_writer->getWriteDescriptorSets();
  vertex_write_descriptors_sets[0] =
      vertex_descriptor_writer->writeImageDescriptorSet(0, &time_of_day_descriptor_info);

  auto& fragment_write_descriptors_sets = fragment_descriptor_writer->getWriteDescriptorSets();
  fragment_write_descriptors_sets[0] =
      fragment_descriptor_writer->writeImageDescriptorSet(0, &descriptor_info);

  vertex_descriptor_writer->overwrite(vertex_descriptor_set);
  fragment_descriptor_writer->overwrite(fragment_descriptor_set);

  std::vector<VkDescriptorSet> descriptor_sets{vertex_descriptor_set, fragment_descriptor_set};

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, descriptor_sets.size(),
                          descriptor_sets.data(), 0, NULL);
}

Tie3VulkanAnotherCategory::Tie3VulkanAnotherCategory(const std::string& name,
                            int my_id,
                            std::shared_ptr<GraphicsDeviceVulkan> device,
                            VulkanInitializationInfo& vulkan_info,
                            Tie3Vulkan* parent,
                            tfrag3::TieCategory category)
    : BaseBucketRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info), m_parent(parent), m_category(category) {
}

void Tie3VulkanAnotherCategory::draw_debug_window() {
  ImGui::Text("Child of this renderer:");
  m_parent->draw_debug_window();
}

void Tie3VulkanAnotherCategory::render(DmaFollower& dma,
                                       SharedVulkanRenderState* render_state,
                                       ScopedProfilerNode& prof) {
  render(dma, reinterpret_cast<BaseSharedRenderState*>(render_state), prof);
}

void Tie3VulkanAnotherCategory::render(DmaFollower& dma,
                                 BaseSharedRenderState* render_state,
                                 ScopedProfilerNode& prof) {
  auto first_tag = dma.current_tag();
  dma.read_and_advance();
  if (first_tag.kind != DmaTag::Kind::CNT || first_tag.qwc != 0) {
    fmt::print("Bucket renderer {} ({}) was supposed to be empty, but wasn't\n", m_my_id, m_name);
    ASSERT(false);
  }
  m_parent->render_from_another(render_state, prof, m_category);
}

Tie3VulkanWithEnvmapJak1::Tie3VulkanWithEnvmapJak1(const std::string& name,
                       int my_id,
                       std::shared_ptr<GraphicsDeviceVulkan> device,
                       VulkanInitializationInfo& vulkan_info,
                       int level_id) : Tie3Vulkan(name, my_id, device, vulkan_info, level_id, tfrag3::TieCategory::NORMAL) {
}
void Tie3VulkanWithEnvmapJak1::render(DmaFollower& dma,
                                      SharedVulkanRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();
  setup_shader(ShaderId::TFRAG3);
  BaseTie3::render(dma, render_state, prof);
  if (m_enable_envmap) {
    render_from_another(render_state, prof, tfrag3::TieCategory::NORMAL_ENVMAP);
  }
}

void Tie3VulkanWithEnvmapJak1::draw_debug_window() {
  ImGui::Checkbox("envmap", &m_enable_envmap);
  BaseTie3::draw_debug_window();
}
