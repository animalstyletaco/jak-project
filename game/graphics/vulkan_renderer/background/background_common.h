#pragma once

#include "common/math/Vector.h"

#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SamplerHelper.h"

struct BackgroundCommonVertexUniformShaderData {
  math::Vector4f hvdf_offset;
  math::Matrix4f camera;
  float fog_constant;
  float fog_min;
  float fog_max;
};

class BackgroundCommonVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  BackgroundCommonVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                      uint32_t instanceCount,
                                      VkDeviceSize minOffsetAlignment);
};

struct BackgroundCommonFragmentUniformShaderData {
  float alpha_min;
  float alpha_max;
  math::Vector4f fog_color;
};

class BackgroundCommonFragmentUniformBuffer : public UniformVulkanBuffer {
 public:
  BackgroundCommonFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                        uint32_t instanceCount,
                                        VkDeviceSize minOffsetAlignment);
};

namespace vulkan_background_common {
void make_all_visible_multidraws(std::vector<VkMultiDrawIndexedInfoEXT>& multiDrawIndexedInfos,
                                 const std::vector<tfrag3::ShrubDraw>& draws);

u32 make_all_visible_multidraws(std::vector<VkMultiDrawIndexedInfoEXT>& multiDrawIndexedInfos,
                                const std::vector<tfrag3::StripDraw>& draws);

u32 make_multidraws_from_vis_string(std::vector<VkMultiDrawIndexedInfoEXT>& multiDrawIndexedInfos,
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

DoubleDraw setup_tfrag_shader(
    BaseSharedRenderState* render_state,
    DrawMode mode,
    VulkanTexture* texture,
    VulkanSamplerHelper& sampler,
    PipelineConfigInfo& pipeline_info,
    std::unique_ptr<BackgroundCommonFragmentUniformBuffer>& uniform_buffer);
DoubleDraw setup_vulkan_from_draw_mode(DrawMode mode,
                                       VulkanTexture* texture,
                                       VulkanSamplerHelper& sampler,
                                       PipelineConfigInfo& pipeline_config,
                                       bool mipmap);

void first_tfrag_draw_setup(const TfragRenderSettings& settings,
                            BaseSharedRenderState* render_state,
                            std::unique_ptr<BackgroundCommonVertexUniformBuffer>& uniform_buffer);
}
