

#include "Sprite3.h"

#include "common/log/log.h"

#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/general_renderer/dma_helpers.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

BaseSprite3::BaseSprite3(const std::string& name, int my_id) : BaseBucketRenderer(name, my_id) {}

void BaseSprite3::graphics_setup() {
  // Set up OpenGL for 'normal' sprites
  graphics_setup_normal();

  // Set up OpenGL for distort sprites
  graphics_setup_distort();
}

void BaseSprite3::graphics_setup_normal() {
  auto verts = SPRITE_RENDERER_MAX_SPRITES * 4;
  auto bytes = verts * sizeof(SpriteVertex3D);

  u32 idx_buffer_len = SPRITE_RENDERER_MAX_SPRITES * 5;

  m_vertices_3d.resize(verts);
  m_index_buffer_data.resize(idx_buffer_len);

  m_default_mode.disable_depth_write();
  m_default_mode.set_depth_test(GsTest::ZTest::GEQUAL);
  m_default_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_SRC_DST);
  m_default_mode.set_aref(38);
  m_default_mode.set_alpha_test(DrawMode::AlphaTest::GEQUAL);
  m_default_mode.set_alpha_fail(GsTest::AlphaFail::FB_ONLY);
  m_default_mode.set_at(true);
  m_default_mode.set_zt(true);
  m_default_mode.set_ab(true);

  m_current_mode = m_default_mode;
}

/*!
 * Handle DMA data that does the per-frame setup.
 * This should get the dma chain immediately after the call to sprite-draw-distorters.
 * It ends right before the sprite-add-matrix-data for the 3d's
 */
