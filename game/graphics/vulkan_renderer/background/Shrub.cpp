#include "Shrub.h"

ShrubVulkan::ShrubVulkan(const std::string& name,
             int my_id,
             std::unique_ptr<GraphicsDeviceVulkan>& device,
             VulkanInitializationInfo& vulkan_info)
    : BaseShrub(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  m_shrub_push_constant.height_scale = m_push_constant.height_scale;
  m_shrub_push_constant.scissor_adjust = m_push_constant.scissor_adjust;

  m_color_result.resize(background_common::TIME_OF_DAY_COLOR_COUNT);

  m_time_of_day_descriptor_image_infos.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
  m_shrub_descriptor_image_infos.resize(background_common::TIME_OF_DAY_COLOR_COUNT);

  m_vertex_shader_uniform_buffer =
      std::make_unique<BackgroundCommonVertexUniformBuffer>(device, background_common::TIME_OF_DAY_COLOR_COUNT, 1);

  m_time_of_day_samplers.resize(background_common::TIME_OF_DAY_COLOR_COUNT, {m_device});

  InitializeShaders();
  InitializeVertexDescriptions();

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  create_pipeline_layout();
  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_vertex_buffer_descriptor_info =
      VkDescriptorBufferInfo{m_vertex_shader_uniform_buffer->getBuffer(), 0,
                             m_vertex_shader_uniform_buffer->getAlignmentSize()};

  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .writeImage(1, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  m_fragment_descriptor_writer->writeImage(0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  auto descriptorSetLayout = m_fragment_descriptor_layout->getDescriptorSetLayout();
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      background_common::TIME_OF_DAY_COLOR_COUNT, descriptorSetLayout};

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_vulkan_info.descriptor_pool->getDescriptorPool();
  allocInfo.pSetLayouts = descriptorSetLayouts.data();
  allocInfo.descriptorSetCount = descriptorSetLayouts.size();

  m_graphics_pipeline_layouts.resize(background_common::TIME_OF_DAY_COLOR_COUNT, {m_device});

  m_shrub_descriptor_sets.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
  m_time_of_day_descriptor_sets.resize(background_common::TIME_OF_DAY_COLOR_COUNT);

  if (vkAllocateDescriptorSets(m_device->getLogicalDevice(), &allocInfo,
                               m_shrub_descriptor_sets.data())) {
    throw std::exception("Failed to allocated descriptor set in Shrub");
  }
  if (vkAllocateDescriptorSets(m_device->getLogicalDevice(), &allocInfo,
                               m_time_of_day_descriptor_sets.data())) {
    throw std::exception("Failed to allocated descriptor set in Shrub");
  }
}

ShrubVulkan::~ShrubVulkan() {
  discard_tree_cache();
  vkFreeDescriptorSets(m_device->getLogicalDevice(), m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_time_of_day_descriptor_sets.size(), m_time_of_day_descriptor_sets.data());
  vkFreeDescriptorSets(m_device->getLogicalDevice(),
                       m_vulkan_info.descriptor_pool->getDescriptorPool(),
                       m_shrub_descriptor_sets.size(), m_shrub_descriptor_sets.data());
}

void ShrubVulkan::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseShrub::render(dma, render_state, prof);
}

