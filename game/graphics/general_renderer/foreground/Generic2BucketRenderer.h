#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/foreground/Generic2.h"

class BaseGeneric2BucketRenderer : public BaseBucketRenderer {
 public:
  BaseGeneric2BucketRenderer(const std::string& name, int id, BaseGeneric2::Mode mode)
      : BaseBucketRenderer(name, id){};
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof){};

  protected:
  BaseGeneric2::Mode m_mode;
};
