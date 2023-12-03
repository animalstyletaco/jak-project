#include "GlowRenderer.h"

#include "third-party/imgui/imgui.h"

/*
 * The glow renderer draws a sprite, but only if the center of the sprite is "visible".
 * To determine visibility, we draw a test "probe" at the center of the sprite and see how many
 * pixels of the probe were visible.
 *
 * The inputs to this renderer are the transformed vertices that would be computed on VU1.
 * The convention is that float -> int conversions and scalings are dropped.
 *
 * To detect if this is visible, we do something a little different from the original game:
 * - first copy the depth buffer to a separate texture. It seems like we could eliminate this copy
 *   eventually and always render to a texture.
 *
 * - For each sprite draw a "test probe" using this depth buffer. Write to a separate texture.
 *   This test probe is pretty small. First clear alpha to 0, then draw a test image with alpha = 1
 *   and depth testing on.
 *
 * - Repeatedly sample the result of the probe as a texture and draw it to another texture with half
 *   the size. This effectively averages the alpha values with texture filtering.
 *
 * - Stop once we've reached 2x2. At this point we can sample the center of this "texture" and the
 *   alpha will indicate the fraction of pixels in the original probe that passed depth. Use this
 *   to scale the intensity of the actual sprite draw.
 *
 * There are a number of optimizations made at this point:
 * - Probe clear/drawing are batched.
 * - Instead of doing the "downsampling" one-at-a-time, all probes are copied to a big grid and
 *   the downsampling happens in batches.
 * - The sampling of the final downsampled texture happens inside the vertex shader. On PS2, it's
 *   used as a texture, drawn as alpha-only over the entire region, then blended with the final
 *   draw. But the alpha of the entire first draw is constant, and we can figure it out in the
 *   vertex shader, so there's no need to do this approach.
 *
 * there are a few remaining improvements that could be made:
 *   - The final draws could be bucketed, this would reduce draw calls by a lot.
 *   - The depth buffer copy could likely be eliminated.
 *   - There's a possibility that overlapping probes do the "wrong" thing. This could be solved by
 *     copying from the depth buffer to the grid, then drawing probes on the grid. Currently the
 *     probes are drawn aligned with the framebuffer, then copied back to the grid. This approach
 *     would also lower the vram needed.
 */

BaseGlowRenderer::BaseGlowRenderer() {
  m_vertex_buffer.resize(kMaxVertices);
  m_sprite_data_buffer.resize(kMaxSprites);
  m_index_buffer.resize(kMaxIndices);

  m_downsample_vertices.resize(kMaxSprites * 4);
  m_downsample_indices.resize(kMaxSprites * 5);
  for (int i = 0; i < kMaxSprites; i++) {
    int x = i / kDownsampleBatchWidth;
    int y = i % kDownsampleBatchWidth;
    float step = 1.f / kDownsampleBatchWidth;
    Vertex* vtx = &m_downsample_vertices.at(i * 4);
    for (int j = 0; j < 4; j++) {
      vtx[j].r = 0.f;  // debug
      vtx[j].g = 0.f;
      vtx[j].b = 0.f;
      vtx[j].a = 0.f;
      vtx[j].x = x * step;
      vtx[j].y = y * step;
      vtx[j].z = 0;
      vtx[j].w = 0;
    }
    vtx[1].x += step;
    vtx[2].y += step;
    vtx[3].x += step;
    vtx[3].y += step;
    m_downsample_indices.at(i * 5 + 0) = i * 4;
    m_downsample_indices.at(i * 5 + 1) = i * 4 + 1;
    m_downsample_indices.at(i * 5 + 2) = i * 4 + 2;
    m_downsample_indices.at(i * 5 + 3) = i * 4 + 3;
    m_downsample_indices.at(i * 5 + 4) = UINT32_MAX;
  }

  // set up a default draw mode for sprites. If they don't set values, they will get this
  // from the giftag
  m_default_draw_mode.set_ab(true);

  //  ;; (new 'static 'gs-test :ate 1 :afail 1 :zte 1 :ztst 2)
  //  (new 'static 'gs-adcmd :cmds (gs-reg64 test-1) :x #x51001)
  m_default_draw_mode.set_at(true);
  m_default_draw_mode.set_alpha_fail(GsTest::AlphaFail::FB_ONLY);
  m_default_draw_mode.set_zt(true);
  m_default_draw_mode.set_depth_test(GsTest::ZTest::GEQUAL);
  m_default_draw_mode.set_alpha_test(DrawMode::AlphaTest::NEVER);

  //  ;; (new 'static 'gs-zbuf :zbp 304 :psm 1 :zmsk 1)
  //  (new 'static 'gs-adcmd :cmds (gs-reg64 zbuf-1) :x #x1000130 :y #x1)
  m_default_draw_mode.disable_depth_write();
}

