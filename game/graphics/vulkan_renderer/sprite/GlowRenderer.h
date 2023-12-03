#pragma once

#include "game/graphics/general_renderer/sprite/GlowRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/DescriptorLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"

class GlowVulkanRenderer : public BaseGlowRenderer {
 public:
  GlowVulkanRenderer(std::shared_ptr<GraphicsDeviceVulkan> device,
                     VulkanInitializationInfo& vulkan_info);

  FramebufferVulkanHelper* render_fb = NULL;

 private:
  void blit_depth(BaseSharedRenderState* render_state);

  void draw_probes(BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof,
                   u32 idx_start,
                   u32 idx_end);

  void debug_draw_probes(BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof,
                         u32 idx_start,
                         u32 idx_end);

  void draw_probe_copies(BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof,
                         u32 idx_start,
                         u32 idx_end);

  void debug_draw_probe_copies(BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof,
                               u32 idx_start,
                               u32 idx_end);
  void downsample_chain(BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof,
                        u32 num_sprites);

  void draw_sprites(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

  void InitializeGlowDrawInputAttributes();
  void InitializeGlowProbeInputAttributes();
  void InitializeGlowProbeReadInputAttributes();
  void InitializeGlowProbeReadDebugInputAttributes();
  void InitializeGlowProbeDownsampleInputAttributes();

  void SwitchToShader(ShaderId);

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VulkanInitializationInfo& m_vulkan_info;

  struct DownsampleFramebufferObject {
    std::unique_ptr<SwapChain> swapchain;
    std::unique_ptr<VulkanTexture> texture;
    std::unique_ptr<VulkanSamplerHelper> sampler;
  };

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;

    std::unique_ptr<FramebufferVulkanHelper> probe_fbo;
    std::unique_ptr<VulkanTexture> probe_fbo_rgba_tex;

    // TODO: verify that this is right. Render objects sound more like swap chain images than
    // standard vulkan images
    std::unique_ptr<FramebufferVulkan> probe_fbo_zbuf_rb;
    uint32_t probe_fbo_w = 640;
    uint32_t probe_fbo_h = 480;

    DownsampleFramebufferObject downsample_fbos[kDownsampleIterations];
  } m_ogl;

  struct {
    std::unique_ptr<IndexBuffer> index_buffer;
    std::unique_ptr<VertexBuffer> vertex_buffer;
  } m_ogl_downsampler;

  struct VulkanGraphicsInputData {
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
  };

  VkDescriptorBufferInfo m_fragment_buffer_descriptor_infos[2];
  VkDescriptorImageInfo m_descriptor_image_infos[2];
  VkDescriptorSet m_descriptor_sets[2];

  GraphicsPipelineLayout m_pipeline_layout;

  PipelineConfigInfo m_pipeline_config_info;

  VulkanGraphicsInputData m_glow_probe_vulkan_inputs;
  VulkanGraphicsInputData m_glow_probe_read_vulkan_inputs;
  VulkanGraphicsInputData m_glow_probe_read_debug_vulkan_inputs;
  VulkanGraphicsInputData m_glow_probe_downsample_vulkan_inputs;
  VulkanGraphicsInputData m_glow_draw_vulkan_inputs;
};
