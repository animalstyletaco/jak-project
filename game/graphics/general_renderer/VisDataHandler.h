#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"

/*!
 * The VisDataHandler copies visibility data from add-pc-port-background-data for the background
 * renderers.
 */
class BaseVisDataHandler : public BaseBucketRenderer {
 public:
  BaseVisDataHandler(const std::string& name, int my_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 private:
  struct LevelStats {
    bool has_vis = false;
    int num_visible = 0;
  };
  static constexpr int kMaxLevels = 10;
  LevelStats m_stats[kMaxLevels];
  bool m_count_vis = false;
};
