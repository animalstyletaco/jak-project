#include "DepthCue.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

namespace {
// Converts fixed point (with 4 bits for decimal) to floating point.

}  // namespace

// Total number of loops depth-cue performs to draw to the framebuffer
constexpr int TOTAL_DRAW_SLICES = 16;

BaseDepthCue::BaseDepthCue(const std::string& name, int my_id) : BaseBucketRenderer(name, my_id) {
  m_draw_slices.resize(TOTAL_DRAW_SLICES);
}

void BaseDepthCue::render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  // First thing should be a NEXT with two nops. this is a jump from buckets to depth-cue
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.size_bytes == 0);

  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // depth-cue renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }

  // Read DMA
  {
    auto prof_node = prof.make_scoped_child("dma");
    read_dma(dma, render_state, prof_node);
  }

  if (!m_enabled) {
    // Renderer disabled, stop early
    return;
  }

  // Set up draw info
  {
    auto prof_node = prof.make_scoped_child("setup");
    setup(render_state, prof_node);
  }

  // Draw
  {
    auto prof_node = prof.make_scoped_child("drawing");
    draw(render_state, prof_node);
  }
}

/*!
 * Reads all depth-cue DMA packets.
 */
void BaseDepthCue::read_dma(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& /*prof*/) {
  // First should be general GS register setup
  {
    auto gs_setup = dma.read_and_advance();
    ASSERT(gs_setup.size_bytes == sizeof(DepthCueGsSetup));
    ASSERT(gs_setup.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(gs_setup.vifcode1().kind == VifCode::Kind::DIRECT);
    memcpy(&m_gs_setup, gs_setup.data, sizeof(DepthCueGsSetup));

    ASSERT(m_gs_setup.gif_tag.nreg() == 6);
    ASSERT(m_gs_setup.gif_tag.reg(0) == GifTag::RegisterDescriptor::AD);

    ASSERT(m_gs_setup.test1.ztest() == GsTest::ZTest::ALWAYS);
    ASSERT(m_gs_setup.zbuf1.zmsk() == true);
    ASSERT(m_gs_setup.tex1.mmag() == true);
    ASSERT(m_gs_setup.tex1.mmin() == 1);
    ASSERT(m_gs_setup.miptbp1 == 0);
    ASSERT(m_gs_setup.alpha1.b_mode() == GsAlpha::BlendMode::DEST);
    ASSERT(m_gs_setup.alpha1.d_mode() == GsAlpha::BlendMode::DEST);
  }

  // Next is 64 DMAs to draw to the depth-cue-base-page and back to the on-screen framebuffer
  // We'll group these by each slice of the framebuffer being drawn to
  for (int i = 0; i < TOTAL_DRAW_SLICES; i++) {
    // Each 'slice' should be:
    // 1. GS setup for drawing from on-screen framebuffer to depth-cue-base-page
    // 2. Draw to depth-cue-base-page
    // 3. GS setup for drawing from depth-cue-base-page back to on-screen framebuffer
    // 4. Draw to on-screen framebuffer
    DrawSlice& slice = m_draw_slices.at(i);

    // depth-cue-base-page setup
    {
      auto depth_cue_page_setup = dma.read_and_advance();
      ASSERT(depth_cue_page_setup.size_bytes == sizeof(DepthCuePageGsSetup));
      ASSERT(depth_cue_page_setup.vifcode0().kind == VifCode::Kind::NOP);
      ASSERT(depth_cue_page_setup.vifcode1().kind == VifCode::Kind::DIRECT);
      memcpy(&slice.depth_cue_page_setup, depth_cue_page_setup.data, sizeof(DepthCuePageGsSetup));

      ASSERT(slice.depth_cue_page_setup.gif_tag.nreg() == 5);
      ASSERT(slice.depth_cue_page_setup.gif_tag.reg(0) == GifTag::RegisterDescriptor::AD);

      ASSERT(slice.depth_cue_page_setup.tex01.tcc() == 1);
      ASSERT(slice.depth_cue_page_setup.test1.ztest() == GsTest::ZTest::ALWAYS);
      ASSERT(slice.depth_cue_page_setup.alpha1.b_mode() == GsAlpha::BlendMode::SOURCE);
      ASSERT(slice.depth_cue_page_setup.alpha1.d_mode() == GsAlpha::BlendMode::SOURCE);
    }

    // depth-cue-base-page draw
    {
      auto depth_cue_page_draw = dma.read_and_advance();
      ASSERT(depth_cue_page_draw.size_bytes == sizeof(DepthCuePageDraw));
      ASSERT(depth_cue_page_draw.vifcode0().kind == VifCode::Kind::NOP);
      ASSERT(depth_cue_page_draw.vifcode1().kind == VifCode::Kind::DIRECT);
      memcpy(&slice.depth_cue_page_draw, depth_cue_page_draw.data, sizeof(DepthCuePageDraw));

      ASSERT(slice.depth_cue_page_draw.gif_tag.nloop() == 1);
      ASSERT(slice.depth_cue_page_draw.gif_tag.pre() == true);
      ASSERT(slice.depth_cue_page_draw.gif_tag.prim() == 6);
      ASSERT(slice.depth_cue_page_draw.gif_tag.flg() == GifTag::Format::PACKED);
      ASSERT(slice.depth_cue_page_draw.gif_tag.nreg() == 5);
      ASSERT(slice.depth_cue_page_draw.gif_tag.reg(0) == GifTag::RegisterDescriptor::RGBAQ);
    }

    // on-screen setup
    {
      auto on_screen_setup = dma.read_and_advance();
      ASSERT(on_screen_setup.size_bytes == sizeof(OnScreenGsSetup));
      ASSERT(on_screen_setup.vifcode0().kind == VifCode::Kind::NOP);
      ASSERT(on_screen_setup.vifcode1().kind == VifCode::Kind::DIRECT);
      memcpy(&slice.on_screen_setup, on_screen_setup.data, sizeof(OnScreenGsSetup));

      ASSERT(slice.on_screen_setup.gif_tag.nreg() == 5);
      ASSERT(slice.on_screen_setup.gif_tag.reg(0) == GifTag::RegisterDescriptor::AD);

      ASSERT(slice.on_screen_setup.tex01.tcc() == 0);
      ASSERT(slice.on_screen_setup.texa.ta0() == 0x80);
      ASSERT(slice.on_screen_setup.texa.ta1() == 0x80);
      ASSERT(slice.on_screen_setup.alpha1.b_mode() == GsAlpha::BlendMode::DEST);
      ASSERT(slice.on_screen_setup.alpha1.d_mode() == GsAlpha::BlendMode::DEST);
    }

    // on-screen draw
    {
      auto on_screen_draw = dma.read_and_advance();
      ASSERT(on_screen_draw.size_bytes == sizeof(OnScreenDraw));
      ASSERT(on_screen_draw.vifcode0().kind == VifCode::Kind::NOP);
      ASSERT(on_screen_draw.vifcode1().kind == VifCode::Kind::DIRECT);
      memcpy(&slice.on_screen_draw, on_screen_draw.data, sizeof(OnScreenDraw));

      ASSERT(slice.on_screen_draw.gif_tag.nloop() == 1);
      ASSERT(slice.on_screen_draw.gif_tag.pre() == true);
      ASSERT(slice.on_screen_draw.gif_tag.prim() == 6);
      ASSERT(slice.on_screen_draw.gif_tag.flg() == GifTag::Format::PACKED);
      ASSERT(slice.on_screen_draw.gif_tag.nreg() == 5);
      ASSERT(slice.on_screen_draw.gif_tag.reg(0) == GifTag::RegisterDescriptor::RGBAQ);
    }
  }

  // Finally, a packet to restore GS state
  {
    auto gs_restore = dma.read_and_advance();
    ASSERT(gs_restore.size_bytes == sizeof(DepthCueGsRestore));
    ASSERT(gs_restore.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(gs_restore.vifcode1().kind == VifCode::Kind::DIRECT);
    memcpy(&m_gs_restore, gs_restore.data, sizeof(DepthCueGsRestore));

    ASSERT(m_gs_restore.gif_tag.nreg() == 2);
    ASSERT(m_gs_restore.gif_tag.reg(0) == GifTag::RegisterDescriptor::AD);
  }

  // End with 'NEXT'
  {
    ASSERT(dma.current_tag().kind == DmaTag::Kind::NEXT);

    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
  }
}

void BaseDepthCue::build_sprite(std::vector<SpriteVertex>& vertices,
                            float x1,
                            float y1,
                            float s1,
                            float t1,
                            float x2,
                            float y2,
                            float s2,
                            float t2) {
  // First triangle
  // -------------
  // Top-left
  vertices.push_back(SpriteVertex(x1, y1, s1, t1));

  // Top-right
  vertices.push_back(SpriteVertex(x2, y1, s2, t1));

  // Bottom-left
  vertices.push_back(SpriteVertex(x1, y2, s1, t2));

  // Second triangle
  // -------------
  // Top-right
  vertices.push_back(SpriteVertex(x2, y1, s2, t1));

  // Bottom-left
  vertices.push_back(SpriteVertex(x1, y2, s1, t2));

  // Bottom-right
  vertices.push_back(SpriteVertex(x2, y2, s2, t2));
}

void BaseDepthCue::draw_debug_window() {
  ImGui::Text("NOTE: depth-cue may be disabled by '*vu1-enable-user-menu*'!");

  ImGui::Checkbox("Cache setup", &m_debug.cache_setup);
  ImGui::Checkbox("Force original resolution", &m_debug.force_original_res);

  ImGui::Checkbox("Override alpha", &m_debug.override_alpha);
  if (m_debug.override_alpha) {
    ImGui::SliderFloat("Alpha", &m_debug.draw_alpha, 0.0f, 1.0f);
  }

  ImGui::Checkbox("Override sharpness", &m_debug.override_sharpness);
  if (m_debug.override_sharpness) {
    ImGui::SliderFloat("Sharpness", &m_debug.sharpness, 0.001f, 1.0f);
  }

  ImGui::SliderFloat("Depth", &m_debug.depth, 0.0f, 1.0f);
  ImGui::SliderFloat("Resolution scale", &m_debug.res_scale, 0.001f, 2.0f);

  if (ImGui::Button("Reset")) {
    m_debug.draw_alpha = 0.4f;
    m_debug.sharpness = 0.999f;
    m_debug.depth = 1.0f;
    m_debug.res_scale = 1.0f;
  }
}
