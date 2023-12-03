#include "Generic2BucketRenderer.h"

GenericVulkan2BucketRenderer::GenericVulkan2BucketRenderer(
    const std::string& name,
    int id,
    std::shared_ptr<GraphicsDeviceVulkan> device,
    VulkanInitializationInfo& vulkan_info,
    BaseGeneric2::Mode mode,
    std::shared_ptr<GenericVulkan2> generic_renderer)
    : BaseGeneric2BucketRenderer(name, id, mode),
      BucketVulkanRenderer(device, vulkan_info),
      m_generic(generic_renderer) {}

void GenericVulkan2BucketRenderer::render(DmaFollower& dma,
                                          SharedVulkanRenderState* render_state,
                                          ScopedProfilerNode& prof) {
  m_generic->render_in_mode(dma, render_state, prof, m_mode);
}

void GenericVulkan2BucketRenderer::draw_debug_window() {
  m_generic->draw_debug_window();
}
