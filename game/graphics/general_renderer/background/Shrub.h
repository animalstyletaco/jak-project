#pragma once

#include <optional>

#include "common/util/FilteredValue.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/gfx.h"

class BaseShrub : public BaseBucketRenderer {
 public:
  BaseShrub(const std::string& name, int my_id);
  ~BaseShrub();
  void render(DmaFollower& dma,
              BaseSharedRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  virtual bool setup_for_level(const std::string& level, BaseSharedRenderState* render_state) = 0;

  virtual void discard_tree_cache() = 0;
  virtual void render_all_trees(const TfragRenderSettings& settings,
                                BaseSharedRenderState* render_state,
                                ScopedProfilerNode& prof) = 0;

  struct Tree {
    u32 vert_count;
    const std::vector<tfrag3::ShrubDraw>* draws = nullptr;
    const std::vector<tfrag3::TieWindInstance>* instance_info = nullptr;
    const std::vector<tfrag3::TimeOfDayColor>* colors = nullptr;
    const u32* index_data = nullptr;
    SwizzledTimeOfDay tod_cache;

    struct {
      u32 draws = 0;
      u32 wind_draws = 0;
      Filtered<float> cull_time;
      Filtered<float> index_time;
      Filtered<float> tod_time;
      Filtered<float> setup_time;
      Filtered<float> draw_time;
      Filtered<float> tree_time;
    } perf;
  };

  std::string m_level_name;
  u64 m_load_id = -1;

  TfragPcPortData m_pc_port_data;
  std::vector<math::Vector<u8, 4>> m_color_result;

  bool m_has_level = false;
  bool m_use_fast_time_of_day = true;
};
