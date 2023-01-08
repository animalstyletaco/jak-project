#include "Tie3.h"

#include "third-party/imgui/imgui.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

Tie3Vulkan::Tie3Vulkan(const std::string& name,
                       int my_id,
                       std::unique_ptr<GraphicsDeviceVulkan>& device,
                       VulkanInitializationInfo& vulkan_info,
                       int level_id)
    : BaseTie3(name, my_id, level_id),
      BucketVulkanRenderer(device, vulkan_info) {
  m_vertex_shader_uniform_buffer = std::make_unique<BackgroundCommonVertexUniformBuffer>(
      device, 1, 1);
  m_time_of_day_color = std::make_unique<BackgroundCommonFragmentUniformBuffer>(
      device, 1, 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  create_pipeline_layout();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  m_vertex_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  m_fragment_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &m_fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);

  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);
  InitializeInputAttributes();
}

Tie3Vulkan::~Tie3Vulkan() {
  discard_tree_cache();
}

void Tie3Vulkan::render(DmaFollower& dma,
                        SharedVulkanRenderState* render_state,
                        ScopedProfilerNode& prof) {
  BaseTie3::render(dma, render_state, prof);
}

void Tie3Vulkan::create_pipeline_layout() {
  // If push constants are needed put them here
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{m_vertex_descriptor_layout->getDescriptorSetLayout(),
                                                          m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void Tie3Vulkan::update_load(const LevelDataVulkan* loader_data) {
  const tfrag3::Level* lev_data = loader_data->level.get();
  m_wind_vectors.clear();
  // We changed level!
  discard_tree_cache();
  for (int geo = 0; geo < 4; ++geo) {
    m_trees[geo].resize(lev_data->tie_trees[geo].size());
  }

  size_t vis_temp_len = 0;
  size_t max_draws = 0;
  size_t max_num_grps = 0;
  u16 max_wind_idx = 0;
  size_t time_of_day_count = 0;
  size_t max_inds = 0;
  for (u32 l_geo = 0; l_geo < tfrag3::TIE_GEOS; l_geo++) {
    for (u32 l_tree = 0; l_tree < lev_data->tie_trees[l_geo].size(); l_tree++) {
      size_t wind_idx_buffer_len = 0;
      size_t num_grps = 0;
      const auto& tree = lev_data->tie_trees[l_geo][l_tree];
      max_draws = std::max(tree.static_draws.size(), max_draws);
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

      lod_tree[l_tree].vertex_buffer = loader_data->tie_data[l_geo][l_tree].vertex_buffer.get();
      lod_tree[l_tree].vert_count = verts;
      lod_tree[l_tree].draws = &tree.static_draws;
      lod_tree[l_tree].colors = &tree.colors;
      lod_tree[l_tree].vis = &tree.bvh;
      lod_tree[l_tree].index_data = tree.unpacked.indices.data();
      lod_tree[l_tree].instance_info = &tree.wind_instance_info;
      lod_tree[l_tree].wind_draws = &tree.instanced_wind_draws;
      vis_temp_len = std::max(vis_temp_len, tree.bvh.vis_nodes.size());
      lod_tree[l_tree].tod_cache = background_common::swizzle_time_of_day(tree.colors);

      // todo: move to loader, this will probably be quite slow.
      //CreateVertexBuffer(tree.unpacked.indices);

      if (wind_idx_buffer_len > 0) {
        lod_tree[l_tree].wind_matrix_cache.resize(tree.wind_instance_info.size());
        lod_tree[l_tree].has_wind = true;
        lod_tree[l_tree].wind_vertex_index_buffer =
            loader_data->tie_data[l_geo][l_tree].wind_indices.get();
        u32 off = 0;
        for (auto& draw : tree.instanced_wind_draws) {
          lod_tree[l_tree].wind_vertex_index_offsets.push_back(off);
          off += draw.vertex_index_stream.size();
        }
      }

      textures[l_geo].emplace_back(VulkanTexture{m_device});
      VkDeviceSize size = 0;
      //CreateIndexBuffer(tree.unpacked.indices);
      VkExtent3D extents{TIME_OF_DAY_COLOR_COUNT, 1, 1};
      textures[l_geo][l_tree].createImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT,
                                          VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
  }

  m_cache.vis_temp.resize(vis_temp_len);
  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multidraw_count_buffer.resize(max_num_grps);
  m_cache.multidraw_index_offset_buffer.resize(max_num_grps);
  m_wind_vectors.resize(4 * max_wind_idx + 4);  // 4x u32's per wind.
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  ASSERT(time_of_day_count <= TIME_OF_DAY_COLOR_COUNT);
}

/*!
 * Set up all Vulkan and temporary buffers for a given level name.
 * The level name should be the 3 character short name.
 */
bool Tie3Vulkan::setup_for_level(const std::string& level, BaseSharedRenderState* render_state) {
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
    lg::info("TIE setup: {:.1f}ms", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}

void Tie3Vulkan::discard_tree_cache() {
  for (int geo = 0; geo < 4; ++geo) {
    for (auto& tree : m_trees[geo]) {
      //TODO: Delete textures and index buffers here
    }

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
  auto& cam_bad = settings.math_camera;
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

  for (size_t draw_idx = 0; draw_idx < tree.wind_draws->size(); draw_idx++) {
    const auto& draw = tree.wind_draws->operator[](draw_idx);
    auto double_draw = background_common::setup_tfrag_shader(
        render_state, draw.mode, &m_textures->at(draw.tree_tex_id), m_pipeline_config_info,
        m_time_of_day_color);

    int off = 0;
    for (auto& grp : draw.instance_groups) {
      if (!m_debug_all_visible && !m_cache.vis_temp.at(grp.vis_idx)) {
        off += grp.num;
        continue;  // invisible, skip.
      }

      m_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
          "camera", 1,
                                             GL_FALSE, (float*)tree.wind_matrix_cache.at(grp.instance_idx)[0].data());

      prof.add_draw_call();
      prof.add_tri(grp.num);

      tree.perf.draws++;
      tree.perf.wind_draws++;

      //glDrawElements(GL_TRIANGLE_STRIP, grp.num, GL_UNSIGNED_INT,
      //               (void*)((off + tree.wind_vertex_index_offsets.at(draw_idx)) * sizeof(u32)));
      off += grp.num;

      switch (double_draw.kind) {
        case DoubleDrawKind::NONE:
          break;
        case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
          tree.perf.draws++;
          tree.perf.wind_draws++;
          prof.add_draw_call();
          prof.add_tri(grp.num);
          m_time_of_day_color->SetUniform1f("alpha_min", -10.f);
          m_time_of_day_color->SetUniform1f("alpha_max", double_draw.aref_second);
          //glDepthMask(GL_FALSE);
          //glDrawElements(GL_TRIANGLE_STRIP, draw.vertex_index_stream.size(), GL_UNSIGNED_INT, (void*)0);
          break;
        default:
          ASSERT(false);
      }
    }
  }
}


void Tie3Vulkan::render_tree(int idx,
                       int geom,
                       const TfragRenderSettings& settings,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof) {
  // reset perf
  Timer tree_timer;
  auto& tree = m_trees.at(geom).at(idx);
  tree.perf.draws = 0;
  tree.perf.wind_draws = 0;

  // don't render if we haven't loaded
  if (!m_has_level) {
    return;
  }

  // update time of day
  if (m_color_result.size() < tree.colors->size()) {
    m_color_result.resize(tree.colors->size());
  }

  Timer interp_timer;
  if (m_use_fast_time_of_day) {
    background_common::interp_time_of_day_fast(settings.itimes, tree.tod_cache, m_color_result.data());
  } else {
    background_common::interp_time_of_day_slow(settings.itimes, *tree.colors, m_color_result.data());
  }
  tree.perf.tod_time.add(interp_timer.getSeconds());

  Timer setup_timer;

  VulkanTexture timeOfDayTexture{m_device};
  VkDeviceSize size = m_color_result.size() * sizeof(m_color_result[0]);

  VkExtent3D extents{tree.colors->size(), 1, 1};
  timeOfDayTexture.createImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_A8B8G8R8_SRGB_PACK32, VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  timeOfDayTexture.createImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SRGB_PACK32,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1);

  timeOfDayTexture.writeToImage(m_color_result.data());

  // setup Vulkan shader
  background_common::first_tfrag_draw_setup(settings, render_state, m_vertex_shader_uniform_buffer);

  tree.perf.tod_time.add(setup_timer.getSeconds());

  int last_texture = -1;

  if (!m_debug_all_visible) {
    // need culling data
    Timer cull_timer;
    background_common::cull_check_all_slow(settings.planes, tree.vis->vis_nodes, settings.occlusion_culling,
                        m_cache.vis_temp.data());
    tree.perf.cull_time.add(cull_timer.getSeconds());
  } else {
    // no culling.
    tree.perf.cull_time.add(0);
  }

  u32 num_tris;
  if (render_state->no_multidraw) {
    Timer index_timer;
    u32 idx_buffer_size;
    if (m_debug_all_visible) {
      idx_buffer_size =
          background_common::make_all_visible_index_list(m_cache.draw_idx_temp.data(), m_cache.index_temp.data(),
                                      *tree.draws, tree.index_data, &num_tris);
    } else {
      idx_buffer_size = background_common::make_index_list_from_vis_string(
          m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, m_cache.vis_temp,
          tree.index_data, &num_tris);
    }

    //CreateIndexBuffer(m_cache.index_temp);
    //glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size * sizeof(u32), m_cache.index_temp.data(),
    //             GL_STREAM_DRAW);
    tree.perf.index_time.add(index_timer.getSeconds());

  } else {
    if (m_debug_all_visible) {
      Timer index_timer;
      num_tris = background_common::make_all_visible_multidraws(
          m_cache.multidraw_offset_per_stripdraw.data(), m_cache.multidraw_count_buffer.data(),
          m_cache.multidraw_index_offset_buffer.data(), *tree.draws);
      tree.perf.index_time.add(index_timer.getSeconds());
    } else {
      Timer index_timer;
      num_tris = background_common::make_multidraws_from_vis_string(
          m_cache.multidraw_offset_per_stripdraw.data(), m_cache.multidraw_count_buffer.data(),
          m_cache.multidraw_index_offset_buffer.data(), *tree.draws, m_cache.vis_temp);
      tree.perf.index_time.add(index_timer.getSeconds());
    }
  }

  Timer draw_timer;
  prof.add_tri(num_tris);

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

    auto double_draw = background_common::setup_tfrag_shader(render_state, draw.mode,
                                          &m_textures->at(draw.tree_tex_id), m_pipeline_config_info, m_time_of_day_color);

    prof.add_draw_call();

    tree.perf.draws++;

    if (render_state->no_multidraw) {
      m_vulkan_info.swap_chain->drawIndexedCommandBuffer(
          m_vulkan_info.render_command_buffer, tree.vertex_buffer, tree.index_buffer,
          m_pipeline_config_info.pipelineLayout, m_descriptor_sets);
      //glDrawElements(GL_TRIANGLE_STRIP, singledraw_indices.second, GL_UNSIGNED_INT,
      //               (void*)(singledraw_indices.first * sizeof(u32)));
    } else {
      //glMultiDrawElements(GL_TRIANGLE_STRIP,
      //                    &m_cache.multidraw_count_buffer[multidraw_indices.first], GL_UNSIGNED_INT,
      //                    &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
      //                    multidraw_indices.second);
    }

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        tree.perf.draws++;
        prof.add_draw_call();
        m_time_of_day_color->SetUniform1f("alpha_min", -10.f);
        m_time_of_day_color->SetUniform1f("alpha_max", double_draw.aref_second);
        if (render_state->no_multidraw) {
          //glDrawElements(GL_TRIANGLE_STRIP, singledraw_indices.second, GL_UNSIGNED_INT,
          //               (void*)(singledraw_indices.first * sizeof(u32)));
        } else {
          //glMultiDrawElements(
          //    GL_TRIANGLE_STRIP, &m_cache.multidraw_count_buffer[multidraw_indices.first],
          //    GL_UNSIGNED_INT, &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
          //    multidraw_indices.second);
        }
        break;
      default:
        ASSERT(false);
    }

    if (m_debug_wireframe && !render_state->no_multidraw) {
      m_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
          "camera", 1, GL_FALSE, (float*)settings.math_camera.data());
      m_vertex_shader_uniform_buffer->SetUniform4f("hvdf_offset",
          settings.hvdf_offset[0], settings.hvdf_offset[1], settings.hvdf_offset[2],
          settings.hvdf_offset[3]);
      m_vertex_shader_uniform_buffer->SetUniform1f("fog_constant", settings.fog.x());
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
      //glMultiDrawElements(GL_TRIANGLE_STRIP,
      //                    &m_cache.multidraw_count_buffer[multidraw_indices.first], GL_UNSIGNED_INT,
      //                    &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
      //                    multidraw_indices.second);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
      prof.add_draw_call();
    }
  }

  if (!m_hide_wind) {
    auto wind_prof = prof.make_scoped_child("wind");
    render_tree_wind(idx, geom, settings, render_state, wind_prof);
  }

  tree.perf.draw_time.add(draw_timer.getSeconds());
  tree.perf.tree_time.add(tree_timer.getSeconds());
}

void Tie3Vulkan::init_shaders(VulkanShaderLibrary& shaders) {
  auto& shader = shaders[ShaderId::TFRAG3];

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

void Tie3Vulkan::InitializeInputAttributes() {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::PreloadedVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions = {bindingDescription};

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

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
}
