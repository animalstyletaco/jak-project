#include "DirectRenderer.h"

#include "common/dma/gs.h"
#include "common/log/log.h"
#include "common/util/Assert.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

BaseDirectRenderer::BaseDirectRenderer(const std::string& name, int my_id, int batch_size)
    : BaseBucketRenderer(name, my_id), m_prim_buffer(batch_size) {
}

BaseDirectRenderer::~BaseDirectRenderer() {
}

/*!
 * Render from a DMA bucket.
 */
void BaseDirectRenderer::render(DmaFollower& dma,
                            BaseSharedRenderState* render_state,
                            ScopedProfilerNode& prof) {
  // if we're rendering from a bucket, we should start off we a totally reset state:
  reset_state();
  setup_common_state(render_state);

  // just dump the DMA data into the other the render function
  while (dma.current_tag_offset() != render_state->next_bucket) {
    auto data = dma.read_and_advance();
    if (data.size_bytes && m_enabled) {
      render_vif(data.vif0(), data.vif1(), data.data, data.size_bytes, render_state, prof);
    }

    if (dma.current_tag_offset() == render_state->default_regs_buffer) {
      //      reset_state();
      dma.read_and_advance();  // cnt
      ASSERT(dma.current_tag().kind == DmaTag::Kind::RET);
      dma.read_and_advance();  // ret
    }
  }

  if (m_enabled) {
    flush_pending(render_state, prof);
  }
}

void BaseDirectRenderer::reset_state() {
  m_test_state_needs_graphics_update = true;
  m_test_state = TestState();

  m_blend_state_needs_graphics_update = true;
  m_blend_state = BlendState();

  m_prim_graphics_state_needs_graphics_update = true;
  m_prim_graphics_state = PrimGlState();

  for (int i = 0; i < TEXTURE_STATE_COUNT; ++i) {
    m_buffered_tex_state[i] = TextureState();
  }
  m_tex_state_from_reg = {};
  m_next_free_tex_state = 0;
  m_current_tex_state_idx = -1;

  m_prim_building = PrimBuildState();

  m_stats = {};
}

void BaseDirectRenderer::draw_debug_window() {
  ImGui::Checkbox("Wireframe", &m_debug_state.wireframe);
  ImGui::SameLine();
  ImGui::Checkbox("No-texture", &m_debug_state.disable_texture);
  ImGui::SameLine();
  ImGui::Checkbox("red", &m_debug_state.red);
  ImGui::SameLine();
  ImGui::Checkbox("always", &m_debug_state.always_draw);
  ImGui::SameLine();
  ImGui::Checkbox("no mip", &m_debug_state.disable_mipmap);

  ImGui::Text("Triangles: %d", m_stats.triangles);
  ImGui::SameLine();
  ImGui::Text("Draws: %d", m_stats.draw_calls);

  ImGui::Text("Flush from state change:");
  ImGui::Text("  tex0: %d", m_stats.flush_from_tex_0);
  ImGui::Text("  tex1: %d", m_stats.flush_from_tex_1);
  ImGui::Text("  zbuf: %d", m_stats.flush_from_zbuf);
  ImGui::Text("  test: %d", m_stats.flush_from_test);
  ImGui::Text("  alph: %d", m_stats.flush_from_alpha);
  ImGui::Text("  clmp: %d", m_stats.flush_from_clamp);
  ImGui::Text("  prim: %d", m_stats.flush_from_prim);
  ImGui::Text("  texstate: %d", m_stats.flush_from_state_exhaust);
  ImGui::Text(" Total: %d/%d",
              m_stats.flush_from_prim + m_stats.flush_from_clamp + m_stats.flush_from_alpha +
                  m_stats.flush_from_test + m_stats.flush_from_zbuf + m_stats.flush_from_tex_1 +
                  m_stats.flush_from_tex_0 + m_stats.flush_from_state_exhaust,
              m_stats.draw_calls);
}

