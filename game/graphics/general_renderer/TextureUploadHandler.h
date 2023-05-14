#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/texture/TexturePoolDataTypes.h"

/*!
 * The TextureUploadHandler receives textures uploads in the DMA chain and updates the TexturePool.
 * The actual textures are preconverted and provided by the loader, so this just updates tables that
 * tell the renderers which OpenGL texture goes with PS2 VRAM addresses.
 */
class BaseTextureUploadHandler : public BaseBucketRenderer {
 public:
  BaseTextureUploadHandler(const std::string& name, int my_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  virtual void eye_renderer_handle_eye_dma2(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;
  virtual void texture_pool_handle_upload_now(const u8* tpage, int mode, const u8* memory_base, u32 s7_ptr) = 0;

  struct TextureUpload {
    u64 page;
    s64 mode;
  };
  void flush_uploads(std::vector<TextureUpload>& uploads, BaseSharedRenderState* render_state);
  bool m_fake_uploads = false;
  int m_upload_count = 0;
};
