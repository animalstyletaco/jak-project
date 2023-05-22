#pragma once

#include "common/math/Vector.h"

#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SamplerHelper.h"

struct BackgroundCommonVertexUniformShaderData {
  math::Matrix4f camera;
  math::Vector4f hvdf_offset;
  float fog_constant;
  float fog_min;
  float fog_max;
};

struct BackgroundCommonEtieVertexUniformShaderData {
  math::Matrix4f cam_no_persp;
  math::Vector4f perspective0;
  math::Vector4f perspective1;
  math::Vector4f envmap_tod_tint;
};

class BackgroundCommonEtieVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  BackgroundCommonEtieVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                          uint32_t instanceCount,
                                          VkDeviceSize minOffsetAlignment);
};

struct BackgroundCommonFragmentPushConstantShaderData {
  float alpha_min;
  float alpha_max;
  math::Vector4f fog_color;
};

namespace vulkan_background_common {
void make_all_visible_multidraws(std::vector<VkMultiDrawIndexedInfoEXT>& multiDrawIndexedInfos,
                                 const std::vector<tfrag3::ShrubDraw>& draws);

u32 make_all_visible_multidraws(std::vector<VkMultiDrawIndexedInfoEXT>& multiDrawIndexedInfos,
                                const std::vector<tfrag3::StripDraw>& draws);

u32 make_multidraws_from_vis_string(std::vector<VkMultiDrawIndexedInfoEXT>& multiDrawIndexedInfos,
                                    const std::vector<tfrag3::StripDraw>& draws,
                                    const std::vector<u8>& vis_data);

u32 make_all_visible_index_list(background_common::DrawSettings* group_out,
                                u32* idx_out,
                                const std::vector<tfrag3::StripDraw>& draws,
                                const u32* idx_in,
                                u32* num_tris_out);

u32 make_index_list_from_vis_string(background_common::DrawSettings* group_out,
                                    u32* idx_out,
                                    const std::vector<tfrag3::StripDraw>& draws,
                                    const std::vector<u8>& vis_data,
                                    const u32* idx_in,
                                    u32* num_tris_out);

u32 make_all_visible_index_list(background_common::DrawSettings* group_out,
                                u32* idx_out,
                                const std::vector<tfrag3::ShrubDraw>& draws,
                                const u32* idx_in);

u32 make_multidraws_from_vis_and_proto_string(background_common::DrawSettings* draw_ptrs_out,
                                              GLsizei* counts_out,
                                              void** index_offsets_out,
                                              const std::vector<tfrag3::StripDraw>& draws,
                                              const std::vector<u8>& vis_data,
                                              const std::vector<u8>& proto_vis_data);

u32 make_index_list_from_vis_and_proto_string(background_common::DrawSettings* group_out,
                                              u32* idx_out,
                                              const std::vector<tfrag3::StripDraw>& draws,
                                              const std::vector<u8>& vis_data,
                                              const std::vector<u8>& proto_vis_data,
                                              const u32* idx_in,
                                              u32* num_tris_out);

DoubleDraw setup_tfrag_shader(
    BaseSharedRenderState* render_state,
    DrawMode mode,
    VulkanSamplerHelper& sampler,
    PipelineConfigInfo& pipeline_info,
    BackgroundCommonFragmentPushConstantShaderData& uniform_buffer);
DoubleDraw setup_vulkan_from_draw_mode(DrawMode mode,
                                       VulkanSamplerHelper& sampler,
                                       PipelineConfigInfo& pipeline_config,
                                       bool mipmap);

void first_tfrag_draw_setup(const TfragRenderSettings& settings,
                            BaseSharedRenderState* render_state,
                            BackgroundCommonVertexUniformShaderData* shader_data);

VkDescriptorImageInfo create_placeholder_descriptor_image_info(
    std::unique_ptr<VulkanTexture>& texture,
    std::unique_ptr<VulkanSamplerHelper>& sampler, VkImageType image_type);
}
