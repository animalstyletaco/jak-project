
#pragma once

#include "game/graphics/general_renderer/SkyRenderer.h"
#include "game/graphics/vulkan_renderer/SkyBlendCPU.h"
#include "game/graphics/vulkan_renderer/SkyBlendGPU.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/background/TFragment.h"

/*!
 * Handles texture blending for the sky.
 * Will insert the result texture into the texture pool.
 */

class SkyBlendVulkanHandler : public BaseSkyBlendHandler, public BucketVulkanRenderer {
 public:
  SkyBlendVulkanHandler(const std::string& name,
                  int my_id,
                  std::unique_ptr<GraphicsDeviceVulkan>& device,
                  VulkanInitializationInfo& vulkan_info,
                  int level_id,
                  std::shared_ptr<SkyBlendVulkanGPU> shared_gpu_blender,
                  std::shared_ptr<SkyBlendCPU> shared_cpu_blender);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

 protected:
  void tfrag_renderer_draw_debug_window() override;

 private:
  void handle_sky_copies(DmaFollower& dma,
                         SharedVulkanRenderState* render_state,
                         ScopedProfilerNode& prof);

  SkyBlendStats cpu_blender_do_sky_blends(DmaFollower& dma,
                                          BaseSharedRenderState* render_state,
                                          ScopedProfilerNode& prof) override;

  SkyBlendStats gpu_blender_do_sky_blends(DmaFollower& dma,
                                          BaseSharedRenderState* render_state,
                                          ScopedProfilerNode& prof) override;

  void tfrag_renderer_render(DmaFollower& dma,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& tfrag_prof) override;

  std::shared_ptr<SkyBlendVulkanGPU> m_shared_gpu_blender;
  std::shared_ptr<SkyBlendCPU> m_shared_cpu_blender;
  SkyBlendStats m_gpu_stats;
  TFragmentVulkan m_tfrag_renderer;
};

/*!
 * Handles sky drawing.
 */
class SkyVulkanRenderer : public BaseSkyRenderer, public BucketVulkanRenderer {
 public:
  SkyVulkanRenderer(const std::string& name,
              int my_id,
              std::unique_ptr<GraphicsDeviceVulkan>& device,
              VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

  protected:
   void direct_renderer_reset_state() override;
   void direct_renderer_draw_debug_window() override;
   void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) override;
   void direct_renderer_render_gif(
             const u8* data,
             u32 size,
             BaseSharedRenderState* render_state,
             ScopedProfilerNode& prof) override;

   void direct_renderer_render_vif(u32 vif0,
                                   u32 vif1,
                                   const u8* data,
                                   u32 size,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) override;

 private:
  DirectVulkanRenderer m_direct_renderer;
  uint32_t m_direct_renderer_call_count = 0;

  struct FrameStats {
    int gif_packets = 0;
  } m_frame_stats;
};
