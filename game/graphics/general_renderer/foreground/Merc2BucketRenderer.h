#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/foreground/Merc2.h"

class BaseMerc2BucketRenderer : public BaseBucketRenderer {
 public:
  BaseMerc2BucketRenderer(const std::string& name, int my_id) : BaseBucketRenderer(name, my_id){};
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {};

 protected:
  BaseMercDebugStats m_debug_stats;
};
