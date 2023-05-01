#include "Tie3.h"

#include "third-party/imgui/imgui.h"

Tie3Vulkan::Tie3Vulkan(const std::string& name,
                       int my_id,
                       std::unique_ptr<GraphicsDeviceVulkan>& device,
                       VulkanInitializationInfo& vulkan_info,
                       int level_id,
                       tfrag3::TieCategory category)
    : BaseTie3(name, my_id, level_id, category), BucketVulkanRenderer(device, vulkan_info) {
  m_vertex_shader_uniform_buffer = std::make_unique<BackgroundCommonVertexUniformBuffer>(
      device, background_common::TIME_OF_DAY_COLOR_COUNT, 1);
  m_time_of_day_color_uniform_buffer = std::make_unique<BackgroundCommonFragmentUniformBuffer>(
      device, background_common::TIME_OF_DAY_COLOR_COUNT, 1);

  m_etie_vertex_shader_uniform_buffer = std::make_unique<BackgroundCommonEtieVertexUniformBuffer>(
      device, background_common::TIME_OF_DAY_COLOR_COUNT, 1);
  m_etie_time_of_day_color_uniform_buffer = std::make_unique<BackgroundCommonFragmentUniformBuffer>(
      device, background_common::TIME_OF_DAY_COLOR_COUNT, 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  create_pipeline_layout();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_vertex_buffer_descriptor_info =
      VkDescriptorBufferInfo{m_vertex_shader_uniform_buffer->getBuffer(), 0,
                             m_vertex_shader_uniform_buffer->getAlignmentSize()};

  m_fragment_buffer_descriptor_info =
      VkDescriptorBufferInfo{m_time_of_day_color_uniform_buffer->getBuffer(), 0,
                             m_time_of_day_color_uniform_buffer->getAlignmentSize()};

  m_vertex_shader_descriptor_sets.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
  m_fragment_shader_descriptor_sets.resize(background_common::TIME_OF_DAY_COLOR_COUNT);

  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .writeImage(1, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  m_fragment_descriptor_writer->writeBuffer(0, &m_fragment_buffer_descriptor_info)
      .writeImage(1, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
  InitializeInputAttributes();

  m_descriptor_image_infos.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
  m_time_of_day_samplers.resize(background_common::TIME_OF_DAY_COLOR_COUNT, {device});

  auto descriptorSetLayout = m_fragment_descriptor_layout->getDescriptorSetLayout();
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      background_common::TIME_OF_DAY_COLOR_COUNT, descriptorSetLayout};

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_vulkan_info.descriptor_pool->getDescriptorPool();
  allocInfo.pSetLayouts = descriptorSetLayouts.data();
  allocInfo.descriptorSetCount = descriptorSetLayouts.size();

  if (vkAllocateDescriptorSets(m_device->getLogicalDevice(), &allocInfo,
                               m_vertex_shader_descriptor_sets.data())) {
    throw std::exception("Failed to allocated descriptor set in Shrub");
  }
  if (vkAllocateDescriptorSets(m_device->getLogicalDevice(), &allocInfo,
                               m_fragment_shader_descriptor_sets.data())) {
    throw std::exception("Failed to allocated descriptor set in Shrub");
  }
}

Tie3Vulkan::~Tie3Vulkan() {
  discard_tree_cache();
  vkFreeDescriptorSets(
      m_device->getLogicalDevice(), m_vulkan_info.descriptor_pool->getDescriptorPool(),
      m_vertex_shader_descriptor_sets.size(), m_vertex_shader_descriptor_sets.data());
  vkFreeDescriptorSets(
      m_device->getLogicalDevice(), m_vulkan_info.descriptor_pool->getDescriptorPool(),
      m_fragment_shader_descriptor_sets.size(), m_fragment_shader_descriptor_sets.data());
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

  VkPushConstantRange pushConstantRange;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(m_push_constant);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  pipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
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

      // openGL vertex buffer from loader
      lod_tree[l_tree].vertex_buffer =
          std::make_unique<VertexBuffer>(m_device, sizeof(tree.packed_vertices.color_indices[0]), verts);
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
      lod_tree[l_tree].index_buffer = std::make_unique<IndexBuffer>(m_device, sizeof(tree.unpacked.indices[0]),
                                                                    tree.unpacked.indices.size());
      lod_tree[l_tree].category_draw_indices = tree.category_draw_indices;

      if (wind_idx_buffer_len > 0) {
        lod_tree[l_tree].wind_matrix_cache.resize(tree.wind_instance_info.size());
        lod_tree[l_tree].has_wind = true;
        lod_tree[l_tree].wind_vertex_buffer =
            std::make_unique<VertexBuffer>(m_device, sizeof(u32), wind_idx_buffer_len); //TODO: Check to see if this is correct
        lod_tree[l_tree].wind_index_buffer =
            std::make_unique<IndexBuffer>(m_device, sizeof(u32), wind_idx_buffer_len);
        u32 off = 0;
        for (auto& draw : tree.instanced_wind_draws) {
          lod_tree[l_tree].wind_vertex_index_offsets.push_back(off);
          off += draw.vertex_index_stream.size();
        }
      }

      lod_tree[l_tree].time_of_day_texture = std::make_unique<VulkanTexture>(m_device);
      VkExtent3D extents{background_common::TIME_OF_DAY_COLOR_COUNT, 1, 1};
      lod_tree[l_tree].time_of_day_texture->createImage(
          extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UINT,
          VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

      lod_tree[l_tree].vis_temp.resize(tree.bvh.vis_nodes.size());

      lod_tree[l_tree].draw_idx_temp.resize(tree.static_draws.size());
      lod_tree[l_tree].index_temp.resize(tree.unpacked.indices.size());
      lod_tree[l_tree].multidraw_idx_temp.resize(tree.static_draws.size());
    }
  }

  m_wind_vectors.resize(4 * max_wind_idx + 4);  // 4x u32's per wind.
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  m_cache.multi_draw_indexed_infos.resize(max_draws);
  ASSERT(time_of_day_count <= background_common::TIME_OF_DAY_COLOR_COUNT);
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

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffers[] = {tree.wind_vertex_buffer->getBuffer()};
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, tree.wind_index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

  for (size_t draw_idx = 0; draw_idx < tree.wind_draws->size(); draw_idx++) {
    const auto& draw = tree.wind_draws->at(draw_idx);

    auto& time_of_day_texture = m_textures->at(draw.tree_tex_id);
    auto& time_of_day_sampler = m_time_of_day_samplers.at(draw.tree_tex_id);

    auto double_draw = vulkan_background_common::setup_tfrag_shader(
        render_state, draw.mode,
        time_of_day_sampler, m_pipeline_config_info,
        m_time_of_day_color_uniform_buffer);

    int off = 0;
    for (auto& grp : draw.instance_groups) {
      if (!m_debug_all_visible && !tree.vis_temp.at(grp.vis_idx)) {
        off += grp.num;
        continue;  // invisible, skip.
      }

      m_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
          "camera", 1, GL_FALSE, (float*)tree.wind_matrix_cache.at(grp.instance_idx)[0].data());

      prof.add_draw_call();
      prof.add_tri(grp.num);

      tree.perf.draws++;
      tree.perf.wind_draws++;

      m_graphics_pipeline_layouts[draw.tree_tex_id].createGraphicsPipeline(m_pipeline_config_info);
      m_graphics_pipeline_layouts[draw.tree_tex_id].bind(m_vulkan_info.render_command_buffer);

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
          m_time_of_day_color_uniform_buffer->SetUniform1f("alpha_min", -10.f);
          m_time_of_day_color_uniform_buffer->SetUniform1f("alpha_max", double_draw.aref_second);
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
    vulkan_background_common::first_tfrag_draw_setup(settings, render_state, m_etie_vertex_shader_uniform_buffer.get());
    // if we use envmap, use the envmap-style math for the base draw to avoid rounding issue.
    init_etie_cam_uniforms(render_state);
  } else {
    vulkan_background_common::first_tfrag_draw_setup(settings, render_state,
                                                     m_vertex_shader_uniform_buffer.get());
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

  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant),
                     (void*)&m_push_constant);

  int last_texture = -1;
  for (size_t draw_idx = tree.category_draw_indices[(int)category];
       draw_idx < tree.category_draw_indices[(int)category + 1]; draw_idx++) {
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

    auto& texture = m_textures->at(draw.tree_tex_id);
    auto& time_of_day_sampler = m_time_of_day_samplers.at(draw.tree_tex_id);

    auto double_draw = vulkan_background_common::setup_tfrag_shader(
        render_state, draw.mode, time_of_day_sampler, m_pipeline_config_info,
        m_etie_time_of_day_color_uniform_buffer);

    uint32_t decal_mode = draw.mode.get_decal();
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(m_push_constant),
                       sizeof(unsigned), (void*) &decal_mode);

    m_graphics_pipeline_layouts[idx].createGraphicsPipeline(m_pipeline_config_info);
    m_graphics_pipeline_layouts[idx].bind(m_vulkan_info.render_command_buffer);

    m_descriptor_image_infos[idx] = VkDescriptorImageInfo{
        m_time_of_day_samplers.at(idx).GetSampler(), m_textures->at(idx).getImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    auto& vertex_write_descriptors_sets = m_vertex_descriptor_writer->getWriteDescriptorSets();
    vertex_write_descriptors_sets[1] =
        m_vertex_descriptor_writer->writeImageDescriptorSet(1, &m_descriptor_image_infos[idx]);

    // FIXME: Wrong image used here
    auto& fragment_write_descriptors_sets = m_fragment_descriptor_writer->getWriteDescriptorSets();
    fragment_write_descriptors_sets[1] =
        m_fragment_descriptor_writer->writeImageDescriptorSet(1, &m_descriptor_image_infos[idx]);

    m_vertex_descriptor_writer->overwrite(m_vertex_shader_descriptor_sets[idx]);
    m_fragment_descriptor_writer->overwrite(m_fragment_shader_descriptor_sets[idx]);

    std::array<uint32_t, 1> dynamicDescriptorOffsets = {
        idx * m_device->getMinimumBufferOffsetAlignment(
                    sizeof(BackgroundCommonFragmentUniformShaderData))};

    std::vector<VkDescriptorSet> descriptor_sets{m_vertex_shader_descriptor_sets[idx],
                                                 m_fragment_shader_descriptor_sets[idx]};

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, descriptor_sets.size(),
                            descriptor_sets.data(), dynamicDescriptorOffsets.size(),
                            dynamicDescriptorOffsets.data());

    prof.add_draw_call();

    if (render_state->no_multidraw) {
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws, 1,
                       singledraw_indices.draw_index, 0, 0);
    } else {
      vkCmdDrawMultiIndexedEXT(
          m_vulkan_info.render_command_buffer, multidraw_indices.number_of_draws,
          m_cache.multi_draw_indexed_infos.data(), 1, 0, sizeof(VkMultiDrawIndexedInfoEXT), NULL);
    }

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        ASSERT(false);
        m_time_of_day_color_uniform_buffer->SetUniform1f("alpha_min", -10.f);
        m_time_of_day_color_uniform_buffer->SetUniform1f("alpha_max", double_draw.aref_second);

        m_time_of_day_color_uniform_buffer->map();
        m_time_of_day_color_uniform_buffer->flush();
        m_time_of_day_color_uniform_buffer->unmap();
        if (render_state->no_multidraw) {
          vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws,
                           1, singledraw_indices.draw_index, 0, 0);
        } else {
          vkCmdDrawMultiIndexedEXT(m_vulkan_info.render_command_buffer,
                                   multidraw_indices.number_of_draws,
                                   m_cache.multi_draw_indexed_infos.data(), 1, 0,
                                   sizeof(VkMultiDrawIndexedInfoEXT), NULL);
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
    envmap_second_pass_draw(tree, settings, render_state, prof,
                            tfrag3::get_second_draw_category(category), idx);
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

  if (m_use_fast_time_of_day) {
    background_common::interp_time_of_day_fast(settings.itimes, tree.tod_cache, m_color_result.data());
  } else {
    background_common::interp_time_of_day_slow(settings.itimes, *tree.colors,
                                               m_color_result.data());
  }

  tree.time_of_day_texture->writeToImage((tfrag3::TimeOfDayColor*)tree.colors->data(),
                                         tree.colors->size() * sizeof(tfrag3::TimeOfDayColor));

  // update proto vis mask
  if (proto_vis_data) {
    tree.proto_visibility.update(proto_vis_data, proto_vis_data_size);
  }

  if (!m_debug_all_visible) {
    // need culling data
    background_common::cull_check_all_slow(settings.planes, tree.vis->vis_nodes,
                                           settings.occlusion_culling, tree.vis_temp.data());
  }

  u32 num_tris = 0;
  if (use_multidraw) {
    if (m_debug_all_visible) {
      num_tris = vulkan_background_common::make_all_visible_multidraws(
          m_cache.multi_draw_indexed_infos, *tree.draws);
    } else {
      Timer index_timer;
      if (tree.has_proto_visibility) {
        //num_tris = vulkan_background_common::make_multidraws_from_vis_and_proto_string(
        //    tree.multidraw_offset_per_stripdraw.data(), tree.multidraw_count_buffer.data(),
        //    tree.multidraw_index_offset_buffer.data(), *tree.draws, tree.vis_temp,
        //    tree.proto_visibility.vis_flags);
      } else {
        num_tris = vulkan_background_common::make_multidraws_from_vis_string(
            m_cache.multi_draw_indexed_infos, *tree.draws, tree.vis_temp);
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
                                         tfrag3::TieCategory category, int index) {
  vulkan_background_common::first_tfrag_draw_setup(settings, render_state, m_etie_vertex_shader_uniform_buffer.get());
  if (render_state->no_multidraw) {
    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                         tree.single_draw_index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
  } else {
    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, tree.index_buffer->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);
  }

  PrepareVulkanDraw(tree, index);
  init_etie_cam_uniforms(render_state);
  m_etie_vertex_shader_uniform_buffer->SetUniformMathVector4f("envmap_tod_tint", m_common_data.envmap_color);

  int last_texture = -1;
  for (size_t draw_idx = tree.category_draw_indices[(int)category];
       draw_idx < tree.category_draw_indices[(int)category + 1]; draw_idx++) {
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

    auto& texture = m_textures->at(draw.tree_tex_id);
    auto& time_of_day_sampler = m_time_of_day_samplers.at(draw.tree_tex_id);

    auto double_draw = vulkan_background_common::setup_tfrag_shader(
        render_state, draw.mode, time_of_day_sampler, m_pipeline_config_info,
        m_etie_time_of_day_color_uniform_buffer);

    prof.add_draw_call();

    if (render_state->no_multidraw) {
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, singledraw_indices.number_of_draws, 1,
                       singledraw_indices.draw_index, 0, 0);
    } else {
      vkCmdDrawMultiIndexedEXT(
          m_vulkan_info.render_command_buffer, multidraw_indices.number_of_draws,
          m_cache.multi_draw_indexed_infos.data(), 1, 0, sizeof(VkMultiDrawIndexedInfoEXT), NULL);
    }

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      default:
        ASSERT(false);
    }
  }
}

