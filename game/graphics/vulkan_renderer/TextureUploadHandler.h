#pragma once

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/TexturePoolVulkan.h"

/*!
 * The TextureUploadHandler receives textures uploads in the DMA chain and updates the TexturePool.
 * The actual textures are preconverted and provided by the loader, so this just updates tables that
 * tell the renderers which OpenGL texture goes with PS2 VRAM addresses.
 */
class TextureUploadHandler : public BucketRenderer {
 public:
  TextureUploadHandler(const std::string& name, BucketId my_id, VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 private:
  struct TextureUpload {
    u64 page;
    s64 mode;
  };
  void flush_uploads(std::vector<TextureUpload>& uploads, SharedRenderState* render_state);
  bool m_fake_uploads = false;
};
