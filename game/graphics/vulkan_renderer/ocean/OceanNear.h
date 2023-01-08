#pragma once

#include "game/graphics/general_renderer/ocean/OceanNear.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/OceanTexture.h"

class OceanNearVulkan : public BaseOceanNear, public BucketVulkanRenderer {
 public:
  OceanNearVulkan(const std::string& name,
            int my_id,
            std::unique_ptr<GraphicsDeviceVulkan>& device,
            VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_textures(VulkanTexturePool& pool) override;

 private:
  void common_ocean_renderer_init_for_near() override;
  void common_ocean_renderer_kick_from_near(const u8* data) override;
  void common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) override;
  void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;

  OceanVulkanTexture m_texture_renderer;
  CommonOceanVulkanRenderer m_common_ocean_renderer;
};