namespace {
void copy_to_vertex(BaseGlowRenderer::Vertex* vtx, const Vector4f& xyzw) {
  vtx->x = xyzw.x();
  vtx->y = xyzw.y();
  vtx->z = xyzw.z();
  vtx->w = xyzw.w();
}
}  // namespace

SpriteGlowOutput* BaseGlowRenderer::alloc_sprite() {
  ASSERT(m_next_sprite < m_sprite_data_buffer.size());
  return &m_sprite_data_buffer[m_next_sprite++];
}

void BaseGlowRenderer::cancel_sprite() {
  ASSERT(m_next_sprite);
  m_next_sprite--;
}

// vertex addition is done in passes, so the "pass1" for all sprites is before any "pass2" vertices.
// But, we still do a single big upload for all passes.
// this way pass1 can be a single giant draw.

/*!
 * Add pass 1 vertices, for drawing the probe.
 */
void BaseGlowRenderer::add_sprite_pass_1(const SpriteGlowOutput& data) {
  {  // first draw is a GS sprite to clear the alpha. This is faster than glClear, and the game
     // computes these for us and gives it a large z that always passes.
     // We need to convert to triangle strip.
    u32 idx_start = m_next_vertex;
    Vertex* vtx = alloc_vtx(4);
    for (int i = 0; i < 4; i++) {
      vtx[i].r = 1.f;  // red for debug
      vtx[i].g = 0.f;
      vtx[i].b = 0.f;
      vtx[i].a = 0.f;  // clearing alpha
    }
    copy_to_vertex(vtx, data.first_clear_pos[0]);
    copy_to_vertex(vtx + 1, data.first_clear_pos[0]);
    vtx[1].x = data.first_clear_pos[1].x();
    copy_to_vertex(vtx + 2, data.first_clear_pos[0]);
    vtx[2].y = data.first_clear_pos[1].y();
    copy_to_vertex(vtx + 3, data.first_clear_pos[1]);

    u32* idx = alloc_index(5);
    idx[0] = idx_start;
    idx[1] = idx_start + 1;
    idx[2] = idx_start + 2;
    idx[3] = idx_start + 3;
    idx[4] = UINT32_MAX;
  }

  {  // second draw is the actual probe, using the real Z, and setting alpha to 1.
    u32 idx_start = m_next_vertex;
    Vertex* vtx = alloc_vtx(4);
    for (int i = 0; i < 4; i++) {
      vtx[i].r = 0.f;
      vtx[i].g = 1.f;  // green for debug
      vtx[i].b = 0.f;
      vtx[i].a = 1.f;  // setting alpha
    }
    copy_to_vertex(vtx, data.second_clear_pos[0]);
    copy_to_vertex(vtx + 1, data.second_clear_pos[0]);
    vtx[1].x = data.second_clear_pos[1].x();
    copy_to_vertex(vtx + 2, data.second_clear_pos[0]);
    vtx[2].y = data.second_clear_pos[1].y();
    copy_to_vertex(vtx + 3, data.second_clear_pos[1]);

    u32* idx = alloc_index(5);
    idx[0] = idx_start;
    idx[1] = idx_start + 1;
    idx[2] = idx_start + 2;
    idx[3] = idx_start + 3;
    idx[4] = UINT32_MAX;
  }
}

/*!
 * Add pass 2 vertices, for copying from the probe fbo to the biggest grid.
 */