void ShrubVulkan::create_pipeline_layout() {
  // If push constants are needed put them here
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{m_vertex_descriptor_layout->getDescriptorSetLayout(),
                                                          m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  VkPushConstantRange pushConstantRange;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(m_shrub_push_constant);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  pipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}


void ShrubVulkan::update_load(const LevelDataVulkan* loader_data) {
  const tfrag3::Level* lev_data = loader_data->level.get();
  // We changed level!
  discard_tree_cache();
  m_trees.resize(lev_data->shrub_trees.size());

  size_t max_draws = 0;
  size_t time_of_day_count = 0;
  size_t max_num_grps = 0;
  size_t max_inds = 0;

  std::vector<tfrag3::ShrubGpuVertex> total_shrub_vertices;
  std::vector<unsigned> total_shrub_indices;

  for (u32 l_tree = 0; l_tree < lev_data->shrub_trees.size(); l_tree++) {
    size_t num_grps = 0;

    const auto& tree = lev_data->shrub_trees[l_tree];
    max_draws = std::max(tree.static_draws.size(), max_draws);
    for (auto& draw : tree.static_draws) {
      (void)draw;
      // num_grps += draw.vis_groups.size(); TODO
      max_num_grps += 1;
    }
    max_num_grps = std::max(max_num_grps, num_grps);

    time_of_day_count = std::max(tree.time_of_day_colors.size(), time_of_day_count);
    max_inds = std::max(tree.indices.size(), max_inds);
    u32 verts = tree.unpacked.vertices.size();

    //m_trees[l_tree].vertex_buffer = loader_data->shrub_vertex_data[l_tree];
    m_trees[l_tree].time_of_day_texture = std::make_unique<VulkanTexture>(m_device);
    m_trees[l_tree].time_of_day_texture->createImage(
        {background_common::TIME_OF_DAY_COLOR_COUNT, 1, 1}, 1, VK_IMAGE_TYPE_1D,
        VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT);

    m_trees[l_tree].time_of_day_texture->createImageView(
        VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    m_trees[l_tree].vert_count = verts;
    m_trees[l_tree].vertex_buffer = std::make_unique<VertexBuffer>(
        m_device, sizeof(tfrag3::ShrubGpuVertex), tree.unpacked.vertices.size());
    m_trees[l_tree].vertex_buffer->writeToGpuBuffer(
        (tfrag3::ShrubGpuVertex*)tree.unpacked.vertices.data());

    m_trees[l_tree].index_buffer = std::make_unique<IndexBuffer>(m_device, sizeof(u32), tree.indices.size());
    m_trees[l_tree].index_buffer->writeToGpuBuffer((u32*)tree.indices.data());

    m_trees[l_tree].single_draw_index_buffer =
    std::make_unique<IndexBuffer>(m_device, sizeof(u32), tree.indices.size());
    m_trees[l_tree].single_draw_index_buffer->writeToGpuBuffer((u32*)tree.indices.data());

    m_trees[l_tree].draws = &tree.static_draws;
    m_trees[l_tree].colors = &tree.time_of_day_colors;
    m_trees[l_tree].index_data = tree.indices.data();
    m_trees[l_tree].tod_cache = background_common::swizzle_time_of_day(tree.time_of_day_colors);

    total_shrub_vertices.insert(total_shrub_vertices.end(), tree.unpacked.vertices.begin(),
                                tree.unpacked.vertices.end());
    total_shrub_indices.insert(total_shrub_indices.end(), tree.indices.begin(), tree.indices.end());
  }

  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multi_draw_indexed_infos.resize(max_draws);
  ASSERT(time_of_day_count <= background_common::TIME_OF_DAY_COLOR_COUNT);
}

bool ShrubVulkan::setup_for_level(const std::string& level, BaseSharedRenderState* render_state) {
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
    return setup_for_level(level, render_state);
  }
  m_textures = &lev_data->textures_map;
  m_load_id = lev_data->load_id;

  if (m_level_name != level) {
    update_load(lev_data);
    m_has_level = true;
    m_level_name = level;
  } else {
    m_has_level = true;
  }

  if (tfrag3_setup_timer.getMs() > 5) {
    lg::info("Shrub setup: {:.1f}ms", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}

void ShrubVulkan::InitializeShaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::SHRUB];

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
}

void ShrubVulkan::InitializeVertexDescriptions() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::ShrubGpuVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::ShrubGpuVertex, x);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::ShrubGpuVertex, s);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R16_SINT;
  attributeDescriptions[2].offset = offsetof(tfrag3::ShrubGpuVertex, color_index);

  //FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8G8B8_UNORM;
  attributeDescriptions[3].offset = offsetof(tfrag3::ShrubGpuVertex, rgba_base);

  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void ShrubVulkan::discard_tree_cache() {
  m_trees.clear();
}

void ShrubVulkan::render_all_trees(const TfragRenderSettings& settings,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof) {
  for (u32 i = 0; i < m_trees.size(); i++) {
    render_tree(i, settings, render_state, prof);
  }
}