void BaseSprite3::handle_sprite_frame_setup(DmaFollower& dma, GameVersion version) {
  // first is some direct data
  auto direct_data = dma.read_and_advance();
  ASSERT(direct_data.size_bytes == 3 * 16);
  memcpy(m_sprite_direct_setup, direct_data.data, 3 * 16);

  // next would be the program, but it's 0 size on the PC and isn't sent.

  // next is the "frame data"
  switch (version) {
    case GameVersion::Jak1: {
      auto frame_data = dma.read_and_advance();
      ASSERT(frame_data.size_bytes == (int)sizeof(SpriteFrameDataJak1));  // very cool
      ASSERT(frame_data.vifcode0().kind == VifCode::Kind::STCYCL);
      VifCodeStcycl frame_data_stcycl(frame_data.vifcode0());
      ASSERT(frame_data_stcycl.cl == 4);
      ASSERT(frame_data_stcycl.wl == 4);
      ASSERT(frame_data.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
      VifCodeUnpack frame_data_unpack(frame_data.vifcode1());
      ASSERT(frame_data_unpack.addr_qw == SpriteDataMem::FrameData);
      ASSERT(frame_data_unpack.use_tops_flag == false);
      SpriteFrameDataJak1 jak1_data;
      memcpy(&jak1_data, frame_data.data, sizeof(SpriteFrameDataJak1));
      m_frame_data.from_jak1(jak1_data);
    } break;
    case GameVersion::Jak2: {
      auto frame_data = dma.read_and_advance();
      ASSERT(frame_data.size_bytes == (int)sizeof(SpriteFrameData));  // very cool
      ASSERT(frame_data.vifcode0().kind == VifCode::Kind::STCYCL);
      VifCodeStcycl frame_data_stcycl(frame_data.vifcode0());
      ASSERT(frame_data_stcycl.cl == 4);
      ASSERT(frame_data_stcycl.wl == 4);
      ASSERT(frame_data.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
      VifCodeUnpack frame_data_unpack(frame_data.vifcode1());
      ASSERT(frame_data_unpack.addr_qw == SpriteDataMem::FrameData);
      ASSERT(frame_data_unpack.use_tops_flag == false);
      memcpy(&m_frame_data, frame_data.data, sizeof(SpriteFrameData));
    } break;
    default:
      ASSERT_NOT_REACHED();
  }

  // next, a MSCALF.
  auto mscalf = dma.read_and_advance();
  ASSERT(mscalf.size_bytes == 0);
  ASSERT(mscalf.vifcode0().kind == VifCode::Kind::MSCALF);
  ASSERT(mscalf.vifcode0().immediate == SpriteProgMem::Init);
  ASSERT(mscalf.vifcode1().kind == VifCode::Kind::FLUSHE);

  // next base and offset
  auto base_offset = dma.read_and_advance();
  ASSERT(base_offset.size_bytes == 0);
  ASSERT(base_offset.vifcode0().kind == VifCode::Kind::BASE);
  ASSERT(base_offset.vifcode0().immediate == SpriteDataMem::Buffer0);
  ASSERT(base_offset.vifcode1().kind == VifCode::Kind::OFFSET);
  ASSERT(base_offset.vifcode1().immediate == SpriteDataMem::Buffer1);
}

void BaseSprite3::render_3d(DmaFollower& dma) {
  // one time matrix data
  auto matrix_data = dma.read_and_advance();
  ASSERT(matrix_data.size_bytes == sizeof(Sprite3DMatrixData));

  bool unpack_ok = verify_unpack_with_stcycl(matrix_data, VifCode::Kind::UNPACK_V4_32, 4, 4, 5,
                                             SpriteDataMem::Matrix, false, false);
  ASSERT(unpack_ok);
  static_assert(sizeof(m_3d_matrix_data) == 5 * 16);
  memcpy(&m_3d_matrix_data, matrix_data.data, sizeof(m_3d_matrix_data));
  // TODO
}

void BaseSprite3::render_fake_shadow(DmaFollower& dma) {
  // TODO
  // nop + flushe
  auto nop_flushe = dma.read_and_advance();
  ASSERT(nop_flushe.vifcode0().kind == VifCode::Kind::NOP);
  ASSERT(nop_flushe.vifcode1().kind == VifCode::Kind::FLUSHE);
}
void BaseSprite3::render_2d_group0(DmaFollower& dma,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  setup_graphics_for_2d_group_0_render();

  u16 last_prog = -1;

  while (sprite_common::looks_like_2d_chunk_start(dma)) {
    m_debug_stats.blocks_2d_grp0++;
    // 4 packets per chunk

    // first is the header
    u32 sprite_count = sprite_common::process_sprite_chunk_header(dma);
    m_debug_stats.count_2d_grp0 += sprite_count;

    // second is the vector data
    u32 expected_vec_size = sizeof(SpriteVecData2d) * sprite_count;
    auto vec_data = dma.read_and_advance();
    ASSERT(expected_vec_size <= sizeof(m_vec_data_2d));
    unpack_to_no_stcycl(&m_vec_data_2d, vec_data, VifCode::Kind::UNPACK_V4_32, expected_vec_size,
                        SpriteDataMem::Vector, false, true);

    // third is the adgif data
    u32 expected_adgif_size = sizeof(AdGifData) * sprite_count;
    auto adgif_data = dma.read_and_advance();
    ASSERT(expected_adgif_size <= sizeof(m_adgif));
    unpack_to_no_stcycl(&m_adgif, adgif_data, VifCode::Kind::UNPACK_V4_32, expected_adgif_size,
                        SpriteDataMem::Adgif, false, true);

    // fourth is the actual run!!!!!
    auto run = dma.read_and_advance();
    ASSERT(run.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(run.vifcode1().kind == VifCode::Kind::MSCAL);

    if (m_enabled) {
      if (run.vifcode1().immediate != last_prog) {
        // one-time setups and flushing
        flush_sprites(render_state, prof, false);
      }

      if (run.vifcode1().immediate == SpriteProgMem::Sprites2dGrp0) {
        if (m_2d_enable) {
          do_block_common(SpriteMode::Mode2D, sprite_count, render_state, prof);
        }
      } else {
        if (m_3d_enable) {
          do_block_common(SpriteMode::Mode3D, sprite_count, render_state, prof);
        }
      }
      last_prog = run.vifcode1().immediate;
    }
  }
}

/*!
 * Handle DMA data for group1 2d's (HUD)
 */
void BaseSprite3::render_2d_group1(DmaFollower& dma,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  // one time matrix data upload
  auto mat_upload = dma.read_and_advance();
  bool mat_ok = verify_unpack_with_stcycl(mat_upload, VifCode::Kind::UNPACK_V4_32, 4, 4, 80,
                                          SpriteDataMem::Matrix, false, false);
  ASSERT(mat_ok);
  ASSERT(mat_upload.size_bytes == sizeof(m_hud_matrix_data));
  memcpy(&m_hud_matrix_data, mat_upload.data, sizeof(m_hud_matrix_data));

  // opengl sprite frame setup
  SetupShader(ShaderId::SPRITE3);
  SetSprite3UniformVertexFourFloatVector("hud_hvdf_offset", sizeof(m_hud_matrix_data.hvdf_offset),
                                         m_hud_matrix_data.hvdf_offset.data());
  SetSprite3UniformVertexUserHvdfVector("hud_hvdf_user", sizeof(m_hud_matrix_data.user_hvdf),
                                        m_hud_matrix_data.user_hvdf[0].data());
  SetSprite3UniformMatrixFourFloatVector("hud_matrix", 1, false, m_hud_matrix_data.matrix.data());

  // loop through chunks.
  while (sprite_common::looks_like_2d_chunk_start(dma)) {
    m_debug_stats.blocks_2d_grp1++;
    // 4 packets per chunk

    // first is the header
    u32 sprite_count = sprite_common::process_sprite_chunk_header(dma);
    m_debug_stats.count_2d_grp1 += sprite_count;

    // second is the vector data
    u32 expected_vec_size = sizeof(SpriteVecData2d) * sprite_count;
    auto vec_data = dma.read_and_advance();
    ASSERT(expected_vec_size <= sizeof(m_vec_data_2d));
    unpack_to_no_stcycl(&m_vec_data_2d, vec_data, VifCode::Kind::UNPACK_V4_32, expected_vec_size,
                        SpriteDataMem::Vector, false, true);

    // third is the adgif data
    u32 expected_adgif_size = sizeof(AdGifData) * sprite_count;
    auto adgif_data = dma.read_and_advance();
    ASSERT(expected_adgif_size <= sizeof(m_adgif));
    unpack_to_no_stcycl(&m_adgif, adgif_data, VifCode::Kind::UNPACK_V4_32, expected_adgif_size,
                        SpriteDataMem::Adgif, false, true);

    // fourth is the actual run!!!!!
    auto run = dma.read_and_advance();
    ASSERT(run.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(run.vifcode1().kind == VifCode::Kind::MSCAL);
    switch (render_state->GetVersion()) {
      case GameVersion::Jak1:
        ASSERT(run.vifcode1().immediate == SpriteProgMem::Sprites2dHud_Jak1);
        break;
      case GameVersion::Jak2:
        ASSERT(run.vifcode1().immediate == SpriteProgMem::Sprites2dHud_Jak2);
        break;
      default:
        ASSERT_NOT_REACHED();
    }
    if (m_enabled && m_2d_enable) {
      do_block_common(SpriteMode::ModeHUD, sprite_count, render_state, prof);
    }
  }
}

void BaseSprite3Jak2::render(DmaFollower& dma,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof) {
  m_debug_stats = {};
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0 || data0.vifcode1().kind == VifCode::Kind::NOP);
  ASSERT(data0.vif0() == 0 || data0.vifcode0().kind == VifCode::Kind::MARK);
  ASSERT(data0.size_bytes == 0);

  if (dma.current_tag_offset() == render_state->next_bucket) {
    fmt::print("early exit!");
    return;
  }

  // First is the distorter (temporarily disabled for jak 2)
  {
    // auto child = prof.make_scoped_child("distorter");
    // render_distorter(dma, render_state, child);
  }

  // next, the normal sprite stuff
  SetupShader(ShaderId::SPRITE3);
  handle_sprite_frame_setup(dma, render_state->GetVersion());

  // 3d sprites
  render_3d(dma);

  // 2d draw
  // m_sprite_renderer.reset_state();
  {
    auto child = prof.make_scoped_child("2d-group0");
    render_2d_group0(dma, render_state, child);
    flush_sprites(render_state, prof, false);
  }

  // shadow draw
  render_fake_shadow(dma);

  // 2d draw (HUD)
  {
    auto child = prof.make_scoped_child("2d-group1");
    render_2d_group1(dma, render_state, child);
    flush_sprites(render_state, prof, true);
  }

  EnableSprite3GraphicsBlending();

  {
    auto p = prof.make_scoped_child("glow");
    glow_dma_and_draw(dma, render_state, p);
  }

  // TODO finish this up.
  // fmt::print("next bucket is 0x{}\n", render_state->next_bucket);
  while (dma.current_tag_offset() != render_state->next_bucket) {
    //    auto tag = dma.current_tag();
    // fmt::print("@ 0x{:x} tag: {}", dma.current_tag_offset(), tag.print());
    auto data = dma.read_and_advance();
    VifCode code(data.vif0());
    // fmt::print(" vif0: {}\n", code.print());
    if (code.kind == VifCode::Kind::NOP) {
      // fmt::print(" vif1: {}\n", VifCode(data.vif1()).print());
    }
  }
}

void BaseSprite3Jak1::render(DmaFollower& dma,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof) {
  m_debug_stats = {};
  // First thing should be a NEXT with two nops. this is a jump from buckets to sprite data
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.size_bytes == 0);

  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // sprite renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }

  // First is the distorter
  {
    auto child = prof.make_scoped_child("distorter");
    render_distorter(dma, render_state, child);
  }

  SetupShader(ShaderId::SPRITE3);

  // next, sprite frame setup.
  handle_sprite_frame_setup(dma, render_state->GetVersion());

  // 3d sprites
  render_3d(dma);

  // 2d draw
  // m_sprite_renderer.reset_state();
  {
    auto child = prof.make_scoped_child("2d-group0");
    render_2d_group0(dma, render_state, child);
    flush_sprites(render_state, prof, false);
  }

  // shadow draw
  render_fake_shadow(dma);

  // 2d draw (HUD)
  {
    auto child = prof.make_scoped_child("2d-group1");
    render_2d_group1(dma, render_state, child);
    flush_sprites(render_state, prof, true);
  }

  EnableSprite3GraphicsBlending();

  // TODO finish this up.
  // fmt::print("next bucket is 0x{}\n", render_state->next_bucket);
  while (dma.current_tag_offset() != render_state->next_bucket) {
    //    auto tag = dma.current_tag();
    // fmt::print("@ 0x{:x} tag: {}", dma.current_tag_offset(), tag.print());
    auto data = dma.read_and_advance();
    VifCode code(data.vif0());
    // fmt::print(" vif0: {}\n", code.print());
    if (code.kind == VifCode::Kind::NOP) {
      // fmt::print(" vif1: {}\n", VifCode(data.vif1()).print());
    }
  }
}

void BaseSprite3::draw_debug_window() {
  ImGui::Separator();
  ImGui::Text("Distort sprites: %d", m_distort_stats.total_sprites);
  ImGui::Text("2D Group 0 (World) blocks: %d sprites: %d", m_debug_stats.blocks_2d_grp0,
              m_debug_stats.count_2d_grp0);
  ImGui::Text("2D Group 1 (HUD) blocks: %d sprites: %d", m_debug_stats.blocks_2d_grp1,
              m_debug_stats.count_2d_grp1);
  ImGui::Checkbox("Culling", &m_enable_culling);
  ImGui::Checkbox("2d", &m_2d_enable);
  ImGui::SameLine();
  ImGui::Checkbox("3d", &m_3d_enable);
  ImGui::Checkbox("Distort", &m_distort_enable);
  ImGui::Checkbox("Distort instancing", &m_enable_distort_instancing);
}

void BaseSprite3::handle_tex0(u64 val,
                              BaseSharedRenderState* /*render_state*/,
                              ScopedProfilerNode& /*prof*/) {
  GsTex0 reg(val);

  // update tbp
  m_current_tbp = reg.tbp0();
  m_current_mode.set_tcc(reg.tcc());

  // tbw: assume they got it right
  // psm: assume they got it right
  // tw: assume they got it right
  // th: assume they got it right

  ASSERT(reg.tfx() == GsTex0::TextureFunction::MODULATE);
  ASSERT(reg.psm() != GsTex0::PSM::PSMT4HH);

  // cbp: assume they got it right
  // cpsm: assume they got it right
  // csm: assume they got it right
}

void BaseSprite3::handle_tex1(u64 val,
                              BaseSharedRenderState* /*render_state*/,
                              ScopedProfilerNode& /*prof*/) {
  GsTex1 reg(val);
  m_current_mode.set_filt_enable(reg.mmag());
}

void BaseSprite3::handle_zbuf(u64 val,
                              BaseSharedRenderState* /*render_state*/,
                              ScopedProfilerNode& /*prof*/) {
  // note: we can basically ignore this. There's a single z buffer that's always configured the same
  // way - 24-bit, at offset 448.
  GsZbuf x(val);
  ASSERT(x.psm() == TextureFormat::PSMZ24);
  ASSERT(x.zbp() == 448 || x.zbp() == 304);  // 304 for jak 2.

  m_current_mode.set_depth_write_enable(!x.zmsk());
}

void BaseSprite3::handle_clamp(u64 val,
                               BaseSharedRenderState* /*render_state*/,
                               ScopedProfilerNode& /*prof*/) {
  if (!(val == 0b101 || val == 0 || val == 1 || val == 0b100)) {
    ASSERT_MSG(false, fmt::format("clamp: 0x{:x}", val));
  }

  m_current_mode.set_clamp_s_enable(val & 0b001);
  m_current_mode.set_clamp_t_enable(val & 0b100);
}

void BaseSprite3::update_mode_from_alpha1(u64 val, DrawMode& mode) {
  GsAlpha reg(val);
  if (reg.a_mode() == GsAlpha::BlendMode::SOURCE && reg.b_mode() == GsAlpha::BlendMode::DEST &&
      reg.c_mode() == GsAlpha::BlendMode::SOURCE && reg.d_mode() == GsAlpha::BlendMode::DEST) {
    // (Cs - Cd) * As + Cd
    // Cs * As  + (1 - As) * Cd
    mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_SRC_DST);

  } else if (reg.a_mode() == GsAlpha::BlendMode::SOURCE &&
             reg.b_mode() == GsAlpha::BlendMode::ZERO_OR_FIXED &&
             reg.c_mode() == GsAlpha::BlendMode::SOURCE &&
             reg.d_mode() == GsAlpha::BlendMode::DEST) {
    // (Cs - 0) * As + Cd
    // Cs * As + (1) * CD
    mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_0_SRC_DST);
  } else if (reg.a_mode() == GsAlpha::BlendMode::SOURCE &&
             reg.b_mode() == GsAlpha::BlendMode::ZERO_OR_FIXED &&
             reg.c_mode() == GsAlpha::BlendMode::ZERO_OR_FIXED &&
             reg.d_mode() == GsAlpha::BlendMode::DEST) {
    ASSERT(reg.fix() == 128);
    // Cv = (Cs - 0) * FIX + Cd
    // if fix = 128, it works out to 1.0
    mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_0_FIX_DST);
    // src plus dest
  } else if (reg.a_mode() == GsAlpha::BlendMode::SOURCE &&
             reg.b_mode() == GsAlpha::BlendMode::DEST &&
             reg.c_mode() == GsAlpha::BlendMode::ZERO_OR_FIXED &&
             reg.d_mode() == GsAlpha::BlendMode::DEST) {
    // Cv = (Cs - Cd) * FIX + Cd
    ASSERT(reg.fix() == 64);
    mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_FIX_DST);
  } else if (reg.a_mode() == GsAlpha::BlendMode::ZERO_OR_FIXED &&
             reg.b_mode() == GsAlpha::BlendMode::SOURCE &&
             reg.c_mode() == GsAlpha::BlendMode::SOURCE &&
             reg.d_mode() == GsAlpha::BlendMode::DEST) {
    // (0 - Cs) * As + Cd
    // Cd - Cs * As
    // s, d
    mode.set_alpha_blend(DrawMode::AlphaBlend::ZERO_SRC_SRC_DST);
  }

  else {
    lg::error("unsupported blend: a {} b {} c {} d {}", (int)reg.a_mode(), (int)reg.b_mode(),
              (int)reg.c_mode(), (int)reg.d_mode());
    mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_SRC_DST);
    ASSERT(false);
  }
}

