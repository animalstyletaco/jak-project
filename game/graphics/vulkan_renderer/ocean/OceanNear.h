#pragma once

#include "game/graphics/general_renderer/ocean/OceanNear.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/OceanTexture.h"

class OceanNearVulkanJak1 : public BaseOceanNearJak1, public BucketVulkanRenderer {
 public:
  OceanNearVulkanJak1(const std::string& name,
                      int my_id,
                      std::shared_ptr<GraphicsDeviceVulkan> device,
                      VulkanInitializationInfo& vulkan_info)
      : BaseOceanNear(name, my_id),
        BaseOceanNearJak1(name, my_id),
        BucketVulkanRenderer(device, vulkan_info),
        m_texture_renderer(false, device, vulkan_info),
        m_common_ocean_renderer(device, vulkan_info) {
    for (auto& a : m_vu_data) {
      a.fill(0);
    }
  }
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof, VkCommandBuffer command_buffer) override;
  void init_textures(VulkanTexturePool& pool) override;

 private:
  void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;
  void common_ocean_renderer_init_for_near() override;
  void common_ocean_renderer_kick_from_near(const u8* data) override;
  void common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) override;

  OceanVulkanTextureJak1 m_texture_renderer;
  CommonOceanVulkanRendererJak1 m_common_ocean_renderer;
};

class OceanNearVulkanJak2 : public BaseOceanNearJak2, public BucketVulkanRenderer {
 public:
  OceanNearVulkanJak2(const std::string& name,
                      int my_id,
                      std::shared_ptr<GraphicsDeviceVulkan> device,
                      VulkanInitializationInfo& vulkan_info)
      : BaseOceanNear(name, my_id),
        BaseOceanNearJak2(name, my_id),
        BucketVulkanRenderer(device, vulkan_info),
        m_texture_renderer(false, device, vulkan_info),
        m_common_ocean_renderer(device, vulkan_info) {
    for (auto& a : m_vu_data) {
      a.fill(0);
    }
  }
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof, VkCommandBuffer command_buffer) override;
  void init_textures(VulkanTexturePool& pool) override;

 private:
  void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;
  void common_ocean_renderer_init_for_near() override;
  void common_ocean_renderer_kick_from_near(const u8* data) override;
  void common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) override;
  OceanVulkanTextureJak2 m_texture_renderer;
  CommonOceanVulkanRendererJak2 m_common_ocean_renderer;
};
