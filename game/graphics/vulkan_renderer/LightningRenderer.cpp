#include "LightningRenderer.h"

LightningVulkanRenderer::LightningVulkanRenderer(const std::string& name,
                                                 int id,
                                                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                 VulkanInitializationInfo& vulkan_info)
    : BaseLightningRenderer(name, id), BucketVulkanRenderer(device, vulkan_info), m_generic(name, id, device, vulkan_info) {}

LightningVulkanRenderer::~LightningVulkanRenderer() {}

void LightningVulkanRenderer::generic_draw_debug_window() {
  m_generic.draw_debug_window();
}

void LightningVulkanRenderer::render(DmaFollower& dma,
                                     SharedVulkanRenderState* render_state,
                                     ScopedProfilerNode& prof) {
  m_generic.render_in_mode(dma, render_state, prof, BaseGeneric2::Mode::LIGHTNING);
}

void LightningVulkanRenderer::generic_render_in_mode(DmaFollower& dma,
                            BaseSharedRenderState* render_state,
                            ScopedProfilerNode& prof,
                            BaseGeneric2::Mode mode) {
  m_generic.render_in_mode(dma, render_state, prof, mode);
}
