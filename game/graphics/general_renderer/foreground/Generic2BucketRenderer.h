#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/foreground/Generic2.h"

class BaseGeneric2BucketRenderer : public BaseBucketRenderer {
 public:
  BaseGeneric2BucketRenderer(const std::string& name,
                         int id, BaseGeneric2::Mode mode);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  virtual void generic_render(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof,
                              BaseGeneric2::Mode mode) = 0;

  protected:
  BaseGeneric2::Mode m_mode;
};