void BaseSprite3::handle_alpha(u64 val,
                               BaseSharedRenderState* /*render_state*/,
                               ScopedProfilerNode& /*prof*/) {
  update_mode_from_alpha1(val, m_current_mode);
}

void BaseSprite3::do_block_common(SpriteMode mode,
                                  u32 count,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  m_current_mode = m_default_mode;
  for (u32 sprite_idx = 0; sprite_idx < count; sprite_idx++) {
    if (m_sprite_idx == SPRITE_RENDERER_MAX_SPRITES) {
      flush_sprites(render_state, prof, mode == ModeHUD);
    }

    if (mode == Mode2D && render_state->has_pc_data && m_enable_culling) {
      // we can skip sprites that are out of view
      // it's probably possible to do this for 3D as well.
      auto bsphere = m_vec_data_2d[sprite_idx].xyz_sx;
      bsphere.w() = std::max(bsphere.w(), m_vec_data_2d[sprite_idx].sy());
      if (bsphere.w() == 0 ||
          !background_common::sphere_in_view_ref(bsphere, render_state->camera_planes)) {
        continue;
      }
    }

    auto& adgif = m_adgif[sprite_idx];
    handle_tex0(adgif.tex0_data, render_state, prof);
    handle_tex1(adgif.tex1_data, render_state, prof);
    if (GsRegisterAddress(adgif.clamp_addr) == GsRegisterAddress::ZBUF_1) {
      handle_zbuf(adgif.clamp_data, render_state, prof);
    } else {
      handle_clamp(adgif.clamp_data, render_state, prof);
    }
    handle_alpha(adgif.alpha_data, render_state, prof);

    u64 key = (((u64)m_current_tbp) << 32) | m_current_mode.as_int();
    Bucket* bucket;
    if (key == m_last_bucket_key) {
      bucket = m_last_bucket;
    } else {
      auto it = m_sprite_buckets.find(key);
      if (it == m_sprite_buckets.end()) {
        bucket = &m_sprite_buckets[key];
        bucket->key = key;
        m_bucket_list.push_back(bucket);
      } else {
        bucket = &it->second;
      }
    }
    u32 start_vtx_id = m_sprite_idx * 4;
    bucket->ids.push_back(start_vtx_id);
    bucket->ids.push_back(start_vtx_id + 1);
    bucket->ids.push_back(start_vtx_id + 2);
    bucket->ids.push_back(start_vtx_id + 3);
    bucket->ids.push_back(UINT32_MAX);

    auto& vert1 = m_vertices_3d.at(start_vtx_id + 0);

    vert1.xyz_sx = m_vec_data_2d[sprite_idx].xyz_sx;
    vert1.quat_sy = m_vec_data_2d[sprite_idx].flag_rot_sy;
    vert1.rgba = m_vec_data_2d[sprite_idx].rgba / 255;
    vert1.flags_matrix[0] = m_vec_data_2d[sprite_idx].flag();
    vert1.flags_matrix[1] = m_vec_data_2d[sprite_idx].matrix();
    vert1.info[0] = 0;  // hack
    vert1.info[1] = m_current_mode.get_tcc_enable();
    vert1.info[2] = 0;
    vert1.info[3] = mode;

    m_vertices_3d.at(start_vtx_id + 1) = vert1;
    m_vertices_3d.at(start_vtx_id + 2) = vert1;
    m_vertices_3d.at(start_vtx_id + 3) = vert1;

    m_vertices_3d.at(start_vtx_id + 1).info[2] = 1;
    m_vertices_3d.at(start_vtx_id + 2).info[2] = 3;
    m_vertices_3d.at(start_vtx_id + 3).info[2] = 2;

    ++m_sprite_idx;
  }
}
