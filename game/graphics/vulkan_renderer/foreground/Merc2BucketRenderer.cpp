#include "Merc2BucketRenderer.h"

MercVulkan2BucketRenderer::MercVulkan2BucketRenderer(const std::string& name,
                                                     int my_id,
                                                     std::shared_ptr<GraphicsDeviceVulkan> device,
                                                     VulkanInitializationInfo& vulkan_info,
                                                     std::shared_ptr<MercVulkan2> merc2)
    : BaseMerc2BucketRenderer(name, my_id),
      BucketVulkanRenderer(device, vulkan_info),
      m_merc2(merc2) {}

void MercVulkan2BucketRenderer::render(DmaFollower& dma,
                                       SharedVulkanRenderState* render_state,
                                       ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  // skip if disabled
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  m_merc2->set_command_buffer(command_buffer);
  m_merc2->render(dma, render_state, prof, &m_debug_stats);
}

void MercVulkan2BucketRenderer::draw_debug_window() {
  m_merc2->draw_debug_window(&m_debug_stats);
}

MercVulkan2BucketRendererJak1::MercVulkan2BucketRendererJak1(
    const std::string& name,
    int my_id,
    std::shared_ptr<GraphicsDeviceVulkan> device,
    VulkanInitializationInfo& vulkan_info,
    std::shared_ptr<MercVulkan2Jak1> merc2)
    : BaseMerc2BucketRenderer(name, my_id),
      BucketVulkanRenderer(device, vulkan_info),
      m_merc2(merc2) {}

void MercVulkan2BucketRendererJak1::render(DmaFollower& dma,
                                           SharedVulkanRenderState* render_state,
                                           ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  // skip if disabled
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  m_merc2->set_command_buffer(command_buffer);
  m_merc2->render(dma, render_state, prof, &m_debug_stats);
}

void MercVulkan2BucketRendererJak1::draw_debug_window() {
  m_merc2->draw_debug_window(&m_debug_stats);
}
