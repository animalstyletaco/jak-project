#pragma once

#include "common/math/Vector.h"

#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

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
  int32_t tex_T0;
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

namespace background_common {
DoubleDraw setup_tfrag_shader(
    BaseSharedRenderState* render_state,
    DrawMode mode,
    VulkanTexture* texture,
    PipelineConfigInfo& pipeline_info,
    std::unique_ptr<BackgroundCommonFragmentUniformBuffer>& uniform_buffer);
DoubleDraw setup_vulkan_from_draw_mode(DrawMode mode,
                                       VulkanTexture* texture,
                                       PipelineConfigInfo& pipeline_config,
                                       bool mipmap);

void first_tfrag_draw_setup(const TfragRenderSettings& settings,
                            BaseSharedRenderState* render_state,
                            std::unique_ptr<BackgroundCommonVertexUniformBuffer>& uniform_buffer);
}  // namespace vulkan_renderer