void ShrubVulkan::render_tree(int idx,
                        const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof) {
  Timer tree_timer;
  auto& tree = m_trees.at(idx);
  tree.perf.draws = 0;
  tree.perf.wind_draws = 0;
  if (!m_has_level) {
    return;
  }

  if (m_color_result.size() < tree.colors->size()) {
    m_color_result.resize(tree.colors->size());
  }

  Timer interp_timer;
  background_common::interp_time_of_day_fast(settings.itimes, tree.tod_cache, m_color_result.data());
  tree.perf.tod_time.add(interp_timer.getSeconds());

  Timer setup_timer;

  tree.time_of_day_texture->writeToImage(
      m_color_result.data(), sizeof(m_color_result[0]) * m_color_result.size());

  vulkan_background_common::first_tfrag_draw_setup(settings, render_state,
                                            m_vertex_shader_uniform_buffer.get());

  tree.perf.tod_time.add(setup_timer.getSeconds());
  tree.perf.cull_time.add(0);

  Timer index_timer;
  if (render_state->no_multidraw) {
    vulkan_background_common::make_all_visible_index_list(m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, tree.index_data);
  } else {
    vulkan_background_common::make_all_visible_multidraws(m_cache.multi_draw_indexed_infos,
                                                          *tree.draws);
  }

  tree.perf.index_time.add(index_timer.getSeconds());

  Timer draw_timer;

  m_graphics_pipeline_layouts[idx].createGraphicsPipeline(m_pipeline_config_info);
  m_graphics_pipeline_layouts[idx].bind(m_vulkan_info.render_command_buffer);

  PrepareVulkanDraw(idx);

  for (size_t draw_idx = 0; draw_idx < tree.draws->size(); draw_idx++) {
    const auto& draw = tree.draws->at(draw_idx);
    const auto& multidraw_indices = m_cache.multidraw_offset_per_stripdraw[draw_idx];
    const auto& singledraw_indices = m_cache.draw_idx_temp[draw_idx];

    if (render_state->no_multidraw) {
      if (singledraw_indices.number_of_draws == 0) {
        vkCmdDrawMultiIndexedEXT(m_vulkan_info.render_command_buffer, multidraw_indices.number_of_draws,
                                 m_cache.multi_draw_indexed_infos.data(),
                                 1, 0, sizeof(VkMultiDrawIndexedInfoEXT), NULL);
      }
    } else {
      if (multidraw_indices.number_of_draws == 0) {
        vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws, 1,
                         singledraw_indices.draw_index, 0, 0);
      }
    }
    auto& time_of_day_texture = m_textures->at(draw.tree_tex_id);
    auto& time_of_day_sampler = m_time_of_day_samplers.at(draw.tree_tex_id);

    auto double_draw = vulkan_background_common::setup_tfrag_shader(render_state, draw.mode,
        time_of_day_sampler, m_pipeline_config_info,
        m_time_of_day_push_constant);

    prof.add_draw_call();
    prof.add_tri(draw.num_triangles);

    tree.perf.draws++;

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        tree.perf.draws++;
        prof.add_draw_call();
        m_time_of_day_push_constant.alpha_min = -10.f;
        m_time_of_day_push_constant.alpha_max = double_draw.aref_second;

        vkCmdPushConstants(m_vulkan_info.render_command_buffer,
                           m_pipeline_config_info.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(math::Vector4f), sizeof(m_time_of_day_push_constant),
                           (void*)&m_time_of_day_push_constant);

        if (render_state->no_multidraw) {
          vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws,
                           1, singledraw_indices.draw_index, 0, 0);

        } else {
          vkCmdDrawMultiIndexedEXT(m_vulkan_info.render_command_buffer, multidraw_indices.number_of_draws,
                                   m_cache.multi_draw_indexed_infos.data(), 1, 0,
                                   sizeof(VkMultiDrawIndexedInfoEXT), NULL);
        }
        break;
      default:
        ASSERT(false);
    }
  }

  tree.perf.draw_time.add(draw_timer.getSeconds());
  tree.perf.tree_time.add(tree_timer.getSeconds());
}

void ShrubVulkan::PrepareVulkanDraw(int index) {
  auto& vertex_write_descriptors_sets = m_vertex_descriptor_writer->getWriteDescriptorSets();
  vertex_write_descriptors_sets[1] = m_fragment_descriptor_writer->writeImageDescriptorSet(
      1, &m_time_of_day_descriptor_image_infos.at(index), 1);

  auto& fragment_write_descriptors_sets =
      m_fragment_descriptor_writer->getWriteDescriptorSets();

  fragment_write_descriptors_sets[1] =
      m_fragment_descriptor_writer->writeImageDescriptorSet(
          1, &m_shrub_descriptor_image_infos.at(index), 1);

  m_vertex_descriptor_writer->overwrite(m_time_of_day_descriptor_sets[index]);
  m_fragment_descriptor_writer->overwrite(m_shrub_descriptor_sets[index]);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = m_trees[index].vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                       m_trees[index].index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_shrub_push_constant),
                     (void*)&m_shrub_push_constant);

  std::vector<VkDescriptorSet> descriptor_sets{m_time_of_day_descriptor_sets[index],
                                               m_shrub_descriptor_sets[index]};

  uint32_t dynamicDescriptorOffset = index * m_vertex_shader_uniform_buffer->getAlignmentSize();

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, descriptor_sets.size(),
                          descriptor_sets.data(), 1,
                          &dynamicDescriptorOffset);
}
