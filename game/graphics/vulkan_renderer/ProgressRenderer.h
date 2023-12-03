#pragma once

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"

/*!
 * Renderer for the "Progress Bucket" of Jak 2.
 */
class ProgressVulkanRenderer : public DirectVulkanRenderer {
 public:
  static constexpr int kMinimapVramAddr = 4032;
  static constexpr int kMinimapWidth = 128;
  static constexpr int kMinimapHeight = 128;
  static constexpr int kScreenFbp = 408;
  static constexpr int kMinimapFbp = 126;
  ProgressVulkanRenderer(const std::string& name,
                         int my_id,
                         std::shared_ptr<GraphicsDeviceVulkan> device,
                         VulkanInitializationInfo& vulkan_info,
                         int batch_size);
  void handle_frame(u64 val,
                    BaseSharedRenderState* render_state,
                    ScopedProfilerNode& prof) override;
  void pre_render() override;
  void post_render() override;

 private:
  // GpuTexture* m_minimap_gpu_tex = nullptr;
  FramebufferVulkanHelper m_minimap_fb;
  // std::optional<FramebufferTexturePairContext> m_fb_ctxt;
  u32 m_current_fbp = kScreenFbp;
};
