#pragma once

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

/*!
 * The BlitDisplays renderer does various blitting and effects on the previous frame
 */
class BlitDisplaysVulkan : public BucketVulkanRenderer {
 public:
  BlitDisplaysVulkan(const std::string& name, int my_id, std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  //void init_textures(VulkanTexturePool& texture_pool, GameVersion) override;
  void draw_debug_window();

 private:
  VulkanTexture m_texture;
  VulkanSamplerHelper m_sampler_helper;

  VulkanGpuTextureMap* m_gpu_tex = nullptr;
  u32 m_tbp;
};
