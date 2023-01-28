#pragma once

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanMidAndFar.h"
#include "game/graphics/vulkan_renderer/ocean/OceanTexture.h"
#include "game/graphics/vulkan_renderer/ocean/OceanMid.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

/*!
 * OceanMidAndFar is the handler for the first ocean bucket.
 * This bucket runs three renderers:
 * - ocean-texture (handled by the OceanTexture C++ class)
 * - ocean-far (handled by this class, it's very simple)
 * - ocean-mid (handled by the C++ OceanMid class)
 */

class OceanVulkanMidAndFar : public BaseOceanMidAndFar, public BucketVulkanRenderer {
 public:
  OceanVulkanMidAndFar(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_textures(VulkanTexturePool& pool) override;
  void draw_debug_window() override;

 private:
  void ocean_mid_renderer_run(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) override;
  void direct_renderer_render_gif(const u8* data,
                                  u32 size,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) override;
  void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& prof) override;
  void direct_renderer_set_mipmap(bool isMipmapEnabled) override;
  void direct_renderer_reset_state() override;
  void texture_renderer_handle_ocean_texture_jak1(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;
  void texture_renderer_handle_ocean_texture_jak2(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;

  DirectVulkanRenderer m_direct;
  OceanVulkanTexture m_texture_renderer;
  OceanMidVulkan m_mid_renderer;
};
