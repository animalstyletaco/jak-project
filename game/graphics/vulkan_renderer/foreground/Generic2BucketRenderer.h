#pragma once

#include "game/graphics/general_renderer/foreground/Generic2BucketRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"

class GenericVulkan2BucketRenderer : public BaseGeneric2BucketRenderer, public BucketVulkanRenderer {
 public:
  GenericVulkan2BucketRenderer(const std::string& name,
                               int id,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               VulkanInitializationInfo& vulkan_info,
                               BaseGeneric2::Mode mode,
                               std::shared_ptr<GenericVulkan2>);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void generic_render(DmaFollower& dma,
                      BaseSharedRenderState* render_state,
                      ScopedProfilerNode& prof,
                      BaseGeneric2::Mode mode) override;
  void draw_debug_window() override;

  protected:
  std::shared_ptr<GenericVulkan2> m_generic;
};
