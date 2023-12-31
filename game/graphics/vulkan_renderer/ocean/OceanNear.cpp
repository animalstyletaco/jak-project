#include "OceanNear.h"

#include "third-party/imgui/imgui.h"

void OceanNearVulkanJak1::render(DmaFollower& dma,
                                 SharedVulkanRenderState* render_state,
                                 ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_common_ocean_renderer.set_command_buffer(command_buffer);
  BaseOceanNearJak1::render(dma, render_state, prof);
}

void OceanNearVulkanJak2::render(DmaFollower& dma,
                                 SharedVulkanRenderState* render_state,
                                 ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_common_ocean_renderer.set_command_buffer(command_buffer);
  BaseOceanNearJak2::render(dma, render_state, prof);
}

void OceanNearVulkanJak1::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}

void OceanNearVulkanJak2::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}

void OceanNearVulkanJak1::common_ocean_renderer_init_for_near() {
  m_common_ocean_renderer.init_for_near();
}

void OceanNearVulkanJak1::common_ocean_renderer_kick_from_near(const u8* data) {
  m_common_ocean_renderer.kick_from_near(data);
}

void OceanNearVulkanJak1::common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                                           ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_near(render_state, prof);
}

void OceanNearVulkanJak1::texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                                BaseSharedRenderState* render_state,
                                                                ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture(dma, render_state, prof);
}

void OceanNearVulkanJak2::common_ocean_renderer_init_for_near() {
  m_common_ocean_renderer.init_for_near();
}

void OceanNearVulkanJak2::common_ocean_renderer_kick_from_near(const u8* data) {
  m_common_ocean_renderer.kick_from_near(data);
}

void OceanNearVulkanJak2::common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                                           ScopedProfilerNode& prof) {
  m_common_ocean_renderer.flush_near(render_state, prof);
}

void OceanNearVulkanJak2::texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                                BaseSharedRenderState* render_state,
                                                                ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture(dma, render_state, prof);
}
