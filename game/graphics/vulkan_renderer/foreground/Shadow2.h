#pragma once

#include "game/graphics/general_renderer/foreground/Shadow2.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class ShadowVulkan2 : public BucketVulkanRenderer, public BaseShadow2 {
 public:
  ShadowVulkan2(const std::string& name,
                int my_id,
                std::unique_ptr<GraphicsDeviceVulkan>& device,
                VulkanInitializationInfo& vulkan_info);
  ~ShadowVulkan2();
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;

 private:
  void init_shaders();
  void create_pipeline_layout() override;
  void InitializeInputAttributes();

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffers[2];
    Shadow2VertexPushConstantData vertex_push_constant;
    math::Vector4f fragment_push_constant;

    std::unique_ptr<GraphicsPipelineLayout> front_graphics_pipeline_layout;
    std::unique_ptr<GraphicsPipelineLayout> front_debug_graphics_pipeline_layout;
    std::unique_ptr<GraphicsPipelineLayout> back_graphics_pipeline_layout;
    std::unique_ptr<GraphicsPipelineLayout> back_debug_graphics_pipeline_layout;

    std::unique_ptr<GraphicsPipelineLayout> lighten_graphics_pipeline_layout;
    std::unique_ptr<GraphicsPipelineLayout> darken_graphics_pipeline_layout;
  } m_ogl;

  void draw_buffers(BaseSharedRenderState* render_state,
                    ScopedProfilerNode& prof,
                    const FrameConstants& constants) override;
  VkColorComponentFlags GetColorMaskSettings(bool red_enabled,
                                             bool green_enabled,
                                             bool blue_enabled,
                                             bool alpha_enabled);
  void ShadowVulkan2::PrepareVulkanDraw(std::unique_ptr<GraphicsPipelineLayout>&);
};
