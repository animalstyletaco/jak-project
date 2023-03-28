#include "Tie3.h"

#include "third-party/imgui/imgui.h"

BaseTie3::BaseTie3(const std::string& name,
           int my_id,
                   int level_id,
                   tfrag3::TieCategory category)
    : BaseBucketRenderer(name, my_id),
      m_level_id(level_id), m_default_category(category) {
  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
}

BaseTie3::~BaseTie3() {
}

void BaseTie3::vector_min_in_place(math::Vector4f& v, float val) {
  for (int i = 0; i < 4; i++) {
    if (v[i] > val) {
      v[i] = val;
    }
  }
}

math::Vector4f BaseTie3::vector_max(const math::Vector4f& v, float val) {
  math::Vector4f result;
  for (int i = 0; i < 4; i++) {
    result[i] = std::max(val, v[i]);
  }
  return result;
}

void BaseTie3::do_wind_math(u16 wind_idx,
                  float* wind_vector_data,
                  const BaseTie3::WindWork& wind_work,
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

void BaseTie3::render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  if (set_up_common_data_from_dma(dma, render_state)) {
    setup_all_trees(lod(), m_common_data.settings, m_common_data.proto_vis_data,
                    m_common_data.proto_vis_data_size, !render_state->no_multidraw, prof);

    draw_matching_draws_for_all_trees(lod(), m_common_data.settings, render_state, prof,
                                      m_default_category);
  }
}

bool BaseTie3::set_up_common_data_from_dma(DmaFollower& dma,
                                           BaseSharedRenderState* render_state) {
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0 || data0.vifcode1().kind == VifCode::Kind::NOP);
  ASSERT(data0.vif0() == 0 || data0.vifcode0().kind == VifCode::Kind::NOP ||
         data0.vifcode0().kind == VifCode::Kind::MARK);
  ASSERT(data0.size_bytes == 0);

  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return false;
  }

  if (dma.current_tag_offset() == render_state->next_bucket) {
    return false;
  }

  auto gs_test = dma.read_and_advance();
  if (gs_test.size_bytes == 160) {
  } else {
    ASSERT(gs_test.size_bytes == 32);

    auto tie_consts = dma.read_and_advance();
    ASSERT(tie_consts.size_bytes == 9 * 16);
  }

  auto mscalf = dma.read_and_advance();
  ASSERT(mscalf.size_bytes == 0);

  auto row = dma.read_and_advance();
  ASSERT(row.size_bytes == 32);

  auto next = dma.read_and_advance();
  if (next.size_bytes == 32) {
    next = dma.read_and_advance();
  }
  ASSERT(next.size_bytes == 0);

  auto pc_port_data = dma.read_and_advance();
  ASSERT(pc_port_data.size_bytes == sizeof(TfragPcPortData));
  memcpy(&m_pc_port_data, pc_port_data.data, sizeof(TfragPcPortData));
  m_pc_port_data.level_name[11] = '\0';

  if (render_state->version == GameVersion::Jak1) {
    auto wind_data = dma.read_and_advance();
    ASSERT(wind_data.size_bytes == sizeof(WindWork));
    memcpy(&m_wind_data, wind_data.data, sizeof(WindWork));
  }

  if (render_state->version == GameVersion::Jak2) {
    // jak 2 proto visibility
    auto proto_mask_data = dma.read_and_advance();
    m_common_data.proto_vis_data = proto_mask_data.data;
    m_common_data.proto_vis_data_size = proto_mask_data.size_bytes;
  }
  // envmap color
  auto envmap_color = dma.read_and_advance();
  ASSERT(envmap_color.size_bytes == 16);
  memcpy(m_common_data.envmap_color.data(), envmap_color.data, 16);
  m_common_data.envmap_color /= 128.f;
  if (render_state->version == GameVersion::Jak1) {
    m_common_data.envmap_color *= 2;
  }
  m_common_data.envmap_color *= m_envmap_strength;

  m_common_data.frame_idx = render_state->frame_idx;

  while (dma.current_tag_offset() != render_state->next_bucket) {
    dma.read_and_advance();
  }

  m_common_data.settings.hvdf_offset = m_pc_port_data.hvdf_off;
  m_common_data.settings.fog = m_pc_port_data.fog;

  memcpy(m_common_data.settings.math_camera.data(), m_pc_port_data.camera[0].data(), 64);
  m_common_data.settings.tree_idx = 0;

  if (render_state->occlusion_vis[m_level_id].valid) {
    m_common_data.settings.occlusion_culling = render_state->occlusion_vis[m_level_id].data;
  } else {
    m_common_data.settings.occlusion_culling = 0;
  }

  background_common::update_render_state_from_pc_settings(render_state, m_pc_port_data);

  for (int i = 0; i < 4; i++) {
    m_common_data.settings.planes[i] = m_pc_port_data.planes[i];
    m_common_data.settings.itimes[i] = m_pc_port_data.itimes[i];
  }

  m_has_level = try_loading_level(m_pc_port_data.level_name, render_state);
  return true;
}

