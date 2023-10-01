#include "Generic2BucketRenderer.h"

GenericVulkan2BucketRenderer::GenericVulkan2BucketRenderer(
    const std::string& name,
    int id,
    std::shared_ptr<GraphicsDeviceVulkan> device,
    VulkanInitializationInfo& vulkan_info,
    BaseGeneric2::Mode mode,
    std::shared_ptr<GenericVulkan2> generic_renderer)
    : BaseGeneric2BucketRenderer(name, id, mode),
      BucketRenderer(device, vulkan_info),
      m_generic(generic_renderer) {
}

void GenericVulkan2BucketRenderer::render(DmaFollower& dma,
                                    SharedVulkanRenderState* render_state,
                                    ScopedProfilerNode& prof) {
  BaseGeneric2BucketRenderer::render(dma, render_state, prof);
}

void GenericVulkan2BucketRendererJak1::generic_render(DmaFollower& dma,
                                                BaseSharedRenderState* render_state,
                                                ScopedProfilerNode& prof,
                                                BaseGeneric2::Mode mode) {
  m_generic->render(dma, render_state, prof, mode);
}

void GenericVulkan2BucketRendererJak1::draw_debug_window() {
  m_generic->draw_debug_window();
}

void GenericVulkan2BucketRendererJak2::generic_render(DmaFollower& dma,
                                                      BaseSharedRenderState* render_state,
                                                      ScopedProfilerNode& prof,
                                                      BaseGeneric2::Mode mode) {
  m_generic->render(dma, render_state, prof, mode);
}

void GenericVulkan2BucketRendererJak2::draw_debug_window() {
  m_generic->draw_debug_window();
}
