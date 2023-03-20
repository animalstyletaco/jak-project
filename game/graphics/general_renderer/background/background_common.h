#pragma once

#include "common/math/Vector.h"

#include "game/graphics/general_renderer/BucketRenderer.h"

// data passed from game to PC renderers
// the GOAL code assumes this memory layout.
struct TfragPcPortData {
  math::Vector4f planes[4];
  math::Vector<s32, 4> itimes[4];
  math::Vector4f camera[4];
  math::Vector4f hvdf_off;
  math::Vector4f fog;
  math::Vector4f cam_trans;

  math::Vector4f camera_rot[4];
  math::Vector4f camera_perspective[4];

  char level_name[16];
};
static_assert(sizeof(TfragPcPortData) == 16 * 24);

// inputs to background renderers.
struct TfragRenderSettings {
  math::Matrix4f math_camera;
  math::Vector4f hvdf_offset;
  math::Vector4f fog;
  int tree_idx;
  math::Vector<s32, 4> itimes[4];
  math::Vector4f planes[4];
  bool debug_culling = false;
  const u8* occlusion_culling = nullptr;
};

enum class DoubleDrawKind { NONE, AFAIL_NO_DEPTH_WRITE };

struct DoubleDraw {
  DoubleDrawKind kind = DoubleDrawKind::NONE;
  float aref_first = 0.;
  float aref_second = 0.;
  float color_mult = 1.;
};

struct SwizzledTimeOfDay {
  std::vector<u8> data;
  u32 color_count = 0;
};

namespace background_common {

static constexpr int TIME_OF_DAY_COLOR_COUNT = 8192;
// TODO: Come up with better struct name
struct DrawSettings {
  int draw_index = 0;
  int number_of_draws = 0;
};

SwizzledTimeOfDay swizzle_time_of_day(const std::vector<tfrag3::TimeOfDayColor>& in);

void interp_time_of_day_slow(const math::Vector<s32, 4> itimes[4],
                             const std::vector<tfrag3::TimeOfDayColor>& in,
                             math::Vector<u8, 4>* out);

void interp_time_of_day_fast(const math::Vector<s32, 4> itimes[4],
                             const SwizzledTimeOfDay& swizzled_colors,
                             math::Vector<u8, 4>* out);

void cull_check_all_slow(const math::Vector4f* planes,
                         const std::vector<tfrag3::VisNode>& nodes,
                         const u8* level_occlusion_string,
                         u8* out);
bool sphere_in_view_ref(const math::Vector4f& sphere, const math::Vector4f* planes);

void update_render_state_from_pc_settings(BaseSharedRenderState* state, const TfragPcPortData& data);

}  // namespace background_common
