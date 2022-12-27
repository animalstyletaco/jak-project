#pragma once

#include "game/common/vu.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanTexture.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"

class OceanMipMapVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  OceanMipMapVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                 uint32_t instanceCount,
                                 VkDeviceSize minOffsetAlignment = 1);
};

class OceanMipMapFragmentUniformBuffer : public UniformVulkanBuffer {
 public:
  OceanMipMapFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                   uint32_t instanceCount,
                                   VkDeviceSize minOffsetAlignment = 1);
};


class OceanVulkanTexture : public BaseOceanTexture {
 public:
  OceanVulkanTexture(bool generate_mipmaps,
               std::unique_ptr<GraphicsDeviceVulkan>& device,
               VulkanInitializationInfo& vulkan_info);
  void handle_ocean_texture(
      DmaFollower& dma,
      BaseSharedRenderState* render_state,
      ScopedProfilerNode& prof);
  void init_textures(VulkanTexturePool& pool);
  void set_gpu_texture(TextureInput&) override;
  void draw_debug_window();
  ~OceanVulkanTexture();

 private:
  void InitializeVertexBuffer();
  void SetupShader(ShaderId);
  void InitializeMipmapVertexInputAttributes();

  void move_existing_to_vram(u32 slot_addr) override;
  void setup_framebuffer_context(int) override;

  void flush(BaseSharedRenderState* render_state,
             ScopedProfilerNode& prof) override;

  void make_texture_with_mipmaps(BaseSharedRenderState* render_state,
                                 ScopedProfilerNode& prof) override;

  VulkanGpuTextureMap* m_tex0_gpu = nullptr;

  struct PcDataVulkan {
    std::unique_ptr<VertexBuffer> static_vertex_buffer;
    std::unique_ptr<VertexBuffer> dynamic_vertex_buffer;
    std::unique_ptr<IndexBuffer> graphics_index_buffer;
  } m_vulkan_pc;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<CommonOceanVertexUniformBuffer> m_common_uniform_vertex_buffer;
  std::unique_ptr<CommonOceanFragmentUniformBuffer> m_common_uniform_fragment_buffer;

  std::unique_ptr<OceanMipMapVertexUniformBuffer> m_ocean_mipmap_uniform_vertex_buffer;
  std::unique_ptr<OceanMipMapFragmentUniformBuffer> m_ocean_mipmap_uniform_fragment_buffer;

  std::unique_ptr<GraphicsPipelineLayout> m_pipeline_layout;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  PipelineConfigInfo m_pipeline_info;
  VulkanInitializationInfo& m_vulkan_info;
  FramebufferVulkanTexturePair m_result_texture;
  FramebufferVulkanTexturePair m_temp_texture;
};
