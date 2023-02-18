#pragma once

#include <optional>

#include "common/util/FilteredValue.h"

#include "game/graphics/gfx.h"
#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/background/background_common.h"

struct BaseTieProtoVisibility {
  void init(const std::vector<std::string>& names);
  void update(const u8* data, size_t size);

  std::vector<u8> vis_flags;
  std::unordered_map<std::string, std::vector<u32>> name_to_idx;

  bool all_visible = true;
};

class BaseTie3 : public BaseBucketRenderer {
 public:
   BaseTie3(const std::string& name, int my_id, int level);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;
  virtual ~BaseTie3();

  void render_all_trees(int geom,
                        const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  virtual void render_tree(int idx,
                           int geom,
                           const TfragRenderSettings& settings,
                           BaseSharedRenderState* render_state,
                           ScopedProfilerNode& prof) = 0;
  virtual bool setup_for_level(const std::string& str, BaseSharedRenderState* render_state) = 0;

  struct WindWork {
    u32 paused;
    u32 pad[3];
    math::Vector4f wind_array[64];
    math::Vector4f wind_normal;
    math::Vector4f wind_temp;
    float wind_force[64];
    u32 wind_time;
    u32 pad2[3];
  } m_wind_data;

  int lod() const { return Gfx::g_global_settings.lod_tie; }

 protected:
  virtual void discard_tree_cache() = 0;

  struct Tree {
    u32 vert_count;
    const std::vector<tfrag3::StripDraw>* draws = nullptr;
    const std::vector<tfrag3::InstancedStripDraw>* wind_draws = nullptr;
    const std::vector<tfrag3::TieWindInstance>* instance_info = nullptr;
    const std::vector<tfrag3::TimeOfDayColor>* colors = nullptr;
    const tfrag3::BVH* vis = nullptr;
    const u32* index_data = nullptr;
    SwizzledTimeOfDay tod_cache;

    std::vector<std::array<math::Vector4f, 4>> wind_matrix_cache;

    bool has_wind = false;
    std::vector<u32> wind_vertex_index_offsets;

    bool has_proto_visibility = false;
    BaseTieProtoVisibility proto_visibility;

    struct {
      u32 draws = 0;
      u32 wind_draws = 0;
      Filtered<float> cull_time;
      Filtered<float> index_time;
      Filtered<float> tod_time;
      Filtered<float> proto_vis_time;
      Filtered<float> setup_time;
      Filtered<float> draw_time;
      Filtered<float> tree_time;
    } perf;
  };

  std::array<std::vector<Tree>, 4> m_trees;  // includes 4 lods!
  std::string m_level_name;
  u64 m_load_id = -1;

  std::vector<math::Vector<u8, 4>> m_color_result;

  static constexpr int TIME_OF_DAY_COLOR_COUNT = 8192;

  bool m_has_level = false;
  char m_user_level[255] = "vi1";
  std::optional<std::string> m_pending_user_level = std::nullopt;
  bool m_override_level = false;
  bool m_use_fast_time_of_day = true;
  bool m_debug_wireframe = false;
  bool m_debug_all_visible = false;
  bool m_hide_wind = false;
  Filtered<float> m_all_tree_time;

  TfragPcPortData m_pc_port_data;

  std::vector<float> m_wind_vectors;  // note: I suspect these are shared with shrub.

  float m_wind_multiplier = 1.f;

  int m_level_id;

  static_assert(sizeof(WindWork) == 84 * 16);

  void vector_min_in_place(math::Vector4f& v, float val);
  math::Vector4f vector_max(const math::Vector4f& v, float val);
  void do_wind_math(u16 wind_idx,
                    float* wind_vector_data,
                    const BaseTie3::WindWork& wind_work,
                    float stiffness,
                    std::array<math::Vector4f, 4>& mat);
};

