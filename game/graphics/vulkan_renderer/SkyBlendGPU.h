
#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/SkyBlendGPU.h"
#include "game/graphics/general_renderer/SkyBlendCommon.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"

class SkyBlendVulkanGPU : BaseSkyBlendGPU {
 public:
  SkyBlendVulkanGPU(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info);
  ~SkyBlendVulkanGPU();
  void init_textures(VulkanTexturePool& tex_pool);
  SkyBlendStats do_sky_blends(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof);

 private:
  void create_pipeline_layout();

  struct PushConstant {
    float height_scale;
    float scissor_adjust;
  };

  PushConstant m_push_constant;

  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;
  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;

  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<VulkanSamplerHelper> m_sampler_helpers[2];
  std::unique_ptr<FramebufferVulkanHelper> m_framebuffers[2];
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<GraphicsPipelineLayout> m_graphics_pipeline_layouts[2];

  VkDescriptorImageInfo m_descriptor_image_infos[2];
  VkDescriptorSet m_fragment_descriptor_sets[2];

  VulkanInitializationInfo& m_vulkan_info;
  PipelineConfigInfo m_pipeline_config_info;

  unsigned m_sizes[2] = {32, 64};

  struct Vertex {
    float x = 0;
    float y = 0;
    float intensity = 0;
  };

  struct TexInfo {
    VulkanGpuTextureMap* tex;
    u32 tbp;
  } m_tex_info[2];
};
