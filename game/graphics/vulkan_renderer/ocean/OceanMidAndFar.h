#pragma once

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/OceanMid.h"
#include "game/graphics/vulkan_renderer/ocean/OceanTexture.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

/*!
 * OceanMidAndFar is the handler for the first ocean bucket.
 * This bucket runs three renderers:
 * - ocean-texture (handled by the OceanTexture C++ class)
 * - ocean-far (handled by this class, it's very simple)
 * - ocean-mid (handled by the C++ OceanMid class)
 */
class OceanMidAndFar : public BucketRenderer {
 public:
  OceanMidAndFar(const std::string& name, BucketId my_id, VulkanInitializationInfo& device);
  void render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;
  void init_textures(TexturePool& pool) override;

 private:
  void handle_ocean_far(DmaFollower& dma,
                        SharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  void handle_ocean_mid(DmaFollower& dma,
                        SharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  DirectRenderer m_direct;
  OceanTexture m_texture_renderer;
  OceanMid m_mid_renderer;

  std::unique_ptr<CommonOceanVertexUniformBuffer> m_uniform_vertex_buffer;
  std::unique_ptr<CommonOceanFragmentUniformBuffer> m_uniform_fragment_buffer;
};
