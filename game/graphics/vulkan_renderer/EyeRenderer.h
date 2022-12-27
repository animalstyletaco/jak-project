#pragma once

#include <string>
#include <memory>

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/EyeRenderer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

class EyeRendererUniformBuffer : public UniformVulkanBuffer {
 public:
  EyeRendererUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VkDeviceSize minOffsetAlignment = 1);
};

class EyeVulkanRenderer : public BaseEyeRenderer, public BucketVulkanRenderer {
 public:
  EyeVulkanRenderer(const std::string& name,
                    int my_id,
                    std::unique_ptr<GraphicsDeviceVulkan>& device,
                    VulkanInitializationInfo& vulkan_info);
  void init_textures(VulkanTexturePool& texture_pool) override;
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;

  struct SingleEyeDrawsVulkan : SingleEyeDraws {
    VulkanGpuTextureMap* iris_tex = VK_NULL_HANDLE;
    VulkanGpuTextureMap* pupil_tex = VK_NULL_HANDLE;
    VulkanGpuTextureMap* lid_tex = VK_NULL_HANDLE;
  };

 private:
  void run_dma_draws_in_gpu(DmaFollower& dma,
                            BaseSharedRenderState* render_state) override;
  void InitializeInputVertexAttribute();

  struct CpuEyeTextures {
    std::unique_ptr<VulkanTexture> texture;
    VulkanGpuTextureMap* gpu_texture = nullptr;
    u32 tbp = 0;
  };

  std::array<CpuEyeTextures, NUM_EYE_PAIRS * 2> m_cpu_eye_textures;

  struct GpuEyeTex {
    VulkanGpuTextureMap* gpu_texture;
    u32 tbp;
    FramebufferVulkanTexturePair fb;

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
  void run_cpu(const std::vector<SingleEyeDrawsVulkan>& draws, BaseSharedRenderState* render_state);
  void run_gpu(const std::vector<SingleEyeDrawsVulkan>& draws, BaseSharedRenderState* render_state);

  std::unique_ptr<VertexBuffer> m_gpu_vertex_buffer;
  std::unique_ptr<EyeRendererUniformBuffer> m_uniform_buffer;
};
