#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/foreground/Merc2.h"

class BaseMerc2BucketRenderer : public BaseBucketRenderer {
 public:
  BaseMerc2BucketRenderer(const std::string& name, int my_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  virtual void merc2_render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof, BaseMercDebugStats*) = 0;

 protected:
  BaseMercDebugStats m_debug_stats;
};
