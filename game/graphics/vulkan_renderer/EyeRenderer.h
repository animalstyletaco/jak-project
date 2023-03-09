#pragma once

#include <string>
#include <memory>

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/EyeRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"

class EyeVulkanRenderer : public BaseEyeRenderer, public BucketVulkanRenderer {
 public:
  EyeVulkanRenderer(const std::string& name,
                    int my_id,
                    std::unique_ptr<GraphicsDeviceVulkan>& device,
                    VulkanInitializationInfo& vulkan_info);
  ~EyeVulkanRenderer();
  void init_textures(VulkanTexturePool& texture_pool) override;
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;

  struct EyeVulkanGraphics {
    EyeVulkanGraphics(std::unique_ptr<GraphicsDeviceVulkan>& device,
                      std::unique_ptr<DescriptorLayout>& setLayout,
                      VulkanInitializationInfo& vulkan_info)
        : pipeline_layout(device), descriptor_writer(setLayout, vulkan_info.descriptor_pool) {
      descriptor_writer.build(descriptor_set);
      descriptor_writer.writeImage(
          0, vulkan_info.texture_pool->get_placeholder_descriptor_image_info()); 
    }
    VulkanGpuTextureMap* texture = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptor_image_info;
    GraphicsPipelineLayout pipeline_layout;
    VkDescriptorSet descriptor_set;
    DescriptorWriter descriptor_writer;
  };

  struct SingleEyeDrawsVulkan : SingleEyeDraws {
    SingleEyeDrawsVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device,
                         std::unique_ptr<DescriptorLayout>& layout,
                         VulkanInitializationInfo& vulkan_info)
        : iris_vulkan_graphics(device, layout, vulkan_info),
          pupil_vulkan_graphics(device, layout, vulkan_info),
          lid_vulkan_graphics(device, layout, vulkan_info) {
    }                                                   

    EyeVulkanGraphics iris_vulkan_graphics;
    EyeVulkanGraphics pupil_vulkan_graphics;
    EyeVulkanGraphics lid_vulkan_graphics;
  };

 private:
  void run_dma_draws_in_gpu(DmaFollower& dma,
                            BaseSharedRenderState* render_state) override;
  void InitializeInputVertexAttribute();
  void create_pipeline_layout() override;
  void init_shaders();

  struct CpuEyeTextures {
    std::unique_ptr<VulkanTexture> texture;
    VulkanGpuTextureMap* gpu_texture = nullptr;
    u32 tbp = 0;
  };

  std::array<CpuEyeTextures, NUM_EYE_PAIRS * 2> m_cpu_eye_textures;

  struct GpuEyeTex {
    VulkanGpuTextureMap* gpu_texture;
    u32 tbp;
    FramebufferVulkanHelper fb;

    GpuEyeTex(std::unique_ptr<GraphicsDeviceVulkan>& device);
  };
  std::unique_ptr<GpuEyeTex> m_gpu_eye_textures[NUM_EYE_PAIRS * 2];

  
  template <bool blend, bool bilinear>
  void draw_eye_impl(u32* out,
                     const BaseEyeRenderer::EyeDraw& draw,
                     VulkanTexture* tex,
                     int pair,
                     int lr,
                     bool flipx);

  template <bool blend>
  void draw_eye(u32* out,
                const BaseEyeRenderer::EyeDraw& draw,
                VulkanTexture* tex,
                int pair,
                int lr,
                bool flipx,
                bool bilinear);

  // xyst per vertex, 4 vertices per square, 3 draws per eye, 11 pairs of eyes, 2 eyes per pair.
  static constexpr int VTX_BUFFER_FLOATS = 4 * 4 * 3 * NUM_EYE_PAIRS * 2;

  std::vector<SingleEyeDrawsVulkan> get_draws(DmaFollower& dma, BaseSharedRenderState* render_state);
  void run_cpu(std::vector<SingleEyeDrawsVulkan>& draws, BaseSharedRenderState* render_state);
  void run_gpu(std::vector<SingleEyeDrawsVulkan>& draws, BaseSharedRenderState* render_state);
  void ExecuteVulkanDraw(VkCommandBuffer commandBuffer,
                         EyeVulkanGraphics& image_info,
                         uint32_t firstVertex,
                         uint32_t vertexCount);

  std::unique_ptr<VertexBuffer> m_gpu_vertex_buffer;
  std::unique_ptr<SwapChain> m_swap_chain;

  VkExtent2D m_eye_renderer_extents;
  VkExtent2D m_original_swap_chain_extents;

  bool isFrameStarted = false;
  uint32_t eye_renderer_frame_count = 0;
};
