
#pragma once
#include "game/graphics/general_renderer/SkyBlendCPU.h"
#include "game/graphics/general_renderer/SkyBlendGPU.h"
#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/DirectRenderer.h"
#include "game/graphics/general_renderer/background/TFragment.h"

/*!
 * Handles texture blending for the sky.
 * Will insert the result texture into the texture pool.
 */
class BaseSkyBlendHandler : public BaseBucketRenderer {
 public:
  BaseSkyBlendHandler(const std::string& name,
                  int my_id,
                  int level_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  void handle_sky_copies(DmaFollower& dma,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof);

  virtual SkyBlendStats cpu_blender_do_sky_blends(DmaFollower& dma,
                                           BaseSharedRenderState* render_state,
                                           ScopedProfilerNode& prof) = 0;

  virtual SkyBlendStats gpu_blender_do_sky_blends(DmaFollower& dma,
                                                BaseSharedRenderState* render_state,
                                                ScopedProfilerNode& prof) = 0;

  virtual void tfrag_renderer_render(DmaFollower& dma,
                                     BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& tfrag_prof) = 0;
  virtual void tfrag_renderer_draw_debug_window() = 0;

  SkyBlendStats m_gpu_stats;
};

/*!
 * Handles sky drawing.
 */
class BaseSkyRenderer : public BaseBucketRenderer {
 public:
  BaseSkyRenderer(const std::string& name, int my_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  struct FrameStats {
    int gif_packets = 0;
  } m_frame_stats;

  virtual void direct_renderer_reset_state() = 0;
  virtual void direct_renderer_draw_debug_window() = 0;
  virtual void direct_renderer_flush_pending(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;
  virtual void direct_renderer_render_gif(const u8* data, u32 size, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;
  virtual void direct_renderer_render_vif(const u32 vif0, const u32 vif1, const u8* data, u32 size_bytes,
                                          BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;
};
