#include "Tie3.h"

#include "third-party/imgui/imgui.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

Tie3::Tie3(const std::string& name,
           BucketId my_id,
           std::unique_ptr<GraphicsDeviceVulkan>& device,
           VulkanInitializationInfo& vulkan_info,
           int level_id)
    : BucketRenderer(name, my_id, device, vulkan_info),
      m_level_id(level_id) {
  m_vertex_shader_uniform_buffer = std::make_unique<BackgroundCommonVertexUniformBuffer>(
      device, 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);
  m_time_of_day_color = std::make_unique<BackgroundCommonFragmentUniformBuffer>(
      device, 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);

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
  auto vertex_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);
  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);
}

Tie3::~Tie3() {
  discard_tree_cache();
}

void Tie3::update_load(const LevelData* loader_data) {
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
      lod_tree[l_tree].tod_cache = vk_common_background_renderer::swizzle_time_of_day(tree.colors);

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

      VkDeviceSize size = 0;
      //CreateIndexBuffer(tree.unpacked.indices);
      VkExtent3D extents{TIME_OF_DAY_COLOR_COUNT, 1, 1};
      textures[l_geo][l_tree].CreateImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT,
                                          VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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
 * Set up all OpenGL and temporary buffers for a given level name.
 * The level name should be the 3 character short name.
 */
bool Tie3::setup_for_level(const std::string& level, SharedRenderState* render_state) {
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
    fmt::print("TIE setup: {:.1f}ms\n", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}

void vector_min_in_place(math::Vector4f& v, float val) {
  for (int i = 0; i < 4; i++) {
    if (v[i] > val) {
      v[i] = val;
    }
  }
}

math::Vector4f vector_max(const math::Vector4f& v, float val) {
  math::Vector4f result;
  for (int i = 0; i < 4; i++) {
    result[i] = std::max(val, v[i]);
  }
  return result;
}

void do_wind_math(u16 wind_idx,
                  float* wind_vector_data,
                  const Tie3::WindWork& wind_work,
                  float stiffness,
                  std::array<math::Vector4f, 4>& mat) {
  float* my_vector = wind_vector_data + (4 * wind_idx);
  const auto& work_vector = wind_work.wind_array[(wind_work.wind_time + wind_idx) & 63];
  constexpr float cx = 0.5;
  constexpr float cy = 100.0;
  constexpr float cz = 0.0166;
  constexpr float cw = -1.0;

  // ld s1, 8(s5)                    # load wind vector 1
  // pextlw s1, r0, s1               # convert to 2x 64 bits, by shifting left
  // qmtc2.i vf18, s1                # put in vf
  float vf18_x = my_vector[2];
  float vf18_z = my_vector[3];

  // ld s2, 0(s5)                    # load wind vector 0
  // pextlw s3, r0, s2               # convert to 2x 64 bits, by shifting left
  // qmtc2.i vf17, s3                # put in vf
  float vf17_x = my_vector[0];
  float vf17_z = my_vector[1];

  // lqc2 vf16, 12(s3)               # load wind vector
  math::Vector4f vf16 = work_vector;

  // vmula.xyzw acc, vf16, vf1       # acc = vf16
  // vmsubax.xyzw acc, vf18, vf19    # acc = vf16 - vf18 * wind_const.x
  // vmsuby.xyzw vf16, vf17, vf19
  //# vf16 -= (vf18 * wind_const.x) + (vf17 * wind_const.y)
  vf16.x() -= cx * vf18_x + cy * vf17_x;
  vf16.z() -= cx * vf18_z + cy * vf17_z;

  // vmulaz.xyzw acc, vf16, vf19     # acc = vf16 * wind_const.z
  // vmadd.xyzw vf18, vf1, vf18
  //# vf18 += vf16 * wind_const.z
  math::Vector4f vf18(vf18_x, 0.f, vf18_z, 0.f);
  vf18 += vf16 * cz;

  // vmulaz.xyzw acc, vf18, vf19    # acc = vf18 * wind_const.z
  // vmadd.xyzw vf17, vf17, vf1
  //# vf17 += vf18 * wind_const.z
  math::Vector4f vf17(vf17_x, 0.f, vf17_z, 0.f);
  vf17 += vf18 * cz;

  // vitof12.xyzw vf11, vf11 # normal convert
  // vitof12.xyzw vf12, vf12 # normal convert

  // vminiw.xyzw vf17, vf17, vf0
  vector_min_in_place(vf17, 1.f);

  // qmfc2.i s3, vf18
  // ppacw s3, r0, s3

  // vmaxw.xyzw vf27, vf17, vf19
  auto vf27 = vector_max(vf17, cw);

  // vmulw.xyzw vf27, vf27, vf15
  vf27 *= stiffness;

  // vmulax.yw acc, vf0, vf0
  // vmulay.xz acc, vf27, vf10
  // vmadd.xyzw vf10, vf1, vf10
  mat[0].x() += vf27.x() * mat[0].y();
  mat[0].z() += vf27.z() * mat[0].y();

  // qmfc2.i s2, vf27
  if (!wind_work.paused) {
    my_vector[0] = vf27.x();
    my_vector[1] = vf27.z();
    my_vector[2] = vf18.x();
    my_vector[3] = vf18.z();
  }

  // vmulax.yw acc, vf0, vf0
  // vmulay.xz acc, vf27, vf11
  // vmadd.xyzw vf11, vf1, vf11
  mat[1].x() += vf27.x() * mat[1].y();
  mat[1].z() += vf27.z() * mat[1].y();

  // ppacw s2, r0, s2
  // vmulax.yw acc, vf0, vf0
  // vmulay.xz acc, vf27, vf12
  // vmadd.xyzw vf12, vf1, vf12
  mat[2].x() += vf27.x() * mat[2].y();
  mat[2].z() += vf27.z() * mat[2].y();

  //
  // if not paused
  // sd s3, 8(s5)
  // sd s2, 0(s5)
}

void Tie3::discard_tree_cache() {
  for (int geo = 0; geo < 4; ++geo) {
    for (auto& tree : m_trees[geo]) {
      //TODO: Delete textures and index buffers here
    }

    m_trees[geo].clear();
  }
}

void Tie3::render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) {
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  if (m_override_level && m_pending_user_level) {
    m_has_level = setup_for_level(*m_pending_user_level, render_state);
    m_pending_user_level = {};
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

  auto gs_test = dma.read_and_advance();
  ASSERT(gs_test.size_bytes == 32);

  auto tie_consts = dma.read_and_advance();
  ASSERT(tie_consts.size_bytes == 9 * 16);

  auto mscalf = dma.read_and_advance();
  ASSERT(mscalf.size_bytes == 0);

  auto row = dma.read_and_advance();
  ASSERT(row.size_bytes == 32);

  auto next = dma.read_and_advance();
  ASSERT(next.size_bytes == 0);

  auto pc_port_data = dma.read_and_advance();
  ASSERT(pc_port_data.size_bytes == sizeof(TfragPcPortData));
  memcpy(&m_pc_port_data, pc_port_data.data, sizeof(TfragPcPortData));
  m_pc_port_data.level_name[11] = '\0';

  auto wind_data = dma.read_and_advance();
  ASSERT(wind_data.size_bytes == sizeof(WindWork));
  memcpy(&m_wind_data, wind_data.data, sizeof(WindWork));

  while (dma.current_tag_offset() != render_state->next_bucket) {
    dma.read_and_advance();
  }

  TfragRenderSettings settings;
  settings.hvdf_offset = m_pc_port_data.hvdf_off;
  settings.fog = m_pc_port_data.fog;

  memcpy(settings.math_camera.data(), m_pc_port_data.camera[0].data(), 64);
  settings.tree_idx = 0;

  if (render_state->occlusion_vis[m_level_id].valid) {
    settings.occlusion_culling = render_state->occlusion_vis[m_level_id].data;
  }

  vk_common_background_renderer::update_render_state_from_pc_settings(render_state, m_pc_port_data);

  for (int i = 0; i < 4; i++) {
    settings.planes[i] = m_pc_port_data.planes[i];
  }

  if (false) {
    //    for (int i = 0; i < 8; i++) {
    //      settings.time_of_day_weights[i] = m_time_of_days[i];
    //    }
  } else {
    for (int i = 0; i < 8; i++) {
      settings.time_of_day_weights[i] =
          2 * (0xff & m_pc_port_data.itimes[i / 2].data()[2 * (i % 2)]) / 127.f;
    }
  }

  if (!m_override_level) {
    m_has_level = setup_for_level(m_pc_port_data.level_name, render_state);
  }

  render_all_trees(lod(), settings, render_state, prof);
}

void Tie3::render_all_trees(int geom,
                            const TfragRenderSettings& settings,
                            SharedRenderState* render_state,
                            ScopedProfilerNode& prof) {
  Timer all_tree_timer;
  if (m_override_level && m_pending_user_level) {
    m_has_level = setup_for_level(*m_pending_user_level, render_state);
    m_pending_user_level = {};
  }
  for (u32 i = 0; i < m_trees[geom].size(); i++) {
    render_tree(i, geom, settings, render_state, prof);
  }
  m_all_tree_time.add(all_tree_timer.getSeconds());
}

void Tie3::render_tree_wind(int idx,
                            int geom,
                            const TfragRenderSettings& settings,
                            SharedRenderState* render_state,
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
    auto double_draw = vk_common_background_renderer::setup_tfrag_shader(
        render_state, draw.mode, m_textures->at(draw.tree_tex_id), m_pipeline_config_info,
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

void Tie3::render_tree(int idx,
                       int geom,
                       const TfragRenderSettings& settings,
                       SharedRenderState* render_state,
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
    vk_common_background_renderer::interp_time_of_day_fast(settings.time_of_day_weights, tree.tod_cache, m_color_result.data());
  } else {
    vk_common_background_renderer::interp_time_of_day_slow(settings.time_of_day_weights, *tree.colors, m_color_result.data());
  }
  tree.perf.tod_time.add(interp_timer.getSeconds());

  Timer setup_timer;

  TextureInfo timeOfDayTexture{m_device};
  VkDeviceSize size = m_color_result.size() * sizeof(m_color_result[0]);

  VkExtent3D extents{tree.colors->size(), 1, 1};
  timeOfDayTexture.CreateImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_A8B8G8R8_SINT_PACK32,
              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  timeOfDayTexture.CreateImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SINT_PACK32,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1);

  timeOfDayTexture.map();
  timeOfDayTexture.writeToBuffer(m_color_result.data());
  timeOfDayTexture.unmap();

  // setup Vulkan shader
  vk_common_background_renderer::first_tfrag_draw_setup(settings, render_state, m_vertex_shader_uniform_buffer);

  tree.perf.tod_time.add(setup_timer.getSeconds());

  int last_texture = -1;

  if (!m_debug_all_visible) {
    // need culling data
    Timer cull_timer;
    vk_common_background_renderer::cull_check_all_slow(settings.planes, tree.vis->vis_nodes, settings.occlusion_culling,
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
          vk_common_background_renderer::make_all_visible_index_list(m_cache.draw_idx_temp.data(), m_cache.index_temp.data(),
                                      *tree.draws, tree.index_data, &num_tris);
    } else {
      idx_buffer_size = vk_common_background_renderer::make_index_list_from_vis_string(
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
      num_tris = vk_common_background_renderer::make_all_visible_multidraws(
          m_cache.multidraw_offset_per_stripdraw.data(), m_cache.multidraw_count_buffer.data(),
          m_cache.multidraw_index_offset_buffer.data(), *tree.draws);
      tree.perf.index_time.add(index_timer.getSeconds());
    } else {
      Timer index_timer;
      num_tris = vk_common_background_renderer::make_multidraws_from_vis_string(
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

    auto double_draw = vk_common_background_renderer::setup_tfrag_shader(render_state, draw.mode,
                                          m_textures->at(draw.tree_tex_id), m_pipeline_config_info, m_time_of_day_color);

    prof.add_draw_call();

    tree.perf.draws++;

    if (render_state->no_multidraw) {
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

void Tie3::draw_debug_window() {
  ImGui::InputText("Custom Level", m_user_level, sizeof(m_user_level));
  if (ImGui::Button("Go!")) {
    m_pending_user_level = m_user_level;
  }
  ImGui::Checkbox("Override level", &m_override_level);
  ImGui::Checkbox("Fast ToD", &m_use_fast_time_of_day);
  ImGui::Checkbox("Wireframe", &m_debug_wireframe);
  ImGui::SameLine();
  ImGui::Checkbox("All Visible", &m_debug_all_visible);
  ImGui::Checkbox("Hide Wind", &m_hide_wind);
  ImGui::SliderFloat("Wind Multiplier", &m_wind_multiplier, 0., 40.f);
  ImGui::Separator();
  for (u32 i = 0; i < m_trees[lod()].size(); i++) {
    auto& perf = m_trees[lod()][i].perf;
    ImGui::Text("Tree: %d", i);
    ImGui::Text("time of days: %d", (int)m_trees[lod()][i].colors->size());
    ImGui::Text("draw: %d", perf.draws);
    ImGui::Text("wind draw: %d", perf.wind_draws);
    ImGui::Text("total: %.2f", perf.tree_time.get());
    ImGui::Text("cull: %.2f index: %.2f tod: %.2f setup: %.2f draw: %.2f",
                perf.cull_time.get() * 1000.f, perf.index_time.get() * 1000.f,
                perf.tod_time.get() * 1000.f, perf.setup_time.get() * 1000.f,
                perf.draw_time.get() * 1000.f);
    ImGui::Separator();
  }
  ImGui::Text("All trees: %.2f", 1000.f * m_all_tree_time.get());
}

void Tie3::InitializeVertexBuffer(SharedRenderState* render_state) {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  auto& shader = render_state->shaders[ShaderId::TFRAG3];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Tie3 Vertex";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Tie3 Fragment";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

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
