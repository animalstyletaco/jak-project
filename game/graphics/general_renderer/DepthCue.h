#pragma once

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/dma_helpers.h"

class BaseDepthCue : public BaseBucketRenderer {
 public:
  BaseDepthCue(const std::string& name, int my_id);
  void render(DmaFollower& dma,
              BaseSharedRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  virtual void graphics_setup() = 0;

  virtual void setup(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;
  virtual void draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;

  void read_dma(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

  float fixed_to_floating_point(int fixed) { return fixed / 16.0f; }

  math::Vector2f fixed_to_floating_point(const math::Vector<s32, 2>& fixed_vec) {
    return math::Vector2f(fixed_to_floating_point(fixed_vec.x()),
                          fixed_to_floating_point(fixed_vec.y()));
  }

  // One-time initial GS setup per frame
  struct DepthCueGsSetup {
    GifTag gif_tag;
    GsTest test1;
    u64 test1_addr;
    GsZbuf zbuf1;
    u64 zbuf1_addr;
    GsTex1 tex1;
    u64 tex1_addr;
    u64 miptbp1;
    u64 miptbp1_addr;
    u64 clamp1;
    u64 clamp1_addr;
    GsAlpha alpha1;
    u64 alpha1_addr;
  };
  static_assert(sizeof(DepthCueGsSetup) == (7 * 16));

  // One-time GS state restoration at the end of each depth-cue frame
  struct DepthCueGsRestore {
    GifTag gif_tag;
    u64 xyoffset1;
    u64 xyoffset1_addr;
    GsFrame frame1;
    u64 frame1_addr;
  };
  static_assert(sizeof(DepthCueGsRestore) == (3 * 16));

  // GS setup for drawing to the depth-cue-base-page framebuffer
  struct DepthCuePageGsSetup {
    GifTag gif_tag;
    GsXYOffset xyoffset1;
    u64 xyoffset1_addr;
    GsFrame frame1;
    u64 frame1_addr;
    GsTex0 tex01;
    u64 tex01_addr;
    GsTest test1;
    u64 test1_addr;
    GsAlpha alpha1;
    u64 alpha1_addr;
  };
  static_assert(sizeof(DepthCuePageGsSetup) == (6 * 16));

  // GS setup for drawing to the on-screen framebuffer
  struct OnScreenGsSetup {
    GifTag gif_tag;
    GsXYOffset xyoffset1;
    u64 xyoffset1_addr;
    GsFrame frame1;
    u64 frame1_addr;
    GsTexa texa;
    u64 texa_addr;
    GsTex0 tex01;
    u64 tex01_addr;
    GsAlpha alpha1;
    u64 alpha1_addr;
  };
  static_assert(sizeof(OnScreenGsSetup) == (6 * 16));

  // Sprite draw command to the depth-cue-base-page framebuffer
  struct DepthCuePageDraw {
    GifTag gif_tag;
    math::Vector4<s32> rgbaq;
    math::Vector4<s32> uv_1;
    math::Vector4<s32> xyzf2_1;
    math::Vector4<s32> uv_2;
    math::Vector4<s32> xyzf2_2;
  };
  static_assert(sizeof(DepthCuePageDraw) == (6 * 16));

  // Sprite draw command to the on-screen framebuffer
  struct OnScreenDraw {
    GifTag gif_tag;
    math::Vector4<s32> rgbaq;
    math::Vector4<s32> uv_1;
    math::Vector4<s32> xyzf2_1;
    math::Vector4<s32> uv_2;
    math::Vector4<s32> xyzf2_2;
  };
  static_assert(sizeof(OnScreenDraw) == (6 * 16));

  // A draw to the depth-cue-base-page and then back to the on-screen framebuffer.
  //
  // This is done 16 times per frame across the entire on-screen framebuffer in vertical strips.
  struct DrawSlice {
    DepthCuePageGsSetup depth_cue_page_setup;
    DepthCuePageDraw depth_cue_page_draw;
    OnScreenGsSetup on_screen_setup;
    OnScreenDraw on_screen_draw;
  };

  struct SpriteVertex {
    math::Vector2f xy;
    math::Vector2f st;

    SpriteVertex() = default;
    SpriteVertex(float x, float y, float s, float t) : xy(x, y), st(s, t) {}
  };

  DepthCueGsSetup m_gs_setup;
  DepthCueGsRestore m_gs_restore;
  std::vector<DrawSlice> m_draw_slices;

  struct {
    // false = recompute setup each frame
    // true = only recompute setup when draw dimensions change
    bool cache_setup = true;
    // true = render depth-cue at original 512px wide resolution
    bool force_original_res = false;
    // true = render with m_draw_alpha alpha
    bool override_alpha = false;
    // 0.4 = default in GOAL
    float draw_alpha = 0.4f;
    // true = render with m_sharpness sharpness
    bool override_sharpness = false;
    // 1.0 = pixel perfect, depth-cue has no effect
    // 0.999 = default in GOAL
    float sharpness = 0.999f;
    // lower to have effect only apply to further away pixels
    // 1.0 = default (apply to all)
    float depth = 1.0f;
    // depth-cue resolution multiplier
    float res_scale = 1.0f;
  } m_debug;

  void build_sprite(std::vector<SpriteVertex>& vertices,
                    float x1,
                    float y1,
                    float x2,
                    float y2,
                    float s1,
                    float t1,
                    float s2,
                    float t2);
};
