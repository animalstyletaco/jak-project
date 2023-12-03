#include "game/graphics/vulkan_renderer/BucketRenderer.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

EmptyBucketVulkanRenderer::EmptyBucketVulkanRenderer(const std::string& name,
                                                     int m_my_id,
                                                     std::shared_ptr<GraphicsDeviceVulkan> device,
                                                     VulkanInitializationInfo& vulkan_info)
    : BaseEmptyBucketRenderer(name, m_my_id), BucketVulkanRenderer(device, vulkan_info) {}

void EmptyBucketVulkanRenderer::render(DmaFollower& dma,
                                       SharedVulkanRenderState* render_state,
                                       ScopedProfilerNode& prof) {
  BaseEmptyBucketRenderer::render(dma, render_state, prof);
}

SkipVulkanRenderer::SkipVulkanRenderer(const std::string& name,
                                       int m_my_id,
                                       std::shared_ptr<GraphicsDeviceVulkan> device,
                                       VulkanInitializationInfo& vulkan_info)
    : BaseSkipRenderer(name, m_my_id), BucketVulkanRenderer(device, vulkan_info) {}

void SkipVulkanRenderer::render(DmaFollower& dma,
                                SharedVulkanRenderState* render_state,
                                ScopedProfilerNode& prof) {
  BaseSkipRenderer::render(dma, render_state, prof);
}

RenderVulkanMux::RenderVulkanMux(const std::string& name,
                                 int m_my_id,
                                 std::shared_ptr<GraphicsDeviceVulkan> device,
                                 VulkanInitializationInfo& vulkan_info,
                                 std::vector<std::shared_ptr<BucketVulkanRenderer>> renderers,
                                 std::vector<std::shared_ptr<BaseBucketRenderer>> bucket_renderers)
    : BucketVulkanRenderer(device, vulkan_info),
      BaseRenderMux(name, m_my_id),
      m_graphics_renderers(renderers),
      m_bucket_renderers(bucket_renderers) {}

void RenderVulkanMux::render(DmaFollower& dma,
                             SharedVulkanRenderState* render_state,
                             ScopedProfilerNode& prof) {
  m_bucket_renderers[m_render_idx]->enabled() = m_enabled;
  m_graphics_renderers[m_render_idx]->render(dma, render_state, prof);
}

void RenderVulkanMux::init_textures(VulkanTexturePool& tp) {
  for (auto& rend : m_graphics_renderers) {
    rend->init_textures(tp);
  }
}