float u32_to_float(u32 in) {
  double x = (double)in / UINT32_MAX;
  return x;
}

float u32_to_sc(u32 in) {
  float flt = u32_to_float(in);
  return (flt - 0.5) * 16.0;
}

void BaseDirectRenderer::flush_pending(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  // update opengl state
  if (m_blend_state_needs_graphics_update) {
    update_graphics_blend();
    m_blend_state_needs_graphics_update = false;
  }

  if (m_prim_graphics_state_needs_graphics_update) {
    update_graphics_prim(render_state);
    m_prim_graphics_state_needs_graphics_update = false;
  }

  if (m_test_state_needs_graphics_update) {
    update_graphics_test();
    m_test_state_needs_graphics_update = false;
  }

  for (int i = 0; i < TEXTURE_STATE_COUNT; i++) {
    auto& tex_state = m_buffered_tex_state[i];
    if (tex_state.used) {
      update_graphics_texture(render_state, i);
      tex_state.used = false;
    }
  }
  m_next_free_tex_state = 0;
  m_current_tex_state_idx = -1;

  render_and_draw_buffers();
}

void BaseDirectRenderer::setup_common_state(BaseSharedRenderState* /*render_state*/) {
  // todo texture clamp.
}

namespace {
/*!
 * If it's a direct, returns the qwc.
 * If it's ignorable (nop, flush), returns 0.
 * Otherwise, assert.
 */
u32 get_direct_qwc_or_nop(const VifCode& code) {
  switch (code.kind) {
    case VifCode::Kind::NOP:
    case VifCode::Kind::FLUSHA:
      return 0;
    case VifCode::Kind::DIRECT:
      if (code.immediate == 0) {
        return 65536;
      } else {
        return code.immediate;
      }
    default:
      ASSERT(false);
  }
}
}  // namespace

/*!
 * Render VIF data.
 */
void BaseDirectRenderer::render_vif(u32 vif0,
                                u32 vif1,
                                const u8* data,
                                u32 size,
                                BaseSharedRenderState* render_state,
                                ScopedProfilerNode& prof) {
  // here we process VIF data. Basically we just go forward, looking for DIRECTs.
  // We skip stuff like flush and nops.

  // read the vif cmds at the front.
  u32 gif_qwc = get_direct_qwc_or_nop(VifCode(vif0));
  if (gif_qwc) {
    // we got a direct. expect the second thing to be a nop/similar.
    ASSERT(get_direct_qwc_or_nop(VifCode(vif1)) == 0);
  } else {
    gif_qwc = get_direct_qwc_or_nop(VifCode(vif1));
  }

  u32 offset_into_data = 0;
  while (offset_into_data < size) {
    if (gif_qwc) {
      if (offset_into_data & 0xf) {
        // not aligned. should get nops.
        u32 vif;
        memcpy(&vif, data + offset_into_data, 4);
        offset_into_data += 4;
        ASSERT(get_direct_qwc_or_nop(VifCode(vif)) == 0);
      } else {
        // aligned! do a gif transfer!
        render_gif(data + offset_into_data, gif_qwc * 16, render_state, prof);
        offset_into_data += gif_qwc * 16;
      }
    } else {
      // we are reading VIF data.
      u32 vif;
      memcpy(&vif, data + offset_into_data, 4);
      offset_into_data += 4;
      gif_qwc = get_direct_qwc_or_nop(VifCode(vif));
    }
  }
}

/*!
 * Render GIF data.
 */
