#pragma once

#include "game/common/vu.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanTexture.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"

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
  void draw_debug_window();
  ~OceanVulkanTexture();

 private:
  void InitializeVertexBuffer();
  void SetupShader(ShaderId);
  void InitializeMipmapVertexInputAttributes();
  void CreatePipelineLayout();

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

  VkDescriptorImageInfo m_descriptor_image_info;
  std::array<VkDescriptorImageInfo, NUM_MIPS> m_mipmap_descriptor_image_infos;

  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;

  std::unique_ptr<GraphicsPipelineLayout> m_ocean_texture_graphics_pipeline_layout;
  std::vector<GraphicsPipelineLayout> m_ocean_texture_mipmap_graphics_pipeline_layouts;

  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  PipelineConfigInfo m_pipeline_info;
  VulkanInitializationInfo& m_vulkan_info;
  FramebufferVulkanHelper m_result_texture;
  FramebufferVulkanHelper m_temp_texture;

  VkPipelineLayout m_ocean_texture_pipeline_layout;
  VkPipelineLayout m_ocean_texture_mipmap_pipeline_layout;

  std::vector<VkVertexInputAttributeDescription>
      m_ocean_texture_mipmap_input_attribute_descriptions;
  std::vector<VkVertexInputAttributeDescription> m_ocean_texture_input_attribute_descriptions;

  std::vector<VkVertexInputBindingDescription>
      m_ocean_texture_input_binding_attribute_descriptions;
  VkVertexInputBindingDescription m_ocean_texture_mipmap_input_binding_attribute_description;

  VkDescriptorSet m_ocean_texture_descriptor_set;
  std::array<VkDescriptorSet, NUM_MIPS> m_ocean_mipmap_texture_descriptor_sets;
  VulkanSamplerHelper m_sampler_helper{m_device};
};
