#pragma once

#include "game/graphics/opengl_renderer/BucketRenderer.h"

class BaseLightningRenderer : public BaseBucketRenderer {
 public:
  BaseLightningRenderer(const std::string& name, int id);
  ~BaseLightningRenderer();
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;
  void init_shaders(ShaderLibrary& shaders) override;

 protected:
  void generic_draw_debug_window() = 0;
  void generic_init_shaders(ShaderLibrary& shaders) = 0;
};
