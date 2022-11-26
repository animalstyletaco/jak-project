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

class OceanMidAndFar : public BaseOceanMidAndFar, public BucketVulkanRenderer {
 public:
  OceanMidAndFar(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_textures(TexturePoolVulkan& pool) override;
  void draw_debug_window() override;

 private:
  void handle_ocean_far(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  void handle_ocean_mid(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  DirectVulkanRenderer m_direct;
  OceanVulkanTexture m_texture_renderer;
  OceanMidVulkan m_mid_renderer;

  std::unique_ptr<CommonOceanVertexUniformBuffer> m_ocean_uniform_vertex_buffer;
  std::unique_ptr<CommonOceanFragmentUniformBuffer> m_ocean_uniform_fragment_buffer;
};
