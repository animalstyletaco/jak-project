

#include "Sprite3.h"

#include "common/log/log.h"

#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/general_renderer/dma_helpers.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

constexpr int SPRITE_RENDERER_MAX_SPRITES = 1920 * 10;
constexpr int SPRITE_RENDERER_MAX_DISTORT_SPRITES =
    256 * 10;  // size of sprite-aux-list in GOAL code * SPRITE_MAX_AMOUNT_MULT

BaseSprite3::BaseSprite3(const std::string& name, int my_id)
    : BaseBucketRenderer(name, my_id) {
  graphics_setup();
}

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
 * Run the sprite distorter.
 */
void BaseSprite3::render_distorter(DmaFollower& dma,
                               BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof) {
  // Skip to distorter DMA
  direct_renderer_reset_state();
  while (dma.current_tag().qwc != 7) {
    auto direct_data = dma.read_and_advance();
    direct_renderer_render_vif(direct_data.vif0(), direct_data.vif1(), direct_data.data,
                        direct_data.size_bytes, render_state, prof);
  }
  direct_renderer_flush_pending(render_state, prof);

  // Read DMA
  {
    auto prof_node = prof.make_scoped_child("dma");
    distort_dma(dma, prof_node);
  }

  if (!m_enabled || !m_distort_enable) {
    // Distort disabled, we can stop here since all the DMA has been read
    return;
  }

  // Set up vertex data
  {
    auto prof_node = prof.make_scoped_child("setup");
    if (m_enable_distort_instancing) {
      distort_setup_instanced(prof_node);
    } else {
      distort_setup(prof_node);
    }
  }

  // Draw
  {
    auto prof_node = prof.make_scoped_child("drawing");
    if (m_enable_distort_instancing) {
      distort_draw_instanced(render_state, prof_node);
    } else {
      distort_draw(render_state, prof_node);
    }
  }
}

/*!
 * Reads all sprite distort related DMA packets.
 */
