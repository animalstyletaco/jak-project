#pragma once

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanMidAndFar.h"
#include "game/graphics/vulkan_renderer/ocean/OceanTexture.h"
#include "game/graphics/vulkan_renderer/ocean/OceanMid.h"

/*!
 * OceanMidAndFar is the handler for the first ocean bucket.
 * This bucket runs three renderers:
 * - ocean-texture (handled by the OceanTexture C++ class)
 * - ocean-far (handled by this class, it's very simple)
 * - ocean-mid (handled by the C++ OceanMid class)
 */

class OceanVulkanMidAndFar : public virtual BaseOceanMidAndFar, public BucketVulkanRenderer {
 public:
  OceanVulkanMidAndFar(const std::string& name,
                 int my_id,
                 std::shared_ptr<GraphicsDeviceVulkan> device,
                 VulkanInitializationInfo& vulkan_info);

 protected:
  void direct_renderer_render_gif(const u8* data,
                                  u32 size,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) override;
  void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& prof) override;
  void direct_renderer_set_mipmap(bool isMipmapEnabled) override;
  void direct_renderer_reset_state() override;

  DirectVulkanRenderer m_direct;

  u64 m_direct_renderer_call_count = 0;
};

class OceanVulkanMidAndFarJak1 : public BaseOceanMidAndFarJak1, public OceanVulkanMidAndFar {
 public:
  OceanVulkanMidAndFarJak1(const std::string& name,
                           int my_id,
                           std::shared_ptr<GraphicsDeviceVulkan> device,
                           VulkanInitializationInfo& vulkan_info)
      : BaseOceanMidAndFar(name, my_id),
        BaseOceanMidAndFarJak1(name, my_id),
        OceanVulkanMidAndFar(name, my_id, device, vulkan_info),
        m_texture_renderer(true, device, vulkan_info),
        m_mid_renderer(device, vulkan_info){};
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;
  void init_textures(VulkanTexturePool& pool) override;
  void draw_debug_window() override;

 private:
  void ocean_mid_renderer_run(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) override;

  OceanVulkanTextureJak1 m_texture_renderer;
  OceanMidVulkanJak1 m_mid_renderer;
};

class OceanVulkanMidAndFarJak2 : public BaseOceanMidAndFarJak2, public OceanVulkanMidAndFar {
 public:
  OceanVulkanMidAndFarJak2(const std::string& name,
                           int my_id,
                           std::shared_ptr<GraphicsDeviceVulkan> device,
                           VulkanInitializationInfo& vulkan_info)
      : BaseOceanMidAndFar(name, my_id),
        BaseOceanMidAndFarJak2(name, my_id),
        OceanVulkanMidAndFar(name, my_id, device, vulkan_info),
        m_texture_renderer(true, device, vulkan_info),
        m_mid_renderer(device, vulkan_info){};
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                             BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) override;
  void init_textures(VulkanTexturePool& pool) override;
  void draw_debug_window() override;

  private:
  void ocean_mid_renderer_run(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) override;

  OceanVulkanTextureJak2 m_texture_renderer;
  OceanMidVulkanJak2 m_mid_renderer;
};
