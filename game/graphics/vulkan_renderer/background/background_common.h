#pragma once

#include "common/math/Vector.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"


struct BackgroundCommonVertexUniformShaderData {
  math::Vector4f hvdf_offset;
  math::Matrix4f camera;
  float fog_constant;
  float fog_min;
  float fog_max;
};

class BackgroundCommonVertexUniformBuffer : public UniformBuffer {
 public:
  BackgroundCommonVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                      uint32_t instanceCount,
                                      VkMemoryPropertyFlags memoryPropertyFlags,
                                      VkDeviceSize minOffsetAlignment);
};

struct BackgroundCommonFragmentUniformShaderData {
  int32_t tex_T0;
  float alpha_min;
  float alpha_max;
  math::Vector4f fog_color;
};

class BackgroundCommonFragmentUniformBuffer : public UniformBuffer {
 public:
  BackgroundCommonFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                        uint32_t instanceCount,
                                        VkMemoryPropertyFlags memoryPropertyFlags,
                                        VkDeviceSize minOffsetAlignment);
};

// data passed from game to PC renderers
// the GOAL code assumes this memory layout.
struct TfragPcPortData {
  math::Vector4f planes[4];
  math::Vector<s32, 4> itimes[4];
  math::Vector4f camera[4];
  math::Vector4f hvdf_off;
  math::Vector4f fog;
  math::Vector4f cam_trans;
  char level_name[16];
};

// inputs to background renderers.
struct TfragRenderSettings {
  math::Matrix4f math_camera;
  math::Vector4f hvdf_offset;
  math::Vector4f fog;
  int tree_idx;
  float time_of_day_weights[8] = {0};
  math::Vector4f planes[4];
  bool debug_culling = false;
  const u8* occlusion_culling = nullptr;
};

enum class DoubleDrawKind { NONE, AFAIL_NO_DEPTH_WRITE };

struct DoubleDraw {
  DoubleDrawKind kind = DoubleDrawKind::NONE;
  float aref_first = 0.;
  float aref_second = 0.;
};

struct SwizzledTimeOfDay {
  std::vector<u8> data;
  u32 color_count = 0;
};

namespace vk_common_background_renderer {
DoubleDraw setup_tfrag_shader(SharedRenderState* render_state,
                              DrawMode mode,
                              TextureInfo& texture,
                              PipelineConfigInfo& pipeline_info,
                              std::unique_ptr<BackgroundCommonFragmentUniformBuffer>& uniform_buffer);
DoubleDraw setup_vulkan_from_draw_mode(DrawMode mode,
                                       TextureInfo& texture,
                                       PipelineConfigInfo& pipeline_config,
                                       bool mipmap);

void first_tfrag_draw_setup(const TfragRenderSettings& settings,
                            SharedRenderState* render_state,
                            std::unique_ptr<BackgroundCommonVertexUniformBuffer>& uniform_buffer);

void interp_time_of_day_slow(const float weights[8],
                             const std::vector<tfrag3::TimeOfDayColor>& in,
                             math::Vector<u8, 4>* out);

SwizzledTimeOfDay swizzle_time_of_day(const std::vector<tfrag3::TimeOfDayColor>& in);

void interp_time_of_day_fast(const float weights[8],
                             const SwizzledTimeOfDay& in,
                             math::Vector<u8, 4>* out);

void cull_check_all_slow(const math::Vector4f* planes,
                         const std::vector<tfrag3::VisNode>& nodes,
                         const u8* level_occlusion_string,
                         u8* out);
bool sphere_in_view_ref(const math::Vector4f& sphere, const math::Vector4f* planes);

void update_render_state_from_pc_settings(SharedRenderState* state, const TfragPcPortData& data);

void make_all_visible_multidraws(std::pair<int, int>* draw_ptrs_out,
                                 GLsizei* counts_out,
                                 void** index_offsets_out,
                                 const std::vector<tfrag3::ShrubDraw>& draws);

u32 make_all_visible_multidraws(std::pair<int, int>* draw_ptrs_out,
                                GLsizei* counts_out,
                                void** index_offsets_out,
                                const std::vector<tfrag3::StripDraw>& draws);

u32 make_multidraws_from_vis_string(std::pair<int, int>* draw_ptrs_out,
                                    GLsizei* counts_out,
                                    void** index_offsets_out,
                                    const std::vector<tfrag3::StripDraw>& draws,
                                    const std::vector<u8>& vis_data);

u32 make_all_visible_index_list(std::pair<int, int>* group_out,
                                u32* idx_out,
                                const std::vector<tfrag3::StripDraw>& draws,
                                const u32* idx_in,
                                u32* num_tris_out);

u32 make_index_list_from_vis_string(std::pair<int, int>* group_out,
                                    u32* idx_out,
                                    const std::vector<tfrag3::StripDraw>& draws,
                                    const std::vector<u8>& vis_data,
                                    const u32* idx_in,
                                    u32* num_tris_out);

u32 make_all_visible_index_list(std::pair<int, int>* group_out,
                                u32* idx_out,
                                const std::vector<tfrag3::ShrubDraw>& draws,
                                const u32* idx_in);
}  // namespace vk_common_background_renderer