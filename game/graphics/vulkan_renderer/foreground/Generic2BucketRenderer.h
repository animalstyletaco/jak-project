#pragma once

#include "game/graphics/general_renderer/foreground/Generic2BucketRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"

class GenericVulkan2BucketRenderer : public BaseGeneric2BucketRenderer, public BucketVulkanRenderer {
 public:
  GenericVulkan2BucketRenderer(const std::string& name,
                               int id,
                               std::shared_ptr<GraphicsDeviceVulkan> device,
                               VulkanInitializationInfo& vulkan_info,
                               BaseGeneric2::Mode mode,
                               std::shared_ptr<GenericVulkan2>);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
};

class GenericVulkan2BucketRendererJak1 : public GenericVulkan2BucketRenderer {
 public:
  GenericVulkan2BucketRendererJak1(const std::string& name,
                               int id,
                               std::shared_ptr<GraphicsDeviceVulkan> device,
                               VulkanInitializationInfo& vulkan_info,
                               BaseGeneric2::Mode mode,
                               std::shared_ptr<GenericVulkan2> generic_renderer)
      : GenericVulkan2BucketRenderer(name, id, device, vulkan_info, mode, generic_renderer){};
  void generic_render(DmaFollower& dma,
                      BaseSharedRenderState* render_state,
                      ScopedProfilerNode& prof,
                      BaseGeneric2::Mode mode) override;
  void draw_debug_window() override;

 protected:
  std::shared_ptr<GenericVulkan2Jak1> m_generic;
};

class GenericVulkan2BucketRendererJak2 : public GenericVulkan2BucketRenderer {
 public:
  GenericVulkan2BucketRendererJak2(const std::string& name,
                                   int id,
                                   std::shared_ptr<GraphicsDeviceVulkan> device,
                                   VulkanInitializationInfo& vulkan_info,
                                   BaseGeneric2::Mode mode,
                                   std::shared_ptr<GenericVulkan2> generic_renderer)
      : GenericVulkan2BucketRenderer(name, id, device, vulkan_info, mode, generic_renderer){};
  void generic_render(DmaFollower& dma,
                      BaseSharedRenderState* render_state,
                      ScopedProfilerNode& prof,
                      BaseGeneric2::Mode mode) override;
  void draw_debug_window() override;

 protected:
  std::shared_ptr<GenericVulkan2Jak2> m_generic;
};
