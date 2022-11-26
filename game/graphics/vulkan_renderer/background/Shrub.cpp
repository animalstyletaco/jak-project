#include "Shrub.h"

ShrubVulkan::ShrubVulkan(const std::string& name,
             int my_id,
             std::unique_ptr<GraphicsDeviceVulkan>& device,
             VulkanInitializationInfo& vulkan_info)
    : BaseShrub(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  m_vertex_shader_uniform_buffer = std::make_unique<BackgroundCommonVertexUniformBuffer>(
      m_device, TIME_OF_DAY_COLOR_COUNT, 1);
  m_time_of_day_color_buffer = std::make_unique<BackgroundCommonFragmentUniformBuffer>(
      m_device, TIME_OF_DAY_COLOR_COUNT, 1);

  auto vertex_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);
}

ShrubVulkan::~ShrubVulkan() {
  discard_tree_cache();
}

void ShrubVulkan::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  BaseShrub::render(dma, render_state, prof);
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
    m_trees[l_tree].vert_count = verts;
    m_trees[l_tree].draws = &tree.static_draws;
    m_trees[l_tree].colors = &tree.time_of_day_colors;
    m_trees[l_tree].index_data = tree.indices.data();
    m_trees[l_tree].tod_cache = background_common::swizzle_time_of_day(tree.time_of_day_colors);

    total_shrub_vertices.insert(total_shrub_vertices.end(), tree.unpacked.vertices.begin(),
                                tree.unpacked.vertices.end());
    total_shrub_indices.insert(total_shrub_indices.end(), tree.indices.begin(), tree.indices.end());
  }

  //TODO: Set up index buffer and vertex buffer here
  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, sizeof(tfrag3::ShrubGpuVertex), total_shrub_vertices.size());
  m_index_buffer = std::make_unique<IndexBuffer>(m_device, sizeof(u32), total_shrub_indices.size());
  m_single_draw_index_buffer =
      std::make_unique<IndexBuffer>(m_device, sizeof(u32), total_shrub_indices.size());

  m_vertex_buffer->writeToGpuBuffer(total_shrub_vertices.data());
  m_index_buffer->writeToGpuBuffer(total_shrub_indices.data());
  m_single_draw_index_buffer->writeToGpuBuffer(total_shrub_indices.data());

  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multidraw_count_buffer.resize(max_num_grps);
  m_cache.multidraw_index_offset_buffer.resize(max_num_grps);
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  ASSERT(time_of_day_count <= TIME_OF_DAY_COLOR_COUNT);
}

bool ShrubVulkan::setup_for_level(const std::string& level, BaseSharedRenderState* render_state) {
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
  m_textures = &lev_data->textures;
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

void ShrubVulkan::InitializeVertexBuffer() {
  auto& shader = m_vulkan_info.shaders[ShaderId::SHRUB];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Shrub Vertex Shader";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Shrub Fragment Shader";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::ShrubGpuVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::ShrubGpuVertex, x);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::ShrubGpuVertex, s);

  //FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R4G4_UNORM_PACK8;
  attributeDescriptions[2].offset = offsetof(tfrag3::ShrubGpuVertex, rgba_base);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
  attributeDescriptions[3].offset = offsetof(tfrag3::ShrubGpuVertex, color_index);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void ShrubVulkan::discard_tree_cache() {
  m_vertex_buffer.reset(nullptr);
  m_index_buffer.reset(nullptr);
  m_single_draw_index_buffer.reset(nullptr);

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

  VulkanTexture texture{m_device};

  //FIXME: Combine these APIs into one
  //CreateTextureImage(m_color_result);
  VkExtent3D extents{tree.colors->size(), 1, 1};
  texture.createImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  texture.createImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

  texture.writeToImage(m_color_result.data());

  //FIXME: Needs to be part of VkSampler
  //CreateTextureSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, 1);

  background_common::first_tfrag_draw_setup(settings, render_state,
                                                        m_vertex_shader_uniform_buffer);

  //FIXME: Needs to be part of pipeline
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  tree.perf.tod_time.add(setup_timer.getSeconds());

  int last_texture = -1;

  tree.perf.cull_time.add(0);
  Timer index_timer;
  if (render_state->no_multidraw) {
    background_common::make_all_visible_index_list(m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, tree.index_data);
  } else {
    background_common::make_all_visible_multidraws(m_cache.multidraw_offset_per_stripdraw.data(),
                                m_cache.multidraw_count_buffer.data(),
                                m_cache.multidraw_index_offset_buffer.data(), *tree.draws);
  }

  tree.perf.index_time.add(index_timer.getSeconds());

  Timer draw_timer;

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

    auto double_draw = background_common::setup_tfrag_shader(render_state, draw.mode, &m_textures->at(draw.tree_tex_id),
                                          m_pipeline_config_info, m_time_of_day_color_buffer);

    prof.add_draw_call();
    prof.add_tri(draw.num_triangles);

    tree.perf.draws++;

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        tree.perf.draws++;
        prof.add_draw_call();
        m_time_of_day_color_buffer->SetUniform1f("alpha_min", -10.f);
        m_time_of_day_color_buffer->SetUniform1f("alpha_max", double_draw.aref_second);
        //TODO: Set real index buffers here
        //glDepthMask(GL_FALSE);
        //if (render_state->no_multidraw) {
        //  glDrawElements(GL_TRIANGLE_STRIP, singledraw_indices.second, GL_UNSIGNED_INT,
        //                 (void*)(singledraw_indices.first * sizeof(u32)));
        //} else {
        //  glMultiDrawElements(
        //      GL_TRIANGLE_STRIP, &m_cache.multidraw_count_buffer[multidraw_indices.first],
        //      GL_UNSIGNED_INT, &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
        //      multidraw_indices.second);
        //}
        break;
      default:
        ASSERT(false);
    }
  }

  //TODO: Double check that VK_EXT_multi_draw is in glad2
  //TODO: Look up VkCmdDrawIndexed and vkCmdDrawMultiIndexedEXT

  tree.perf.draw_time.add(draw_timer.getSeconds());
  tree.perf.tree_time.add(tree_timer.getSeconds());
}

