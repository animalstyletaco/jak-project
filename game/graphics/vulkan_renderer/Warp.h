#pragma once

#include "game/graphics/general_renderer/Warp.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/Warp.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"

class WarpVulkan : public BaseWarp, public BucketVulkanRenderer {
 public:
  WarpVulkan(const std::string& name, int id, std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo&);
  ~WarpVulkan();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void generic_draw_debug_window() override;

 private:
  GenericVulkan2 m_generic;
  FramebufferVulkanCopier m_fb_copier;
  //GpuTextureVulkan* m_warp_src_tex = nullptr;
  u32 m_tbp = 1216;  // hack, jak 2
};