void BaseSprite3::distort_dma(DmaFollower& dma, ScopedProfilerNode& /*prof*/) {
  // First should be the GS setup
  auto sprite_distorter_direct_setup = dma.read_and_advance();
  ASSERT(sprite_distorter_direct_setup.vifcode0().kind == VifCode::Kind::NOP);
  ASSERT(sprite_distorter_direct_setup.vifcode1().kind == VifCode::Kind::DIRECT);
  ASSERT(sprite_distorter_direct_setup.vifcode1().immediate == 7);
  memcpy(&m_sprite_distorter_setup, sprite_distorter_direct_setup.data, 7 * 16);

  auto gif_tag = m_sprite_distorter_setup.gif_tag;
  ASSERT(gif_tag.nloop() == 1);
  ASSERT(gif_tag.eop() == 1);
  ASSERT(gif_tag.nreg() == 6);
  ASSERT(gif_tag.reg(0) == GifTag::RegisterDescriptor::AD);

  auto zbuf1 = m_sprite_distorter_setup.zbuf;
  ASSERT(zbuf1.zbp() == 0x1c0);
  ASSERT(zbuf1.zmsk() == true);
  ASSERT(zbuf1.psm() == TextureFormat::PSMZ24);

  auto tex0 = m_sprite_distorter_setup.tex0;
  ASSERT(tex0.tbw() == 8);
  ASSERT(tex0.tw() == 9);
  ASSERT(tex0.th() == 8);

  auto tex1 = m_sprite_distorter_setup.tex1;
  ASSERT(tex1.mmag() == true);
  ASSERT(tex1.mmin() == 1);

  auto alpha = m_sprite_distorter_setup.alpha;
  ASSERT(alpha.a_mode() == GsAlpha::BlendMode::SOURCE);
  ASSERT(alpha.b_mode() == GsAlpha::BlendMode::DEST);
  ASSERT(alpha.c_mode() == GsAlpha::BlendMode::SOURCE);
  ASSERT(alpha.d_mode() == GsAlpha::BlendMode::DEST);

  // Next is the aspect used by the sine tables (PC only)
  //
  // This was added to let the renderer reliably detect when the sine tables changed,
  // which is whenever the aspect ratio changed. However, the tables aren't always
  // updated on the same frame that the aspect changed, so this just lets the game
  // easily notify the renderer when it finally does get updated.
  auto sprite_distort_tables_aspect = dma.read_and_advance();
  ASSERT(sprite_distort_tables_aspect.size_bytes == 16);
  ASSERT(sprite_distort_tables_aspect.vifcode1().kind == VifCode::Kind::PC_PORT);
  memcpy(&m_sprite_distorter_sine_tables_aspect, sprite_distort_tables_aspect.data,
         sizeof(math::Vector4f));

  // Next thing should be the sine tables
  auto sprite_distorter_tables = dma.read_and_advance();
  unpack_to_stcycl(&m_sprite_distorter_sine_tables, sprite_distorter_tables,
                   VifCode::Kind::UNPACK_V4_32, 4, 4, 0x8b * 16, 0x160, false, false);

  ASSERT(GsPrim(m_sprite_distorter_sine_tables.gs_gif_tag.prim()).kind() ==
         GsPrim::Kind::TRI_STRIP);

  // Finally, should be frame data packets (containing sprites)
  // Up to 170 sprites will be DMA'd at a time followed by a mscalf,
  // and this process can happen twice up to a maximum of 256 sprites DMA'd
  // (256 is the size of sprite-aux-list which drives this).
  int sprite_idx = 0;
  m_distort_stats.total_sprites = 0;

  while (sprite_common::looks_like_distort_frame_data(dma)) {
    math::Vector<u32, 4> num_sprites_vec;

    // Read sprite packets
    do {
      int qwc = dma.current_tag().qwc;
      int dest = dma.current_tag_vifcode1().immediate;
      auto distort_data = dma.read_and_advance();

      if (dest == 511) {
        // VU address 511 specifies the number of sprites
        unpack_to_no_stcycl(&num_sprites_vec, distort_data, VifCode::Kind::UNPACK_V4_32, 16, dest,
                            false, false);
      } else {
        // VU address >= 512 is the actual vertex data
        ASSERT(dest >= 512);
        ASSERT(sprite_idx + (qwc / 3) <= (int)m_sprite_distorter_frame_data.capacity());

        unpack_to_no_stcycl(&m_sprite_distorter_frame_data.at(sprite_idx), distort_data,
                            VifCode::Kind::UNPACK_V4_32, qwc * 16, dest, false, false);

        sprite_idx += qwc / 3;
      }
    } while (sprite_common::looks_like_distort_frame_data(dma));

    // Sprite packets should always end with a mscalf flush
    ASSERT(dma.current_tag().kind == DmaTag::Kind::CNT);
    ASSERT(dma.current_tag_vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(dma.current_tag_vifcode1().kind == VifCode::Kind::FLUSH);
    dma.read_and_advance();

    m_distort_stats.total_sprites += num_sprites_vec.x();
  }

  // Done
  ASSERT(m_distort_stats.total_sprites <= SPRITE_RENDERER_MAX_DISTORT_SPRITES);
}

/*!
 * Sets up OpenGL data for each distort sprite.
 */
void BaseSprite3::distort_setup(ScopedProfilerNode& /*prof*/) {
  m_distort_stats.total_tris = 0;

  m_sprite_distorter_vertices.clear();
  m_sprite_distorter_indices.clear();

  int sprite_idx = 0;
  int sprites_left = m_distort_stats.total_sprites;

  // This part is mostly ripped from the VU program
  while (sprites_left != 0) {
    // flag seems to represent the 'resolution' of the circle sprite used to create the distortion
    // effect For example, a flag value of 3 will create a circle using 3 "pie-slice" shapes
    u32 flag = m_sprite_distorter_frame_data.at(sprite_idx).flag;
    u32 slices_left = flag;

    // flag has a minimum value of 3 which represents the first ientry
    // Additionally, the ientry index has 352 added to it (which is the start of the entry array
    // in VU memory), so we need to subtract that as well
    int entry_index = m_sprite_distorter_sine_tables.ientry[flag - 3].x() - 352;

    // Here would be adding the giftag, but we don't need that

    // Get the frame data for the next distort sprite
    SpriteDistortFrameData frame_data = m_sprite_distorter_frame_data.at(sprite_idx);
    sprite_idx++;

    // Build the OpenGL data for the sprite
    math::Vector2f vf03 = frame_data.st;
    math::Vector3f vf14 = frame_data.xyz;

    // Each slice shares a center vertex, we can use this fact and cut out duplicate vertices
    u32 center_vert_idx = m_sprite_distorter_vertices.size();
    m_sprite_distorter_vertices.push_back({vf14, vf03});

    do {
      math::Vector3f vf06 = m_sprite_distorter_sine_tables.entry[entry_index++].xyz();
      math::Vector2f vf07 = m_sprite_distorter_sine_tables.entry[entry_index++].xy();
      math::Vector3f vf08 = m_sprite_distorter_sine_tables.entry[entry_index + 0].xyz();
      math::Vector2f vf09 = m_sprite_distorter_sine_tables.entry[entry_index + 1].xy();

      slices_left--;

      math::Vector2f vf11 = (vf07 * frame_data.rgba.z()) + frame_data.st;
      math::Vector2f vf13 = (vf09 * frame_data.rgba.z()) + frame_data.st;
      math::Vector3f vf06_2 = (vf06 * frame_data.rgba.x()) + frame_data.xyz;
      math::Vector2f vf07_2 = (vf07 * frame_data.rgba.x()) + frame_data.st;
      math::Vector3f vf08_2 = (vf08 * frame_data.rgba.x()) + frame_data.xyz;
      math::Vector2f vf09_2 = (vf09 * frame_data.rgba.x()) + frame_data.st;
      math::Vector3f vf10 = (vf06 * frame_data.rgba.y()) + frame_data.xyz;
      math::Vector3f vf12 = (vf08 * frame_data.rgba.y()) + frame_data.xyz;
      math::Vector3f vf06_3 = vf06_2;
      math::Vector3f vf08_3 = vf08_2;

      m_sprite_distorter_indices.push_back(m_sprite_distorter_vertices.size());
      m_sprite_distorter_vertices.push_back({vf06_3, vf07_2});

      m_sprite_distorter_indices.push_back(m_sprite_distorter_vertices.size());
      m_sprite_distorter_vertices.push_back({vf08_3, vf09_2});

      m_sprite_distorter_indices.push_back(m_sprite_distorter_vertices.size());
      m_sprite_distorter_vertices.push_back({vf10, vf11});

      m_sprite_distorter_indices.push_back(m_sprite_distorter_vertices.size());
      m_sprite_distorter_vertices.push_back({vf12, vf13});

      // Originally, would add the shared center vertex, but in our case we can just add the index
      m_sprite_distorter_indices.push_back(center_vert_idx);
      // m_sprite_distorter_vertices.push_back({vf14, vf03});

      m_distort_stats.total_tris += 2;
    } while (slices_left != 0);

    // Mark the end of the triangle strip
    m_sprite_distorter_indices.push_back(UINT32_MAX);

    sprites_left--;
  }
}

/*!
 * Sets up OpenGL data for rendering distort sprites using instanced rendering.
 *
 * A mesh is built once for each possible sprite resolution and is only re-built
 * when the dimensions of the window are changed. These meshes are built just like
 * the triangle strips in the VU program, but with the sprite-specific data removed.
 *
 * Required sprite-specific frame data is kept as is and is grouped by resolution.
 */
void BaseSprite3::distort_setup_instanced(ScopedProfilerNode& /*prof*/) {
  if (m_distort_instanced_ogl.last_aspect_x != m_sprite_distorter_sine_tables_aspect.x() ||
      m_distort_instanced_ogl.last_aspect_y != m_sprite_distorter_sine_tables_aspect.y()) {
    m_distort_instanced_ogl.last_aspect_x = m_sprite_distorter_sine_tables_aspect.x();
    m_distort_instanced_ogl.last_aspect_y = m_sprite_distorter_sine_tables_aspect.y();
    // Aspect ratio changed, which means we have a new sine table
    m_sprite_distorter_vertices_instanced.clear();

    // Build a mesh for every possible distort sprite resolution
    auto vf03 = math::Vector2f(0, 0);
    auto vf14 = math::Vector3f(0, 0, 0);

    for (int res = 3; res < 12; res++) {
      int entry_index = m_sprite_distorter_sine_tables.ientry[res - 3].x() - 352;

      for (int i = 0; i < res; i++) {
        math::Vector3f vf06 = m_sprite_distorter_sine_tables.entry[entry_index++].xyz();
        math::Vector2f vf07 = m_sprite_distorter_sine_tables.entry[entry_index++].xy();
        math::Vector3f vf08 = m_sprite_distorter_sine_tables.entry[entry_index + 0].xyz();
        math::Vector2f vf09 = m_sprite_distorter_sine_tables.entry[entry_index + 1].xy();

        // Normally, there would be a bunch of transformations here against the sprite data.
        // Instead, we'll let the shader do it and just store the sine table specific parts here.

        m_sprite_distorter_vertices_instanced.push_back({vf06, vf07});
        m_sprite_distorter_vertices_instanced.push_back({vf08, vf09});
        m_sprite_distorter_vertices_instanced.push_back({vf06, vf07});
        m_sprite_distorter_vertices_instanced.push_back({vf08, vf09});
        m_sprite_distorter_vertices_instanced.push_back({vf14, vf03});
      }
    }

    m_distort_instanced_ogl.vertex_data_changed = true;
  }

  // Set up instance data for each sprite
  m_distort_stats.total_tris = 0;

  for (auto& [res, vec] : m_sprite_distorter_instances_by_res) {
    vec.clear();
  }

  for (int i = 0; i < m_distort_stats.total_sprites; i++) {
    SpriteDistortFrameData frame_data = m_sprite_distorter_frame_data.at(i);

    // Shader just needs the position, tex coords, and scale
    auto x_y_z_s = math::Vector4f(frame_data.xyz.x(), frame_data.xyz.y(), frame_data.xyz.z(),
                                  frame_data.st.x());
    auto sx_sy_sz_t = math::Vector4f(frame_data.rgba.x(), frame_data.rgba.y(), frame_data.rgba.z(),
                                     frame_data.st.y());

    int res = frame_data.flag;

    m_sprite_distorter_instances_by_res[res].push_back({x_y_z_s, sx_sy_sz_t});

    m_distort_stats.total_tris += res * 2;
  }
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
  SetSprite3UniformVertexFourFloatVector("hud_hvdf_user", sizeof(m_hud_matrix_data.user_hvdf),
                                         m_hud_matrix_data.user_hvdf[0].data());
  SetSprite3UniformMatrixFourFloatVector("hud_matrix", sizeof(m_hud_matrix_data.matrix), false,
                                         m_hud_matrix_data.matrix.data());

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
    ASSERT(run.vifcode1().immediate == SpriteProgMem::Sprites2dHud);
    if (m_enabled && m_2d_enable) {
      do_block_common(SpriteMode::ModeHUD, sprite_count, render_state, prof);
    }
  }
}

void BaseSprite3::render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  switch (render_state->version) {
    case GameVersion::Jak1:
      render_jak1(dma, render_state, prof);
      break;
    case GameVersion::Jak2:
      render_jak2(dma, render_state, prof);
      break;
    default:
      ASSERT_NOT_REACHED();
  }
}

void BaseSprite3::render_jak2(DmaFollower& dma,
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
  handle_sprite_frame_setup(dma, render_state->version);

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

void BaseSprite3::render_jak1(DmaFollower& dma,
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
  handle_sprite_frame_setup(dma, render_state->version);

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
      if (bsphere.w() == 0 || !background_common::sphere_in_view_ref(bsphere, render_state->camera_planes)) {
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