void BaseTie3::setup_all_trees(int geom,
                            const TfragRenderSettings& settings,
                            const u8* proto_vis_data,
                            size_t proto_vis_data_size,
                            bool use_multidraw,
                            ScopedProfilerNode& prof) {
  for (u32 i = 0; i < get_tree_count(geom); i++) {
    setup_tree(i, geom, settings, proto_vis_data, proto_vis_data_size, use_multidraw, prof);
  }
}

void BaseTie3::render_from_another(BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof,
                                   tfrag3::TieCategory category) {
  if (render_state->frame_idx != m_common_data.frame_idx) {
    return;
  }
  draw_matching_draws_for_all_trees(lod(), m_common_data.settings, render_state, prof, category);
}

void BaseTie3::draw_matching_draws_for_all_trees(int geom,
                                                 const TfragRenderSettings& settings,
                                                 BaseSharedRenderState* render_state,
                                                 ScopedProfilerNode& prof,
                                                 tfrag3::TieCategory category) {
  for (u32 i = 0; i < get_tree_count(geom); i++) {
    draw_matching_draws_for_tree(i, geom, settings, render_state, prof, category);
  }
}

void BaseTie3::draw_debug_window() {
  ImGui::Checkbox("envmap 2nd draw", &m_draw_envmap_second_draw);
  ImGui::SliderFloat("envmap str", &m_envmap_strength, 0, 2);
  ImGui::Checkbox("Fast ToD", &m_use_fast_time_of_day);
  ImGui::SameLine();
  ImGui::Checkbox("All Visible", &m_debug_all_visible);
  ImGui::Checkbox("Hide Wind", &m_hide_wind);
  ImGui::SliderFloat("Wind Multiplier", &m_wind_multiplier, 0., 40.f);
  ImGui::Separator();
}

void BaseTieProtoVisibility::init(const std::vector<std::string>& names) {
  vis_flags.resize(names.size());
  for (auto& x : vis_flags) {
    x = 1;
  }
  all_visible = true;
  name_to_idx.clear();
  size_t i = 0;
  for (auto& name : names) {
    name_to_idx[name].push_back(i++);
  }
}

void BaseTieProtoVisibility::update(const u8* data, size_t size) {
  char name_buffer[256];  // ??

  if (!all_visible) {
    for (auto& x : vis_flags) {
      x = 1;
    }
    all_visible = true;
  }

  const u8* end = data + size;

  while (true) {
    int name_idx = 0;
    while (*data) {
      name_buffer[name_idx++] = *data;
      data++;
    }
    if (name_idx) {
      ASSERT(name_idx < 254);
      name_buffer[name_idx] = '\0';
      const auto& it = name_to_idx.find(name_buffer);
      if (it != name_to_idx.end()) {
        all_visible = false;
        for (auto x : name_to_idx.at(name_buffer)) {
          vis_flags[x] = 0;
        }
      }
    }

    while (*data == 0) {
      if (data >= end) {
        return;
      }
      data++;
    }
  }
}
