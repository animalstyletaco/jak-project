#pragma once

#include "game/common/vu.h"
#include "game/graphics/general_renderer/ShadowRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class ShadowRendererUniformBuffer : public UniformVulkanBuffer {
 public:
  ShadowRendererUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                              uint32_t instanceCount,
                              VkDeviceSize minOffsetAlignment = 1);
};

class ShadowVulkanRenderer : public BaseShadowRenderer, public BucketVulkanRenderer {
 public:
  ShadowVulkanRenderer(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& vulkan_info);
  ~ShadowVulkanRenderer();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_shaders(VulkanShaderLibrary& shaders) override;

 protected:
  void InitializeInputVertexAttribute();
  void draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void create_pipeline_layout() override;
  void VulkanDrawWithIndexBufferId(uint32_t indexBufferId);

  struct {
    // index is front, back
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffers[2];
  } m_ogl;

  bool m_debug_draw_volume = false;
  std::unique_ptr<ShadowRendererUniformBuffer> m_uniform_buffer;
};
