#include "Merc2BucketRenderer.h"

MercVulkan2BucketRenderer::MercVulkan2BucketRenderer(const std::string& name,
                                                   int my_id,
                                                   std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                   VulkanInitializationInfo& vulkan_info,
                                                   std::shared_ptr<MercVulkan2> merc2) : BaseMerc2BucketRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info), m_merc2(merc2) {
}

void MercVulkan2BucketRenderer::render(DmaFollower& dma,
                                 SharedVulkanRenderState* render_state,
                                 ScopedProfilerNode& prof) {
  BaseMerc2BucketRenderer::render(dma, render_state, prof);
}

void MercVulkan2BucketRenderer::merc2_render(DmaFollower& dma,
                  BaseSharedRenderState* render_state,
                  ScopedProfilerNode& prof,
                  BaseMercDebugStats* debug_stats) {
  m_merc2->render(dma, render_state, prof, debug_stats);
}

void MercVulkan2BucketRenderer::draw_debug_window() {
  m_merc2->draw_debug_window();
}
