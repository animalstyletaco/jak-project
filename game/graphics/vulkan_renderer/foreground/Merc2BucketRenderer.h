#pragma once

#include "game/graphics/general_renderer/foreground/Merc2BucketRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/foreground/Merc2.h"

class MercVulkan2BucketRenderer : public BaseMerc2BucketRenderer, public BucketVulkanRenderer {
 public:
  MercVulkan2BucketRenderer(const std::string& name,
                            int my_id,
                            std::shared_ptr<GraphicsDeviceVulkan> device,
                            VulkanInitializationInfo& vulkan_info,
                            std::shared_ptr<MercVulkan2>);
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof, VkCommandBuffer command_buffer) override;
  void draw_debug_window() override;

 protected:
  std::shared_ptr<MercVulkan2> m_merc2;
};

class MercVulkan2BucketRendererJak1 : public BaseMerc2BucketRenderer, public BucketVulkanRenderer {
 public:
  MercVulkan2BucketRendererJak1(const std::string& name,
                                int my_id,
                                std::shared_ptr<GraphicsDeviceVulkan> device,
                                VulkanInitializationInfo& vulkan_info,
                                std::shared_ptr<MercVulkan2Jak1>);
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof, VkCommandBuffer command_buffer) override;
  void draw_debug_window() override;

 protected:
  std::shared_ptr<MercVulkan2Jak1> m_merc2;
};
