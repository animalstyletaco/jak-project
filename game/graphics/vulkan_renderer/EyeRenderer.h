#pragma once

#include <memory>
#include <string>

#include "game/graphics/general_renderer/EyeRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"

class EyeVulkanRenderer : public BaseEyeRenderer, public BucketVulkanRenderer {
 public:
  EyeVulkanRenderer(const std::string& name,
                    int my_id,
                    std::shared_ptr<GraphicsDeviceVulkan> device,
                    VulkanInitializationInfo& vulkan_info);
  ~EyeVulkanRenderer();
  void init_textures(VulkanTexturePool& texture_pool) override;
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof, VkCommandBuffer) override;
  void set_command_buffer(VkCommandBuffer command_buffer) { m_command_buffer = command_buffer; };

  VulkanTexture* lookup_eye_texture(u8 eye_id);

  struct EyeVulkanGraphics {
    VulkanGpuTextureMap* texture = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptor_image_info;
    VkDescriptorSet descriptor_set;
  };

  struct SingleEyeDrawsVulkan : SingleEyeDraws {
    EyeVulkanGraphics iris_vulkan_graphics;
    EyeVulkanGraphics pupil_vulkan_graphics;
    EyeVulkanGraphics lid_vulkan_graphics;
  };

 private:
  void run_dma_draws_in_gpu(DmaFollower& dma, BaseSharedRenderState* render_state) override;
  void InitializeInputVertexAttribute();
  void create_pipeline_layout() override;
  void init_shaders();

  struct GpuEyeTex {
    VulkanGpuTextureMap* gpu_texture;
    u32 tbp;
  };
  GpuEyeTex m_gpu_eye_textures[NUM_EYE_PAIRS * 2];

  // xyst per vertex, 4 vertices per square, 3 draws per eye, 11 pairs of eyes, 2 eyes per pair.
  static constexpr int VTX_BUFFER_FLOATS = 4 * 4 * 3 * NUM_EYE_PAIRS * 2;

  void setup_draws(DmaFollower& dma, BaseSharedRenderState* render_state);
  void run_gpu(BaseSharedRenderState* render_state);
  void ExecuteVulkanDraw(VkCommandBuffer commandBuffer,
                         EyeVulkanGraphics& image_info,
                         uint32_t firstVertex,
                         uint32_t vertexCount);

  std::vector<SingleEyeDrawsVulkan> m_single_eye_draws;
  std::unique_ptr<VertexBuffer> m_gpu_vertex_buffer;
  std::unique_ptr<SwapChain> m_swap_chain;

  std::vector<VulkanSamplerHelper> m_sampler_helpers;
  std::vector<VulkanTexture> m_eye_textures;

  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;
  std::unique_ptr<FramebufferVulkanHelper> m_framebuffer_helper;

  VkExtent2D m_eye_renderer_extents;

  float m_gpu_vertex_buffer_data[1920];
};
