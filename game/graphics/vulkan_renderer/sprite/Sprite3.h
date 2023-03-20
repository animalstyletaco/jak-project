
#pragma once

#include <map>

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/vulkan_renderer/FramebufferHelper.h"
#include "game/graphics/general_renderer/sprite/Sprite3.h"
#include "game/graphics/vulkan_renderer/sprite/SpriteCommon.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"
#include "game/graphics/vulkan_renderer/sprite/GlowRenderer.h"

class SpriteDistortInstancedVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  SpriteDistortInstancedVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                            VkDeviceSize minOffsetAlignment);
};

class SpriteDistortInstancedFragmentUniformBuffer : public UniformVulkanBuffer {
 public:
  SpriteDistortInstancedFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                              VkDeviceSize minOffsetAlignment);
};

class SpriteVulkan3 : public BaseSprite3, public BucketVulkanRenderer {
 public:
  SpriteVulkan3(const std::string& name,
          int my_id,
          std::unique_ptr<GraphicsDeviceVulkan>& device,
          VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void SetupShader(ShaderId shaderId) override;

 protected:
  void setup_graphics_for_2d_group_0_render() override;

 private:
  void InitializeInputVertexAttribute();
  void graphics_setup() override;
  void graphics_setup_normal() override;
  void graphics_setup_distort() override;

  void glow_renderer_cancel_sprite() override;
  SpriteGlowOutput* glow_renderer_alloc_sprite() override;
  void glow_renderer_flush(BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) override;

  void distort_draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void distort_draw_instanced(BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) override;
  void distort_draw_common(BaseSharedRenderState* render_state,
                           ScopedProfilerNode& prof) override;
  void distort_setup_framebuffer_dims(BaseSharedRenderState* render_state) override;

  void flush_sprites(BaseSharedRenderState* render_state,
                     ScopedProfilerNode& prof,
                     bool double_draw) override;

  struct VulkanDistortOgl : BaseSprite3::GraphicsDistortOgl {
    std::unique_ptr<VertexBuffer>
        vertex_buffer;  // contains vertex data for each possible sprite resolution (3-11)
    std::unique_ptr<IndexBuffer>
        index_buffer;  // contains all instance specific data for each sprite per frame

    std::unique_ptr<FramebufferVulkan> fbo;
    std::unique_ptr<VulkanTexture> fbo_texture;
  };
  VulkanDistortOgl m_distort_ogl;

  struct VulkanDistortInstancedOgl {
    std::unique_ptr<VertexBuffer>
        vertex_buffer;  // contains vertex data for each possible sprite resolution (3-11)
    std::unique_ptr<VertexBuffer>
        instance_buffer;  // contains all instance specific data for each sprite per frame

  } m_vulkan_distort_instanced_ogl;

  void direct_renderer_reset_state() override;
  void direct_renderer_render_vif(u32 vif0,
                                  u32 vif1,
                                  const u8* data,
                                  u32 size,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) override;
  void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& prof) override;
  void SetSprite3UniformVertexFourFloatVector(const char* name,
                                              u32 numberOfFloats,
                                              float* data,
                                              u32 flags = 0) override;
  void SetSprite3UniformMatrixFourFloatVector(const char* name,
                                              u32 numberOfFloats,
                                              bool isTransponsedMatrix,
                                              float* data,
                                              u32 flags = 0) override;

  void EnableSprite3GraphicsBlending() override;

  DirectVulkanRenderer m_direct;
  GlowVulkanRenderer m_glow_renderer;

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
  } m_ogl;

  GraphicsPipelineLayout m_distorted_pipeline_layout;
  GraphicsPipelineLayout m_distorted_instance_pipeline_layout;

  std::unique_ptr<Sprite3dVertexUniformBuffer> m_sprite_3d_vertex_uniform_buffer;
  std::unique_ptr<Sprite3dFragmentUniformBuffer> m_sprite_3d_fragment_uniform_buffer;

  VulkanSamplerHelper m_sampler_helper;
  VulkanSamplerHelper m_distort_sampler_helper;

  std::unique_ptr<SpriteDistortInstancedVertexUniformBuffer>
      m_sprite_3d_instanced_vertex_uniform_buffer;
  std::unique_ptr<SpriteDistortInstancedFragmentUniformBuffer>
      m_sprite_3d_instanced_fragment_uniform_buffer;
};