void Tie3Vulkan::init_etie_cam_uniforms(const BaseSharedRenderState* render_state) {
  m_etie_vertex_shader_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "camera_no_presp", 1, GL_FALSE, (float*)render_state->camera_no_persp[0].data());

  math::Vector4f perspective[2];
  float inv_fog = 1.f / render_state->camera_fog[0];
  auto& hvdf_off = render_state->camera_hvdf_off;
  float pxx = render_state->camera_persp[0].x();
  float pyy = render_state->camera_persp[1].y();
  float pzz = render_state->camera_persp[2].z();
  float pzw = render_state->camera_persp[2].w();
  float pwz = render_state->camera_persp[3].z();
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

void Tie3Vulkan::PrepareVulkanDraw(TreeVulkan& tree, int index) {
  m_descriptor_image_infos[index] = VkDescriptorImageInfo{
      m_time_of_day_samplers.at(index).GetSampler(), m_textures->at(index).getImageView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  auto& vertex_write_descriptors_sets = m_vertex_descriptor_writer->getWriteDescriptorSets();
  vertex_write_descriptors_sets[1] = m_vertex_descriptor_writer->writeImageDescriptorSet(
      1, &m_descriptor_image_infos[index]);

  //FIXME: Wrong image used here
  auto& fragment_write_descriptors_sets = m_fragment_descriptor_writer->getWriteDescriptorSets();
  fragment_write_descriptors_sets[1] = m_fragment_descriptor_writer->writeImageDescriptorSet(
      1, &m_descriptor_image_infos[index]);

  m_vertex_descriptor_writer->overwrite(m_vertex_shader_descriptor_sets[index]);
  m_fragment_descriptor_writer->overwrite(m_fragment_shader_descriptor_sets[index]);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = tree.vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, tree.index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant),
                     (void*)&m_push_constant);

  std::array<uint32_t, 2> dynamicDescriptorOffsets = {
      index * m_device->getMinimumBufferOffsetAlignment(
                  sizeof(BackgroundCommonVertexUniformShaderData)),
      index * m_device->getMinimumBufferOffsetAlignment(
                  sizeof(BackgroundCommonFragmentUniformShaderData))};

  std::vector<VkDescriptorSet> descriptor_sets{m_vertex_shader_descriptor_sets[index],
                                               m_fragment_shader_descriptor_sets[index]};

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, descriptor_sets.size(),
                          descriptor_sets.data(), dynamicDescriptorOffsets.size(),
                          dynamicDescriptorOffsets.data());
}

Tie3VulkanAnotherCategory::Tie3VulkanAnotherCategory(const std::string& name,
                            int my_id,
                            std::unique_ptr<GraphicsDeviceVulkan>& device,
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
                       std::unique_ptr<GraphicsDeviceVulkan>& device,
                       VulkanInitializationInfo& vulkan_info,
                       int level_id) : Tie3Vulkan(name, my_id, device, vulkan_info, level_id, tfrag3::TieCategory::NORMAL) {
}
void Tie3VulkanWithEnvmapJak1::render(DmaFollower& dma,
                                      SharedVulkanRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();
  BaseTie3::render(dma, render_state, prof);
  if (m_enable_envmap) {
    render_from_another(render_state, prof, tfrag3::TieCategory::NORMAL_ENVMAP);
  }
}

void Tie3VulkanWithEnvmapJak1::draw_debug_window() {
  ImGui::Checkbox("envmap", &m_enable_envmap);
  BaseTie3::draw_debug_window();
}
