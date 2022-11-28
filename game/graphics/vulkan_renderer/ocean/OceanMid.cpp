#include "OceanMid.h"

OceanMidVulkan::OceanMidVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info) : m_device(device), m_common_ocean_renderer(device, vulkan_info) {
  m_ocean_uniform_vertex_buffer = std::make_unique<CommonOceanVertexUniformBuffer>(m_device, 1, 1);
  m_ocean_uniform_fragment_buffer =
      std::make_unique<CommonOceanFragmentUniformBuffer>(m_device, 1, 1);
}

void OceanMidVulkan::run(DmaFollower& dma,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof) {
  BaseOceanMid::run(dma, render_state, prof);
}

void OceanMidVulkan::common_ocean_renderer_init_for_near() {
  m_common_ocean_renderer.init_for_near();
}
void OceanMidVulkan::common_ocean_renderer_kick_from_near(const u8* data) {
  m_common_ocean_renderer.kick_from_near(data);
}
void OceanMidVulkan::common_ocean_renderer_init_for_mid() {
  m_common_ocean_renderer.init_for_mid();
}
void OceanMidVulkan::common_ocean_renderer_kick_from_mid(const u8* data) {
  m_common_ocean_renderer.kick_from_mid(data);
}
void OceanMidVulkan::common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                                      ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_near(render_state, prof, m_ocean_uniform_vertex_buffer,
                                     m_ocean_uniform_fragment_buffer);
}
void OceanMidVulkan::common_ocean_renderer_flush_mid(BaseSharedRenderState* render_state,
                                                     ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_mid(render_state, prof, m_ocean_uniform_vertex_buffer,
                                    m_ocean_uniform_fragment_buffer);
}
