#pragma once

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/DirectRenderer.h"
#include "game/graphics/general_renderer/background/Tie3.h"

using math::Matrix4f;
using math::Vector4f;

struct TFragData {
  Vector4f fog;          // 0   656 (vf01)
  Vector4f val;          // 1   657 (vf02)
  GifTag str_gif;        // 2   658 (vf06)
  GifTag fan_gif;        // 3   659
  GifTag ad_gif;         // 4   660
  Vector4f hvdf_offset;  // 5   661 (vf10)
  Vector4f hmge_scale;   // 6   662 (vf11)
  Vector4f invh_scale;   // 7   663
  Vector4f ambient;      // 8   664
  Vector4f guard;        // 9   665
  Vector4f k0s[2];       // 10/11 666, 667
  Vector4f k1s[2];       // 12/13 668, 669

  std::string print() const;
};
static_assert(sizeof(TFragData) == 0xe0, "TFragData size");

struct TFragBufferedData {
  u8 pad[328 * 16];
};
static_assert(sizeof(TFragBufferedData) == 328 * 16);

class BaseTFragment : public BaseBucketRenderer {
 public:
  BaseTFragment(const std::string& name,
                int my_id,
                const std::vector<tfrag3::TFragmentTreeKind>& trees,
                bool child_mode,
                int level_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  virtual ~BaseTFragment();

  void draw_debug_window() override;
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

  struct DebugVertex {
    math::Vector3f position;
    math::Vector4f rgba;
  };

  void update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                   const BaseLevelData* loader_data);

 protected:
  static constexpr int GEOM_MAX = 3;

  float frac(float in);

  void debug_vis_draw(int first_root,
                      int tree,
                      int num,
                      int depth,
                      const std::vector<tfrag3::VisNode>& nodes,
                      std::vector<BaseTFragment::DebugVertex>& verts_out);

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

 protected:
  int lod() const { return Gfx::g_global_settings.lod_tfrag; }
  void handle_initialization(DmaFollower& dma);

  bool m_child_mode = false;
  bool m_override_time_of_day = false;
  float m_time_of_days[8] = {1, 0, 0, 0, 0, 0, 0, 0};

  // GS setup data
  u8 m_test_setup[32];

  // VU data
  TFragData m_tfrag_data;

  TfragPcPortData m_pc_port_data;

  // buffers
  TFragBufferedData m_buffered_data[2];

  enum TFragDataMem {
    Buffer0_Start = 0,
    TFragMatrix0 = 5,

    Buffer1_Start = 328,
    TFragMatrix1 = TFragMatrix0 + Buffer1_Start,

    TFragFrameData = 656,
    TFragKickZoneData = 670,
  };

  enum TFragProgMem {
    TFragSetup = 0,
  };

  std::vector<tfrag3::TFragmentTreeKind> m_tree_kinds;
  int m_level_id;

};