void BaseGlowRenderer::add_sprite_pass_2(const SpriteGlowOutput& data, int sprite_idx) {
  // output is a grid of kBatchWidth * kBatchWidth.
  // for simplicity, we'll map to (0, 1) here, and the shader will convert to (-1, 1) for opengl.
  int x = sprite_idx / kDownsampleBatchWidth;
  int y = sprite_idx % kDownsampleBatchWidth;
  float step = 1.f / kDownsampleBatchWidth;

  u32 idx_start = m_next_vertex;
  Vertex* vtx = alloc_vtx(4);
  for (int i = 0; i < 4; i++) {
    vtx[i].r = 1.f;  // debug
    vtx[i].g = 0.f;
    vtx[i].b = 0.f;
    vtx[i].a = 0.f;
    vtx[i].x = x * step;  // start of our cell
    vtx[i].y = y * step;
    vtx[i].z = 0;
    vtx[i].w = 0;
  }
  vtx[1].x += step;
  vtx[2].y += step;
  vtx[3].x += step;
  vtx[3].y += step;

  // transformation code gives us these coordinates for where to sample probe fbo
  vtx[0].u = data.offscreen_uv[0][0];
  vtx[0].v = data.offscreen_uv[0][1];
  vtx[1].u = data.offscreen_uv[1][0];
  vtx[1].v = data.offscreen_uv[0][1];
  vtx[2].u = data.offscreen_uv[0][0];
  vtx[2].v = data.offscreen_uv[1][1];
  vtx[3].u = data.offscreen_uv[1][0];
  vtx[3].v = data.offscreen_uv[1][1];

  u32* idx = alloc_index(5);
  idx[0] = idx_start;
  idx[1] = idx_start + 1;
  idx[2] = idx_start + 2;
  idx[3] = idx_start + 3;
  idx[4] = UINT32_MAX;
}

/*!
 * Add pass 3 vertices and update sprite records. This is the final draw.
 */
void BaseGlowRenderer::add_sprite_pass_3(const SpriteGlowOutput& data, int sprite_idx) {
  // figure out our cell, we'll need to read from this to see if we're visible or not.
  int x = sprite_idx / kDownsampleBatchWidth;
  int y = sprite_idx % kDownsampleBatchWidth;
  float step = 1.f / kDownsampleBatchWidth;

  u32 idx_start = m_next_vertex;
  Vertex* vtx = alloc_vtx(4);
  for (int i = 0; i < 4; i++) {
    // include the color, used by the shader
    vtx[i].r = data.flare_draw_color[0];
    vtx[i].g = data.flare_draw_color[1];
    vtx[i].b = data.flare_draw_color[2];
    vtx[i].a = data.flare_draw_color[3];
    copy_to_vertex(&vtx[i], data.flare_xyzw[i]);
    vtx[i].u = 0;
    vtx[i].v = 0;
    // where to sample from to see probe result
    // offset by step/2 to sample the middle.
    // we use 2x2 for the final resolution and sample the middle - should be the same as
    // going to a 1x1, but saves a draw.
    vtx[i].uu = x * step + step / 2;
    vtx[i].vv = y * step + step / 2;
  }
  // texture uv's hardcoded to corners
  vtx[1].u = 1;
  vtx[3].v = 1;
  vtx[2].u = 1;
  vtx[2].v = 1;

  // get a record
  auto& record = m_sprite_records[sprite_idx];
  record.draw_mode = m_default_draw_mode;
  record.tbp = 0;
  record.idx = m_next_index;

  u32* idx = alloc_index(5);
  // flip first two - fan -> strip
  idx[0] = idx_start + 1;
  idx[1] = idx_start + 0;
  idx[2] = idx_start + 2;
  idx[3] = idx_start + 3;
  idx[4] = UINT32_MAX;

  // handle adgif stuff
  {
    ASSERT(data.adgif.tex0_addr == (u32)GsRegisterAddress::TEX0_1);
    GsTex0 reg(data.adgif.tex0_data);
    record.tbp = reg.tbp0();
    record.draw_mode.set_tcc(reg.tcc());
    // shader is hardcoded for this right now.
    ASSERT(reg.tcc() == 1);
    ASSERT(reg.tfx() == GsTex0::TextureFunction::MODULATE);
  }

  {
    ASSERT((u8)data.adgif.tex1_addr == (u8)GsRegisterAddress::TEX1_1);
    GsTex1 reg(data.adgif.tex1_data);
    record.draw_mode.set_filt_enable(reg.mmag());
  }

  {
    ASSERT(data.adgif.mip_addr == (u32)GsRegisterAddress::MIPTBP1_1);
    // ignore
  }

  // clamp or zbuf
  if (GsRegisterAddress(data.adgif.clamp_addr) == GsRegisterAddress::ZBUF_1) {
    GsZbuf x(data.adgif.clamp_data);
    record.draw_mode.set_depth_write_enable(!x.zmsk());
  } else if (GsRegisterAddress(data.adgif.clamp_addr) == GsRegisterAddress::CLAMP_1) {
    u32 val = data.adgif.clamp_data;
    if (!(val == 0b101 || val == 0 || val == 1 || val == 0b100)) {
      ASSERT_MSG(false, fmt::format("clamp: 0x{:x}", val));
    }
    record.draw_mode.set_clamp_s_enable(val & 0b001);
    record.draw_mode.set_clamp_t_enable(val & 0b100);
  } else {
    ASSERT(false);
  }

  // alpha
  ASSERT(data.adgif.alpha_addr == (u32)GsRegisterAddress::ALPHA_1);  // ??

  // ;; a = 0, b = 2, c = 1, d = 1
  // Cv = (Cs - 0) * Ad + D
  // leaving out the multiply by Ad.
  record.draw_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_0_FIX_DST);
}

