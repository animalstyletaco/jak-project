#pragma once

#include "game/common/vu.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanTexture.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"

class OceanVulkanTexture : public BaseOceanTexture {
 public:
  OceanVulkanTexture(bool generate_mipmaps,
               std::unique_ptr<GraphicsDeviceVulkan>& device,
               VulkanInitializationInfo& vulkan_info);
  void handle_ocean_texture(
      DmaFollower& dma,
      SharedVulkanRenderState* render_state,
      ScopedProfilerNode& prof,
      std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
      std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer);
  void init_textures(TexturePoolVulkan& pool);
  void set_gpu_texture(TextureInput&) override;
  ~OceanVulkanTexture();

 private:
  void InitializeVertexBuffer();
  void SetupShader();
  void InitializeMipmapVertexInputAttributes();

  void move_existing_to_vram(GpuTexture* tex, u32 slot_addr) override;
  void setup_framebuffer_context(int) override;

  void flush(BaseSharedRenderState* render_state,
             ScopedProfilerNode& prof);

  void make_texture_with_mipmaps(SharedVulkanRenderState* render_state,
                                 ScopedProfilerNode& prof,
                                 std::unique_ptr<CommonOceanFragmentUniformBuffer>&);

  GpuTexture* m_tex0_gpu = nullptr;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<GraphicsPipelineLayout> m_pipeline_layout;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  PipelineConfigInfo m_pipeline_info;
  VulkanInitializationInfo& m_vulkan_info;
  FramebufferVulkanTexturePair m_result_texture;
  FramebufferVulkanTexturePair m_temp_texture;
};
