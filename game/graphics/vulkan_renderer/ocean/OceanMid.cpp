#include "OceanMid.h"

OceanMidVulkan::OceanMidVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info) : m_device(device), m_common_ocean_renderer(device, vulkan_info) {
}

void OceanMidVulkan::run(DmaFollower& dma,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof) {
  BaseOceanMid::run(dma, render_state, prof);
}

void OceanMidVulkan::common_ocean_renderer_init_for_mid() {
  m_common_ocean_renderer.init_for_mid();
}
void OceanMidVulkan::common_ocean_renderer_kick_from_mid(const u8* data) {
  m_common_ocean_renderer.kick_from_mid(data);
}
void OceanMidVulkan::common_ocean_renderer_flush_mid(BaseSharedRenderState* render_state,
                                                     ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_mid(render_state, prof);
}