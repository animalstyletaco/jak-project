#pragma once

#include "game/common/vu.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanMid.h"

class OceanMidVulkan : public BaseOceanMid {
 public:
  OceanMidVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info);
  void run(DmaFollower& dma,
           BaseSharedRenderState* render_state,
           ScopedProfilerNode& prof);

 private:
  void common_ocean_renderer_init_for_mid() override;
  void common_ocean_renderer_kick_from_mid(const u8* data) override;
  void common_ocean_renderer_flush_mid(BaseSharedRenderState* render_state,
                                       ScopedProfilerNode& prof) override;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;

  CommonOceanVulkanRenderer m_common_ocean_renderer;
};