#include "OceanNear.h"

#include "third-party/imgui/imgui.h"

OceanNearVulkan::OceanNearVulkan(const std::string& name,
                     int my_id,
                     std::shared_ptr<GraphicsDeviceVulkan> device,
                     VulkanInitializationInfo& vulkan_info)
    : BaseOceanNear(name, my_id), BucketVulkanRenderer(device, vulkan_info), m_common_ocean_renderer(device, vulkan_info) {
  for (auto& a : m_vu_data) {
    a.fill(0);
  }
}

void OceanNearVulkanJak1::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseOceanNearJak1::render(dma, render_state, prof);
}

void OceanNearVulkanJak2::render(DmaFollower& dma,
                             SharedVulkanRenderState* render_state,
                             ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseOceanNearJak2::render(dma, render_state, prof);
}

void OceanNearVulkanJak1::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}
void OceanNearVulkanJak2::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}
void OceanNearVulkan::common_ocean_renderer_init_for_near() {
  m_common_ocean_renderer.init_for_near();
}
void OceanNearVulkan::common_ocean_renderer_kick_from_near(const u8* data) {
  m_common_ocean_renderer.kick_from_near(data);
}
void OceanNearVulkan::common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                                       ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_near(render_state, prof);
}
void OceanNearVulkanJak1::texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                            BaseSharedRenderState* render_state,
                                                            ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture(dma, render_state, prof);
}
void OceanNearVulkanJak2::texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                                BaseSharedRenderState* render_state,
                                                                ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture(dma, render_state, prof);
}

