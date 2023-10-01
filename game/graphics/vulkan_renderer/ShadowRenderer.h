#pragma once

#include "game/common/vu.h"
#include "game/graphics/general_renderer/ShadowRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class ShadowVulkanRenderer : public BaseShadowRenderer, public BucketVulkanRenderer {
 public:
  ShadowVulkanRenderer(const std::string& name,
                 int my_id,
                 std::shared_ptr<GraphicsDeviceVulkan> device,
                 VulkanInitializationInfo& vulkan_info);
  ~ShadowVulkanRenderer();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_shaders(VulkanShaderLibrary& shaders) override;

 protected:
  void InitializeInputVertexAttribute();
  void draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void create_pipeline_layout() override;
  void PrepareVulkanDraw(uint32_t& pipelineLayoutId, uint32_t indexBufferId);

  struct {
    // index is front, back
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffers[2];
  } m_ogl;

  bool m_debug_draw_volume = false;

  math::Vector4f m_color_uniform;
};
