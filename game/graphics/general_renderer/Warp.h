#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/Warp.h"
#include "game/graphics/general_renderer/foreground/Generic2.h"

class BaseWarp : public BaseBucketRenderer {
 public:
  BaseWarp(const std::string& name, int id);
  ~BaseWarp();
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

  virtual void generic_render_in_mode(DmaFollower&, BaseSharedRenderState*, ScopedProfilerNode&, BaseGeneric2::Mode) = 0;
  virtual void generic_draw_debug_window() = 0;

 protected:
  u32 m_tbp = 1216;  // hack, jak 2
};