void BaseGlowRenderer::draw_debug_window() {
  ImGui::Checkbox("Show Probes", &m_debug.show_probes);
  ImGui::Checkbox("Show Copy", &m_debug.show_probe_copies);
  ImGui::SliderFloat("Boost Glow", &m_debug.glow_boost, 0, 10);
  ImGui::Text("Count: %d", m_debug.num_sprites);
}

/*!
 * Draw all pending sprites.
 */
void BaseGlowRenderer::flush(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_debug.num_sprites = m_next_sprite;
  if (!m_next_sprite) {
    // no sprites submitted.
    return;
  }

  // copy depth from framebuffer to a temporary buffer
  // (this is a bit wasteful)
  blit_depth(render_state);

  // generate vertex/index data for probes
  u32 probe_idx_start = m_next_index;
  for (u32 sidx = 0; sidx < m_next_sprite; sidx++) {
    add_sprite_pass_1(m_sprite_data_buffer[sidx]);
  }

  // generate vertex/index data for copy to downsample buffer
  u32 copy_idx_start = m_next_index;
  for (u32 sidx = 0; sidx < m_next_sprite; sidx++) {
    add_sprite_pass_2(m_sprite_data_buffer[sidx], sidx);
  }
  u32 copy_idx_end = m_next_index;

  // generate vertex/index data for framebuffer draws
  for (u32 sidx = 0; sidx < m_next_sprite; sidx++) {
    add_sprite_pass_3(m_sprite_data_buffer[sidx], sidx);
  }

  // draw probes
  draw_probes(render_state, prof, probe_idx_start, copy_idx_start);
  if (m_debug.show_probes) {
    debug_draw_probes(render_state, prof, probe_idx_start, copy_idx_start);
  }

  // copy probes
  draw_probe_copies(render_state, prof, copy_idx_start, copy_idx_end);
  if (m_debug.show_probe_copies) {
    debug_draw_probe_copies(render_state, prof, copy_idx_start, copy_idx_end);
  }

  // downsample probes.
  downsample_chain(render_state, prof, m_next_sprite);

  draw_sprites(render_state, prof);

  m_next_vertex = 0;
  m_next_index = 0;
  m_next_sprite = 0;
}

BaseGlowRenderer::Vertex* BaseGlowRenderer::alloc_vtx(int num) {
  ASSERT(m_next_vertex + num <= m_vertex_buffer.size());
  auto* result = &m_vertex_buffer[m_next_vertex];
  m_next_vertex += num;
  return result;
}

u32* BaseGlowRenderer::alloc_index(int num) {
  ASSERT(m_next_index + num <= m_index_buffer.size());
  auto* result = &m_index_buffer[m_next_index];
  m_next_index += num;
  return result;
}
