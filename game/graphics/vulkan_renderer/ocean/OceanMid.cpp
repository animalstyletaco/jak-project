#include "OceanMid.h"

OceanMidVulkanJak1::OceanMidVulkanJak1(std::shared_ptr<GraphicsDeviceVulkan> device,
                                       VulkanInitializationInfo& vulkan_info)
    : m_common_ocean_renderer(device, vulkan_info) {}

OceanMidVulkanJak2::OceanMidVulkanJak2(std::shared_ptr<GraphicsDeviceVulkan> device,
                                       VulkanInitializationInfo& vulkan_info)
    : m_common_ocean_renderer(device, vulkan_info) {}

void OceanMidVulkanJak1::run(DmaFollower& dma,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof) {
  BaseOceanMidJak1::run(dma, render_state, prof);
}

void OceanMidVulkanJak2::run(DmaFollower& dma,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof) {
  BaseOceanMidJak2::run(dma, render_state, prof);
}

void OceanMidVulkanJak1::common_ocean_renderer_init_for_mid() {
  m_common_ocean_renderer.init_for_mid();
}
void OceanMidVulkanJak1::common_ocean_renderer_kick_from_mid(const u8* data) {
  m_common_ocean_renderer.kick_from_mid(data);
}
void OceanMidVulkanJak1::common_ocean_renderer_flush_mid(BaseSharedRenderState* render_state,
                                                         ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_mid(render_state, prof);
}

void OceanMidVulkanJak2::common_ocean_renderer_init_for_mid() {
  m_common_ocean_renderer.init_for_mid();
}
void OceanMidVulkanJak2::common_ocean_renderer_kick_from_mid(const u8* data) {
  m_common_ocean_renderer.kick_from_mid(data);
}
void OceanMidVulkanJak2::common_ocean_renderer_flush_mid(BaseSharedRenderState* render_state,
                                                         ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_mid(render_state, prof);
}
