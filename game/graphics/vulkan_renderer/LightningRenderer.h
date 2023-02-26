#pragma once

#include "game/graphics/general_renderer/LightningRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"

class LightningVulkanRenderer : public BaseLightningRenderer, public BucketVulkanRenderer {
 public:
  LightningVulkanRenderer(const std::string& name,
                          int id,
                          std::unique_ptr<GraphicsDeviceVulkan>& device,
                          VulkanInitializationInfo& vulkan_info);
  ~LightningVulkanRenderer();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

  protected:
  void generic_draw_debug_window() override;
  void generic_render_in_mode(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof,
                              BaseGeneric2::Mode) override;

 private:
  GenericVulkan2 m_generic;
};