void BaseDirectRenderer::render_gif(const u8* data,
                                u32 size,
                                BaseSharedRenderState* render_state,
                                ScopedProfilerNode& prof) {
  if (size != UINT32_MAX) {
    ASSERT(size >= 16);
  }

  bool eop = false;

  u32 offset = 0;
  while (!eop) {
    if (size != UINT32_MAX) {
      ASSERT(offset < size);
    }
    GifTag tag(data + offset);
    offset += 16;

    // unpack registers.
    // faster to do it once outside of the nloop loop.
    GifTag::RegisterDescriptor reg_desc[16];
    u32 nreg = tag.nreg();
    for (u32 i = 0; i < nreg; i++) {
      reg_desc[i] = tag.reg(i);
    }

    auto format = tag.flg();
    if (format == GifTag::Format::PACKED) {
      if (tag.pre()) {
        handle_prim(tag.prim(), render_state, prof);
      }
      for (u32 loop = 0; loop < tag.nloop(); loop++) {
        for (u32 reg = 0; reg < nreg; reg++) {
          // fmt::print("{}\n", reg_descriptor_name(reg_desc[reg]));
          switch (reg_desc[reg]) {
            case GifTag::RegisterDescriptor::AD:
              handle_ad(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::ST:
              handle_st_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::RGBAQ:
              handle_rgbaq_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              handle_xyzf2_packed(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::XYZ2:
              handle_xyz2_packed(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::PRIM:
              handle_prim_packed(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::TEX0_1:
              handle_tex0_1_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::UV:
              handle_uv_packed(data + offset);
              break;
            default:
              ASSERT_MSG(false, fmt::format("Register {} is not supported in packed mode yet\n",
                                            reg_descriptor_name(reg_desc[reg])));
          }
          offset += 16;  // PACKED = quadwords
        }
      }
    } else if (format == GifTag::Format::REGLIST) {
      for (u32 loop = 0; loop < tag.nloop(); loop++) {
        for (u32 reg = 0; reg < nreg; reg++) {
          u64 register_data;
          memcpy(&register_data, data + offset, 8);
          // fmt::print("loop: {} reg: {} {}\n", loop, reg, reg_descriptor_name(reg_desc[reg]));
          switch (reg_desc[reg]) {
            case GifTag::RegisterDescriptor::PRIM:
              handle_prim(register_data, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::RGBAQ:
              handle_rgbaq(register_data);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              handle_xyzf2(register_data, render_state, prof);
              break;
            default:
              ASSERT_MSG(false, fmt::format("Register {} is not supported in reglist mode yet\n",
                                            reg_descriptor_name(reg_desc[reg])));
          }
          offset += 8;  // PACKED = quadwords
        }
      }
    } else {
      ASSERT(false);  // format not packed or reglist.
    }

    eop = tag.eop();
  }

  if (size != UINT32_MAX) {
    if ((offset + 15) / 16 != size / 16) {
      ASSERT_MSG(false, fmt::format("BaseDirectRenderer size failed in {}. expected: {}, got: {}",
                                    name_and_id(), size, offset));
    }
  }

  //  fmt::print("{}\n", GifTag(data).print());
}

void BaseDirectRenderer::handle_ad(const u8* data,
                               BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof) {
  u64 value;
  GsRegisterAddress addr;
  memcpy(&value, data, sizeof(u64));
  memcpy(&addr, data + 8, sizeof(GsRegisterAddress));

  // fmt::print("{}\n", register_address_name(addr));
  switch (addr) {
    case GsRegisterAddress::ZBUF_1:
      handle_zbuf1(value, render_state, prof);
      break;
    case GsRegisterAddress::TEST_1:
      handle_test1(value, render_state, prof);
      break;
    case GsRegisterAddress::ALPHA_1:
      handle_alpha1(value, render_state, prof);
      break;
    case GsRegisterAddress::PABE:
      handle_pabe(value);
      break;
    case GsRegisterAddress::CLAMP_1:
      handle_clamp1(value);
      break;
    case GsRegisterAddress::PRIM:
      handle_prim(value, render_state, prof);
      break;

    case GsRegisterAddress::TEX1_1:
      handle_tex1_1(value);
      break;
    case GsRegisterAddress::TEXA:
      handle_texa(value);
      break;
    case GsRegisterAddress::TEXCLUT:
      // TODO
      // the only thing the direct renderer does with texture is font, which does no tricks with
      // CLUT. The texture upload process will do all of the lookups with the default CLUT.
      // So we'll just assume that the TEXCLUT is set properly and ignore this.
      break;
    case GsRegisterAddress::FOGCOL:
      // TODO
      break;
    case GsRegisterAddress::TEX0_1:
      handle_tex0_1(value);
      break;
    case GsRegisterAddress::MIPTBP1_1:
    case GsRegisterAddress::MIPTBP2_1:
      // TODO this has the address of different mip levels.
      break;
    case GsRegisterAddress::TEXFLUSH:
      break;
    case GsRegisterAddress::FRAME_1:
      break;
    case GsRegisterAddress::RGBAQ:
      // shadow scissor does this?
      {
        m_prim_building.rgba_reg[0] = data[0];
        m_prim_building.rgba_reg[1] = data[1];
        m_prim_building.rgba_reg[2] = data[2];
        m_prim_building.rgba_reg[3] = data[3];
        memcpy(&m_prim_building.Q, data + 4, 4);
      }
      break;
    default:
      ASSERT_MSG(false, fmt::format("Address {} is not supported", register_address_name(addr)));
  }
}

void BaseDirectRenderer::handle_tex1_1(u64 val) {
  GsTex1 reg(val);
  // for now, we aren't going to handle mipmapping. I don't think it's used with direct.
  //   ASSERT(reg.mxl() == 0);
  // if that's true, we can ignore LCM, MTBA, L, K

  bool want_tex_filt = reg.mmag();
  if (want_tex_filt != m_tex_state_from_reg.enable_tex_filt) {
    m_tex_state_from_reg.enable_tex_filt = want_tex_filt;
    // we changed the state_from_reg, we no longer know if it points to a texture state.
    m_current_tex_state_idx = -1;
  }

  // MMAG/MMIN specify texture filtering. For now, assume always linear
  //  ASSERT(reg.mmag() == true);
  //  if (!(reg.mmin() == 1 || reg.mmin() == 4)) {  // with mipmap off, both of these are linear
  //                                                //    lg::error("unsupported mmin");
  //  }
}

void BaseDirectRenderer::handle_tex0_1_packed(const u8* data) {
  u64 val;
  memcpy(&val, data, sizeof(u64));
  handle_tex0_1(val);
}

void BaseDirectRenderer::handle_tex0_1(u64 val) {
  GsTex0 reg(val);
  // update tbp
  if (m_tex_state_from_reg.current_register != reg) {
    m_tex_state_from_reg.texture_base_ptr = reg.tbp0();
    m_tex_state_from_reg.using_mt4hh = reg.psm() == GsTex0::PSM::PSMT4HH;
    m_tex_state_from_reg.current_register = reg;
    m_tex_state_from_reg.tcc = reg.tcc();
    m_tex_state_from_reg.decal = reg.tfx() == GsTex0::TextureFunction::DECAL;
    ASSERT(reg.tfx() == GsTex0::TextureFunction::DECAL ||
           reg.tfx() == GsTex0::TextureFunction::MODULATE);

    // we changed the state_from_reg, we no longer know if it points to a texture state.
    m_current_tex_state_idx = -1;
  }

  // tbw: assume they got it right
  // psm: assume they got it right
  // tw: assume they got it right
  // th: assume they got it right

  // MERC hack
  // ASSERT(reg.tfx() == GsTex0::TextureFunction::MODULATE);

  // cbp: assume they got it right
  // cpsm: assume they got it right
  // csm: assume they got it right
}

void BaseDirectRenderer::handle_texa(u64 val) {
  GsTexa reg(val);

  // rgba16 isn't used so this doesn't matter?
  // but they use sane defaults anyway
  ASSERT(reg.ta0() == 0);
  ASSERT(reg.ta1() == 0x80);  // note: check rgba16_to_rgba32 if this changes.

  ASSERT(reg.aem() == false);
}

void BaseDirectRenderer::handle_st_packed(const u8* data) {
  memcpy(&m_prim_building.st_reg.x(), data + 0, 4);
  memcpy(&m_prim_building.st_reg.y(), data + 4, 4);
  memcpy(&m_prim_building.Q, data + 8, 4);
}

void BaseDirectRenderer::handle_uv_packed(const u8* data) {
  u32 u, v;
  memcpy(&u, data, 4);
  memcpy(&v, data + 4, 4);
  m_prim_building.st_reg.x() = u;
  m_prim_building.st_reg.y() = v;
  m_prim_building.Q = 16.f;
}

void BaseDirectRenderer::handle_rgbaq_packed(const u8* data) {
  // TODO update Q from st.
  m_prim_building.rgba_reg[0] = data[0];
  m_prim_building.rgba_reg[1] = data[4];
  m_prim_building.rgba_reg[2] = data[8];
  m_prim_building.rgba_reg[3] = data[12];
}

void BaseDirectRenderer::handle_xyzf2_packed(const u8* data,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  u32 x, y;
  memcpy(&x, data, 4);
  memcpy(&y, data + 4, 4);

  u64 upper;
  memcpy(&upper, data + 8, 8);
  u32 z = (upper >> 4) & 0xffffff;

  u8 f = (upper >> 36);
  bool adc = upper & (1ull << 47);
  handle_xyzf2_common(x << 16, y << 16, z << 8, f, render_state, prof, !adc);
}

void BaseDirectRenderer::handle_xyz2_packed(const u8* data,
                                        BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  u32 x, y;
  memcpy(&x, data, 4);
  memcpy(&y, data + 4, 4);

  u64 upper;
  memcpy(&upper, data + 8, 8);
  u32 z = upper;

  bool adc = upper & (1ull << 47);
  handle_xyzf2_common(x << 16, y << 16, z, 0, render_state, prof, !adc);
}

PerGameVersion<u32> normal_zbp = {448, 304};
void BaseDirectRenderer::handle_zbuf1(u64 val,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  // note: we can basically ignore this. There's a single z buffer that's always configured the same
  // way - 24-bit, at offset 448.
  GsZbuf x(val);
  ASSERT(x.psm() == TextureFormat::PSMZ24);
  ASSERT(x.zbp() == normal_zbp[render_state->version]);

  bool write = !x.zmsk();
  //  ASSERT(write);
  if (write != m_test_state.depth_writes) {
    m_stats.flush_from_zbuf++;
    flush_pending(render_state, prof);
    m_test_state_needs_graphics_update = true;
    m_prim_graphics_state_needs_graphics_update = true;
    m_test_state.depth_writes = write;
  }
}

void BaseDirectRenderer::handle_test1(u64 val,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  GsTest reg(val);
  if (reg.alpha_test_enable()) {
    // ASSERT(reg.alpha_test() == GsTest::AlphaTest::ALWAYS);
  }
  ASSERT(!reg.date());
  if (m_test_state.current_register != reg) {
    m_stats.flush_from_test++;
    flush_pending(render_state, prof);
    m_test_state.from_register(reg);
    m_test_state_needs_graphics_update = true;
    m_prim_graphics_state_needs_graphics_update = true;
  }
}

void BaseDirectRenderer::handle_alpha1(u64 val,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  GsAlpha reg(val);
  if (m_blend_state.current_register != reg) {
    m_stats.flush_from_alpha++;
    flush_pending(render_state, prof);
    m_blend_state.from_register(reg);
    m_blend_state_needs_graphics_update = true;
  }
}

void BaseDirectRenderer::handle_pabe(u64 val) {
  ASSERT(val == 0);  // not really sure how to handle this yet.
}

void BaseDirectRenderer::handle_clamp1(u64 val) {
  if (!(val == 0b101 || val == 0 || val == 1 || val == 0b100)) {
    //    fmt::print("clamp: 0x{:x}\n", val);
    //    ASSERT(false);
  }

  if (m_tex_state_from_reg.m_clamp_state.current_register != val) {
    m_current_tex_state_idx = -1;
    m_tex_state_from_reg.m_clamp_state.current_register = val;
    m_tex_state_from_reg.m_clamp_state.clamp_s = val & 0b001;
    m_tex_state_from_reg.m_clamp_state.clamp_t = val & 0b100;
  }
}

void BaseDirectRenderer::handle_prim_packed(const u8* data,
                                        BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  u64 val;
  memcpy(&val, data, sizeof(u64));
  handle_prim(val, render_state, prof);
}

void BaseDirectRenderer::handle_prim(u64 val,
                                 BaseSharedRenderState* render_state,
                                 ScopedProfilerNode& prof) {
  if (m_prim_building.tri_strip_startup) {
    m_prim_building.tri_strip_startup = 0;
    m_prim_building.building_idx = 0;
  } else {
    if (m_prim_building.building_idx > 0) {
      ASSERT(false);  // shouldn't leave any half-finished prims
    }
  }
  // need to flush any in progress prims to the buffer.

  GsPrim prim(val);
  if (m_prim_graphics_state.current_register != prim || m_blend_state.alpha_blend_enable != prim.abe()) {
    m_stats.flush_from_prim++;
    flush_pending(render_state, prof);
    m_prim_graphics_state.from_register(prim);
    m_blend_state.alpha_blend_enable = prim.abe();
    m_prim_graphics_state_needs_graphics_update = true;
    m_blend_state_needs_graphics_update = true;
  }

  m_prim_building.kind = prim.kind();
}

void BaseDirectRenderer::handle_rgbaq(u64 val) {
  ASSERT((val >> 32) == 0);  // q = 0
  memcpy(m_prim_building.rgba_reg.data(), &val, 4);
}

int BaseDirectRenderer::get_texture_unit_for_current_reg(BaseSharedRenderState* render_state,
                                                     ScopedProfilerNode& prof) {
  if (m_current_tex_state_idx != -1) {
    return m_current_tex_state_idx;
  }

  if (m_next_free_tex_state >= TEXTURE_STATE_COUNT) {
    m_stats.flush_from_state_exhaust++;
    flush_pending(render_state, prof);
    return get_texture_unit_for_current_reg(render_state, prof);
  } else {
    ASSERT(!m_buffered_tex_state[m_next_free_tex_state].used);
    m_buffered_tex_state[m_next_free_tex_state] = m_tex_state_from_reg;
    m_buffered_tex_state[m_next_free_tex_state].used = true;
    m_current_tex_state_idx = m_next_free_tex_state++;
    return m_current_tex_state_idx;
  }
}

void BaseDirectRenderer::handle_xyzf2_common(u32 x,
                                         u32 y,
                                         u32 z,
                                         u8 f,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof,
                                         bool advance) {
  if (m_prim_buffer.is_full()) {
    lg::warn("Buffer wrapped in {} ({} verts, {} bytes)", m_name, vertex_buffer_max_verts,
             m_prim_buffer.vert_count * sizeof(Vertex));
    flush_pending(render_state, prof);
  }

  m_prim_building.building_stq.at(m_prim_building.building_idx) = math::Vector<float, 3>(
      m_prim_building.st_reg.x(), m_prim_building.st_reg.y(), m_prim_building.Q);
  m_prim_building.building_rgba.at(m_prim_building.building_idx) = m_prim_building.rgba_reg;
  m_prim_building.building_vert.at(m_prim_building.building_idx) = math::Vector<u32, 4>{x, y, z, f};

  m_prim_building.building_idx++;

  int tex_unit = get_texture_unit_for_current_reg(render_state, prof);
  bool tcc = m_buffered_tex_state[tex_unit].tcc;
  bool decal = m_buffered_tex_state[tex_unit].decal;
  bool fge = m_prim_graphics_state.fogging_enable;

  switch (m_prim_building.kind) {
    case GsPrim::Kind::SPRITE: {
      if (m_prim_building.building_idx == 2) {
        // build triangles from the sprite.
        auto& corner1_vert = m_prim_building.building_vert[0];
        auto& corner1_rgba = m_prim_building.building_rgba[0];
        auto& corner2_vert = m_prim_building.building_vert[1];
        auto& corner2_rgba = m_prim_building.building_rgba[1];
        auto& corner1_stq = m_prim_building.building_stq[0];
        auto& corner2_stq = m_prim_building.building_stq[1];

        // should use most recent vertex z.
        math::Vector<u32, 4> corner3_vert{corner1_vert[0], corner2_vert[1], corner2_vert[2]};
        math::Vector<u32, 4> corner4_vert{corner2_vert[0], corner1_vert[1], corner2_vert[2]};
        math::Vector<float, 3> corner3_stq{corner1_stq[0], corner2_stq[1], corner2_stq[2]};
        math::Vector<float, 3> corner4_stq{corner2_stq[0], corner1_stq[1], corner2_stq[2]};

        if (m_prim_graphics_state.gouraud_enable) {
          // I'm not really sure what the GS does here.
          ASSERT(false);
        }
        auto& corner3_rgba = corner2_rgba;
        auto& corner4_rgba = corner2_rgba;

        m_prim_buffer.push(corner1_rgba, corner1_vert, corner1_stq, 0, tcc, decal, fge);
        m_prim_buffer.push(corner3_rgba, corner3_vert, corner3_stq, 0, tcc, decal, fge);
        m_prim_buffer.push(corner2_rgba, corner2_vert, corner2_stq, 0, tcc, decal, fge);
        m_prim_buffer.push(corner2_rgba, corner2_vert, corner2_stq, 0, tcc, decal, fge);
        m_prim_buffer.push(corner4_rgba, corner4_vert, corner4_stq, 0, tcc, decal, fge);
        m_prim_buffer.push(corner1_rgba, corner1_vert, corner1_stq, 0, tcc, decal, fge);
        m_prim_building.building_idx = 0;
      }
    } break;
    case GsPrim::Kind::TRI_STRIP: {
      if (m_prim_building.building_idx == 3) {
        m_prim_building.building_idx = 0;
      }

      if (m_prim_building.tri_strip_startup < 3) {
        m_prim_building.tri_strip_startup++;
      }
      if (m_prim_building.tri_strip_startup >= 3) {
        if (advance) {
          for (int i = 0; i < 3; i++) {
            m_prim_buffer.push(m_prim_building.building_rgba[i], m_prim_building.building_vert[i],
                               m_prim_building.building_stq[i], tex_unit, tcc, decal, fge);
          }
        }
      }

    } break;

    case GsPrim::Kind::TRI:
      if (m_prim_building.building_idx == 3) {
        m_prim_building.building_idx = 0;
        for (int i = 0; i < 3; i++) {
          m_prim_buffer.push(m_prim_building.building_rgba[i], m_prim_building.building_vert[i],
                             m_prim_building.building_stq[i], tex_unit, tcc, decal, fge);
        }
      }
      break;

    case GsPrim::Kind::TRI_FAN: {
      if (m_prim_building.tri_strip_startup < 2) {
        m_prim_building.tri_strip_startup++;
      } else {
        if (m_prim_building.building_idx == 2) {
          // nothing.
        } else if (m_prim_building.building_idx == 3) {
          m_prim_building.building_idx = 1;
        }
        for (int i = 0; i < 3; i++) {
          m_prim_buffer.push(m_prim_building.building_rgba[i], m_prim_building.building_vert[i],
                             m_prim_building.building_stq[i], tex_unit, tcc, decal, fge);
        }
      }
    } break;

    case GsPrim::Kind::LINE: {
      if (m_prim_building.building_idx == 2) {
        math::Vector<double, 3> pt0 = m_prim_building.building_vert[0].xyz().cast<double>();
        math::Vector<double, 3> pt1 = m_prim_building.building_vert[1].xyz().cast<double>();
        auto normal = (pt1 - pt0).normalized().cross(math::Vector<double, 3>{0, 0, 1});

        double line_width = (1 << 19);
        //        debug_print_vtx(m_prim_building.building_vert[0]);
        //        debug_print_vtx(m_prim_building.building_vert[1]);

        math::Vector<double, 3> a = pt0 + normal * line_width;
        math::Vector<double, 3> b = pt1 + normal * line_width;
        math::Vector<double, 3> c = pt0 - normal * line_width;
        math::Vector<double, 3> d = pt1 - normal * line_width;
        math::Vector<u32, 4> ai{a.x(), a.y(), a.z(), 0};
        math::Vector<u32, 4> bi{b.x(), b.y(), b.z(), 0};
        math::Vector<u32, 4> ci{c.x(), c.y(), c.z(), 0};
        math::Vector<u32, 4> di{d.x(), d.y(), d.z(), 0};

        // ACB:
        m_prim_buffer.push(m_prim_building.building_rgba[0], ai, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[0], ci, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[1], bi, {}, 0, false, false, false);
        // b c d
        m_prim_buffer.push(m_prim_building.building_rgba[1], bi, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[0], ci, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[1], di, {}, 0, false, false, false);
        //

        m_prim_building.building_idx = 0;
      }
    } break;
    default:
      ASSERT_MSG(false, fmt::format("prim type {} is unsupported in {}.", (int)m_prim_building.kind,
                                    name_and_id()));
  }
}

void BaseDirectRenderer::handle_xyzf2(u64 val,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  u32 x = val & 0xffff;
  u32 y = (val >> 16) & 0xffff;
  u32 z = (val >> 32) & 0xffffff;
  u32 f = (val >> 56) & 0xff;

  handle_xyzf2_common(x << 16, y << 16, z << 8, f, render_state, prof, true);
}

void BaseDirectRenderer::TestState::from_register(GsTest reg) {
  current_register = reg;
  alpha_test_enable = reg.alpha_test_enable();
  if (alpha_test_enable) {
    alpha_test = reg.alpha_test();
    aref = reg.aref();
    afail = reg.afail();
  }

  date = reg.date();
  if (date) {
    datm = reg.datm();
  }

  zte = reg.zte();
  ztst = reg.ztest();
}

void BaseDirectRenderer::BlendState::from_register(GsAlpha reg) {
  current_register = reg;
  a = reg.a_mode();
  b = reg.b_mode();
  c = reg.c_mode();
  d = reg.d_mode();
  fix = reg.fix();
}

void BaseDirectRenderer::PrimGlState::from_register(GsPrim reg) {
  current_register = reg;
  gouraud_enable = reg.gouraud();
  texture_enable = reg.tme();
  fogging_enable = reg.fge();
  aa_enable = reg.aa1();
  use_uv = reg.fst();
  ctxt = reg.ctxt();
  fix = reg.fix();
}

BaseDirectRenderer::PrimitiveBuffer::PrimitiveBuffer(int max_triangles) {
  vertices.resize(max_triangles * 3);
  max_verts = max_triangles * 3;
}

void BaseDirectRenderer::PrimitiveBuffer::push(const math::Vector<u8, 4>& rgba,
                                           const math::Vector<u32, 4>& vert,
                                           const math::Vector<float, 3>& st,
                                           int unit,
                                           bool tcc,
                                           bool decal,
                                           bool fog_enable) {
  if (is_full()) {
    return;
  }

  auto& v = vertices[vert_count];
  v.rgba = rgba;
  v.xyzf[0] = (float)vert[0] / (float)UINT32_MAX;
  v.xyzf[1] = (float)vert[1] / (float)UINT32_MAX;
  v.xyzf[2] = (float)vert[2] / (float)UINT32_MAX;
  v.xyzf[3] = (float)vert[3];
  v.stq = st;
  v.tex_unit = unit;
  v.tcc = tcc;
  v.decal = decal;
  v.fog_enable = fog_enable;
  vert_count++;
}
