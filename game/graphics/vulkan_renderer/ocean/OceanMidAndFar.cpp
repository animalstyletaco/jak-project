#include "OceanMidAndFar.h"

#include "third-party/imgui/imgui.h"

OceanVulkanMidAndFar::OceanVulkanMidAndFar(const std::string& name,
                               int my_id,
                               std::shared_ptr<GraphicsDeviceVulkan> device,
                               VulkanInitializationInfo& vulkan_info)
    : BaseOceanMidAndFar(name, my_id), BucketVulkanRenderer(device, vulkan_info),
      m_direct(name, my_id, device, vulkan_info, 4096) {
}

void OceanVulkanMidAndFarJak1::draw_debug_window() {
  m_texture_renderer.draw_debug_window();
  m_direct.draw_debug_window();
}

void OceanVulkanMidAndFarJak2::draw_debug_window() {
  m_texture_renderer.draw_debug_window();
  m_direct.draw_debug_window();
}

void OceanVulkanMidAndFarJak1::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_direct_renderer_call_count = 0;
  m_direct.set_current_index(m_direct_renderer_call_count);
  BaseOceanMidAndFarJak1::render(dma, render_state, prof);
}

void OceanVulkanMidAndFarJak2::render(DmaFollower& dma,
                                      SharedVulkanRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  m_direct_renderer_call_count = 0;
  m_direct.set_current_index(m_direct_renderer_call_count);
  BaseOceanMidAndFarJak2::render(dma, render_state, prof);
}

void OceanVulkanMidAndFarJak1::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}

void OceanVulkanMidAndFarJak2::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}

void OceanVulkanMidAndFarJak1::ocean_mid_renderer_run(DmaFollower& dma,
                                                  BaseSharedRenderState* render_state,
                                                  ScopedProfilerNode& prof) {
  m_mid_renderer.run(dma, render_state, prof);
}
void OceanVulkanMidAndFarJak2::ocean_mid_renderer_run(DmaFollower& dma,
                                                  BaseSharedRenderState* render_state,
                                                  ScopedProfilerNode& prof) {
  m_mid_renderer.run(dma, render_state, prof);
}
void OceanVulkanMidAndFar::direct_renderer_render_gif(const u8* data,
                                                      u32 size,
                                                      BaseSharedRenderState* render_state,
                                                      ScopedProfilerNode& prof) {
  m_direct.set_current_index(m_direct_renderer_call_count++);
  m_direct.render_gif(data, size, render_state, prof);
}

void OceanVulkanMidAndFar::direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                                         ScopedProfilerNode& prof) {
  m_direct.set_current_index(m_direct_renderer_call_count++);
  m_direct.flush_pending(render_state, prof);
}
void OceanVulkanMidAndFar::direct_renderer_set_mipmap(bool isMipmapEnabled) {
  m_direct.set_mipmap(isMipmapEnabled);
}
void OceanVulkanMidAndFar::direct_renderer_reset_state() {
  m_direct.reset_state();
}

void OceanVulkanMidAndFarJak1::texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                                 BaseSharedRenderState* render_state,
                                                                 ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture(dma, render_state, prof);
}

void OceanVulkanMidAndFarJak2::texture_renderer_handle_ocean_texture(
    DmaFollower& dma,
    BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture(dma, render_state, prof);
}
