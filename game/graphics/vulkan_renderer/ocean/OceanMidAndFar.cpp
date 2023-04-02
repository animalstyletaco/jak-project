#include "OceanMidAndFar.h"

#include "third-party/imgui/imgui.h"

OceanVulkanMidAndFar::OceanVulkanMidAndFar(const std::string& name,
                               int my_id,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               VulkanInitializationInfo& vulkan_info)
    : BaseOceanMidAndFar(name, my_id), BucketVulkanRenderer(device, vulkan_info),
      m_direct(name, my_id, device, vulkan_info, 4096),
      m_texture_renderer(true, device, vulkan_info), m_mid_renderer(device, vulkan_info) {
}

void OceanVulkanMidAndFar::draw_debug_window() {
  m_texture_renderer.draw_debug_window();
  m_direct.draw_debug_window();
}

void OceanVulkanMidAndFar::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_direct_renderer_call_count = 0;
  m_direct.set_current_index(m_direct_renderer_call_count);
  BaseOceanMidAndFar::render(dma, render_state, prof);
}

void OceanVulkanMidAndFar::init_textures(VulkanTexturePool& pool) {
  m_texture_renderer.init_textures(pool);
}

void OceanVulkanMidAndFar::ocean_mid_renderer_run(DmaFollower& dma,
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

void OceanVulkanMidAndFar::texture_renderer_handle_ocean_texture_jak1(DmaFollower& dma,
                                                                 BaseSharedRenderState* render_state,
                                                                 ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture_jak1(dma, render_state, prof);
}

void OceanVulkanMidAndFar::texture_renderer_handle_ocean_texture_jak2(
    DmaFollower& dma,
    BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) {
  m_texture_renderer.handle_ocean_texture_jak2(dma, render_state, prof);
}
