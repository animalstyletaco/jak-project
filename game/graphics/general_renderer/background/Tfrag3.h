#pragma once

#include "common/custom_data/Tfrag3Data.h"
#include "common/math/Vector.h"

#include "game/graphics/gfx.h"
#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/background/background_common.h"

class BaseTfrag3 {
 public:
  virtual ~BaseTfrag3();

  void render_all_trees(int geom,
                        const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  void render_matching_trees(int geom,
                             const std::vector<tfrag3::TFragmentTreeKind>& trees,
                             const TfragRenderSettings& settings,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof);

  virtual void render_tree(int geom,
                           const TfragRenderSettings& settings,
                           BaseSharedRenderState* render_state,
                           ScopedProfilerNode& prof) = 0;

  virtual bool setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                       const std::string& level,
                       BaseSharedRenderState* render_state) = 0;
  virtual void discard_tree_cache() = 0;

  virtual void render_tree_cull_debug(const TfragRenderSettings& settings,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) = 0;

  void draw_debug_window();
  struct DebugVertex {
    math::Vector3f position;
    math::Vector4f rgba;
  };

  void update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                   const BaseLevelData* loader_data);

  int lod() const { return Gfx::g_global_settings.lod_tfrag; }

 protected:
  static constexpr int GEOM_MAX = 3;

  float frac(float in);

  void debug_vis_draw(int first_root,
                      int tree,
                      int num,
                      int depth,
                      const std::vector<tfrag3::VisNode>& nodes,
                      std::vector<BaseTfrag3::DebugVertex>& verts_out);

  struct TreeCache {
    tfrag3::TFragmentTreeKind kind = tfrag3::TFragmentTreeKind::INVALID;

    u32 vert_count = 0;
    const std::vector<tfrag3::StripDraw>* draws = nullptr;
    const std::vector<tfrag3::TimeOfDayColor>* colors = nullptr;
    const tfrag3::BVH* vis = nullptr;
    const u32* index_data = nullptr;
    SwizzledTimeOfDay tod_cache;
    u64 draw_mode = 0;

    void reset_stats() {
      rendered_this_frame = false;
      tris_this_frame = 0;
      draws_this_frame = 0;
    }
    bool rendered_this_frame = false;
    int tris_this_frame = 0;
    int draws_this_frame = 0;
    bool allowed = true;
    bool forced = false;
    bool cull_debug = false;

    bool freeze_itimes = false;
    math::Vector<s32, 4> itimes_debug[4];
  };

  virtual TreeCache& get_cached_tree(int bucket_index, int cache_index) = 0;
  virtual size_t get_total_cached_trees_count(int bucket_index) = 0;

  std::string m_level_name;
  std::vector<math::Vector<u8, 4>> m_color_result;

  u64 m_load_id = -1;

  static constexpr int DEBUG_TRI_COUNT = 4096;
  std::vector<DebugVertex> m_debug_vert_data;

  bool m_has_level = false;
  bool m_use_fast_time_of_day = true;
};

