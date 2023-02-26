#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/foreground/Generic2.h"

class BaseLightningRenderer : public BaseBucketRenderer {
 public:
  BaseLightningRenderer(const std::string& name, int id);
  ~BaseLightningRenderer();
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void draw_debug_window() override;

 protected:
  virtual void generic_draw_debug_window() = 0;
  virtual void generic_render_in_mode(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof,
                              BaseGeneric2::Mode) = 0;
};
