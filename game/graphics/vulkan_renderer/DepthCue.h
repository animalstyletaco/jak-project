#pragma once

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/DepthCue.h"

struct DepthCueVertexUniformData {
  math::Vector4f u_color;
  float u_depth;
};

class DepthCueVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  DepthCueVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                              VkDeviceSize instanceSize,
                              uint32_t instanceCount,
                              VkDeviceSize minOffsetAlignment);
};

class DepthCueVulkan : public BaseDepthCue, public BucketVulkanRenderer {
 public:
  DepthCueVulkan(const std::string& name,
           int my_id,
           std::unique_ptr<GraphicsDeviceVulkan>& device,
           VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

 protected:

  struct {
    // Framebuffer for depth-cue-base-page
    std::unique_ptr<VulkanTexture> fbo;
    std::unique_ptr<VulkanTexture> fbo_texture;
    int fbo_width = 0;
    int fbo_height = 0;

    // Vertex data for drawing to depth-cue-base-page
    std::unique_ptr<VertexBuffer> depth_cue_page_vertex_buffer;

    // Vertex data for drawing to on-screen framebuffer
    std::unique_ptr<VertexBuffer> on_screen_vertex_buffer;

    // Texture to sample the framebuffer from
    std::unique_ptr<VulkanTexture> framebuffer_sample_fbo;
    std::unique_ptr<VulkanTexture> framebuffer_sample_tex;
    int framebuffer_sample_width = 0;
    int framebuffer_sample_height = 0;

    int last_draw_region_w = -1;
    int last_draw_region_h = -1;
    bool last_override_sharpness = false;
    float last_custom_sharpness = 0.999f;
    bool last_force_original_res = false;
    float last_res_scale = 1.0f;
  } m_ogl;

  void graphics_setup() override;
  void setup(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

  std::unique_ptr<DepthCueVertexUniformBuffer> m_depth_cue_vertex_uniform_buffer;
  std::unique_ptr<UniformVulkanBuffer> m_depth_cue_fragment_uniform_buffer;
};
