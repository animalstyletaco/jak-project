#include "Shrub.h"

Shrub::Shrub(const std::string& name, BucketId my_id, VkDevice& device) : BucketRenderer(name, my_id, device) {
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);

  VkDeviceSize device_size = sizeof(m_color_result[0]) * TIME_OF_DAY_COLOR_COUNT;
  time_of_day_color_buffer = UniformBuffer(device, device_size);
}

Shrub::~Shrub() {
  discard_tree_cache();
}

void Shrub::render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) {
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.size_bytes == 0);

  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }

  auto pc_port_data = dma.read_and_advance();
  ASSERT(pc_port_data.size_bytes == sizeof(TfragPcPortData));
  memcpy(&m_pc_port_data, pc_port_data.data, sizeof(TfragPcPortData));
  m_pc_port_data.level_name[11] = '\0';

  while (dma.current_tag_offset() != render_state->next_bucket) {
    dma.read_and_advance();
  }

  TfragRenderSettings settings;
  settings.hvdf_offset = m_pc_port_data.hvdf_off;
  settings.fog = m_pc_port_data.fog;

  memcpy(settings.math_camera.data(), m_pc_port_data.camera[0].data(), 64);
  settings.tree_idx = 0;

  for (int i = 0; i < 8; i++) {
    settings.time_of_day_weights[i] =
        2 * (0xff & m_pc_port_data.itimes[i / 2].data()[2 * (i % 2)]) / 127.f;
  }

  update_render_state_from_pc_settings(render_state, m_pc_port_data);

  for (int i = 0; i < 4; i++) {
    settings.planes[i] = m_pc_port_data.planes[i];
  }

  m_has_level = setup_for_level(m_pc_port_data.level_name, render_state);
  render_all_trees(settings, render_state, prof);
}

void Shrub::update_load(const LevelData* loader_data) {
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

    m_trees[l_tree].vertex_buffer = loader_data->shrub_vertex_data[l_tree];
    m_trees[l_tree].vert_count = verts;
    m_trees[l_tree].draws = &tree.static_draws;
    m_trees[l_tree].colors = &tree.time_of_day_colors;
    m_trees[l_tree].index_data = tree.indices.data();
    m_trees[l_tree].tod_cache = swizzle_time_of_day(tree.time_of_day_colors);

    total_shrub_vertices.insert(total_shrub_vertices.end(), tree.unpacked.vertices.begin(),
                                tree.unpacked.vertices.end());
    total_shrub_indices.insert(total_shrub_indices.end(), tree.indices.begin(), tree.indices.end());
  }

  //TODO: Set up index buffer and vertex buffer here

  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multidraw_count_buffer.resize(max_num_grps);
  m_cache.multidraw_index_offset_buffer.resize(max_num_grps);
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  ASSERT(time_of_day_count <= TIME_OF_DAY_COLOR_COUNT);
}

void Shrub::InitializeVertexBuffer(SharedRenderState* render_state) {
  auto& shader = render_state->shaders[ShaderId::SHRUB];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Vertex Fragment";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Shrub Fragment";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  //FIXME: Added necessary configuration back to shrub pipeline
  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;

  //if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
  //                              &graphicsPipeline) != VK_SUCCESS) {
  //  throw std::runtime_error("failed to create graphics pipeline!");
  //}

  //TODO: Should shaders be deleted now?
}

bool Shrub::setup_for_level(const std::string& level, SharedRenderState* render_state) {
  // make sure we have the level data.
  Timer tfrag3_setup_timer;
  auto lev_data = render_state->loader->get_tfrag3_level(level);
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
    fmt::print("Shrub setup: {:.1f}ms\n", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}

void Shrub::discard_tree_cache() {
  for (auto& tree : m_trees) {
    //glBindTexture(GL_TEXTURE_1D, tree.time_of_day_texture);
    //glDeleteTextures(1, &tree.time_of_day_texture);
    //glDeleteBuffers(1, &tree.index_buffer);
    //glDeleteBuffers(1, &tree.single_draw_index_buffer);
    //glDeleteVertexArrays(1, &tree.vao);
  }

  m_trees.clear();
}

void Shrub::render_all_trees(const TfragRenderSettings& settings,
                             SharedRenderState* render_state,
                             ScopedProfilerNode& prof) {
  for (u32 i = 0; i < m_trees.size(); i++) {
    render_tree(i, settings, render_state, prof);
  }
}

void Shrub::render_tree(int idx,
                        const TfragRenderSettings& settings,
                        SharedRenderState* render_state,
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
  interp_time_of_day_fast(settings.time_of_day_weights, tree.tod_cache, m_color_result.data());
  tree.perf.tod_time.add(interp_timer.getSeconds());

  Timer setup_timer;

  VkImage texture_image;
  VkDeviceMemory texture_image_memory;

  //FIXME: Combine these APIs into one
  //CreateTextureImage(m_color_result);
  //CreateImage(_width, _height, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
  //            VK_IMAGE_TILING_OPTIMAL,
  //            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
  //            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture_image,
  //            texture_image_memory);

  VkImageView texture_image_view =
      CreateImageView(texture_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_ASPECT_COLOR_BIT, 1);

  //texture_image_views.push_back(texture_image_view);

  //FIXME: Needs to be part of VkSampler
  CreateTextureSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, 1);

  first_tfrag_draw_setup(settings, render_state, time_of_day_color_buffer);

  //FIXME: Needs to be part of pipeline
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_TRUE;

  tree.perf.tod_time.add(setup_timer.getSeconds());

  int last_texture = -1;

  tree.perf.cull_time.add(0);
  Timer index_timer;
  if (render_state->no_multidraw) {
    make_all_visible_index_list(m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, tree.index_data);
  } else {
    make_all_visible_multidraws(m_cache.multidraw_offset_per_stripdraw.data(),
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

    auto double_draw = setup_tfrag_shader(render_state, draw.mode, m_textures->at(draw.tree_tex_id),
                                          time_of_day_color_buffer);

    prof.add_draw_call();
    prof.add_tri(draw.num_triangles);

    tree.perf.draws++;

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        tree.perf.draws++;
        prof.add_draw_call();
        m_uniform_buffer.SetUniform1f("alpha_min", -10.f);
        m_uniform_buffer.SetUniform1f("alpha_max", double_draw.aref_second);
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

void Shrub::draw_debug_window() {}
