#pragma once

#include <memory>
#include <string>

#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/Profiler.h"
#include "game/graphics/opengl_renderer/Shader.h"
#include "game/graphics/general_renderer/buckets.h"
#include "game/graphics/opengl_renderer/loader/Loader.h"
#include "game/graphics/texture/TexturePool.h"

class EyeRenderer;
/*!
 * The main renderer will contain a single SharedRenderState that's passed to all bucket renderers.
 * This allows bucket renders to share textures and shaders.
 */
struct SharedRenderState : BaseSharedRenderState {
  explicit SharedRenderState(std::shared_ptr<TexturePool> _texture_pool,
                             std::shared_ptr<Loader> _loader,
                             GameVersion version)
      : BaseSharedRenderState( version), shaders(version), texture_pool(_texture_pool), loader(_loader) {}
  ShaderLibrary shaders;
  std::shared_ptr<TexturePool> texture_pool;
  std::shared_ptr<Loader> loader;

  void reset() override;

  EyeRenderer* eye_renderer = nullptr;

  GLuint render_fb = -1;
};

/*!
 * Interface for bucket renders. Each bucket will have its own BucketRenderer.
 */
class BucketRenderer {
 public:
  BucketRenderer(const std::string& name, int my_id) : m_name(name), m_my_id(my_id) {}
  virtual void render(DmaFollower& dma,
                      SharedRenderState* render_state,
                      ScopedProfilerNode& prof) = 0;
  std::string name_and_id() const;
  virtual ~BucketRenderer() = default;
  bool& enabled() { return m_enabled; }
  virtual bool empty() const { return false; }
  virtual void draw_debug_window() = 0;
  virtual void init_shaders(ShaderLibrary&) {}
  virtual void init_textures(TexturePool&, GameVersion) {}

 protected:
  std::string m_name;
  int m_my_id;
  bool m_enabled = true;
};

class RenderMux : public BucketRenderer {
 public:
  RenderMux(const std::string& name,
            int my_id,
            std::vector<std::unique_ptr<BucketRenderer>> renderers);
  void render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;
  void init_shaders(ShaderLibrary&) override;
  void init_textures(TexturePool&, GameVersion) override;
  void set_idx(u32 i) { m_render_idx = i; };

 private:
  std::vector<std::unique_ptr<BucketRenderer>> m_renderers;
  int m_render_idx = 0;
  std::vector<std::string> m_name_strs;
  std::vector<const char*> m_name_str_ptrs;
};

/*!
 * Renderer that makes sure the bucket is empty and ignores it.
 */
class EmptyBucketRenderer : public BucketRenderer {
 public:
  EmptyBucketRenderer(const std::string& name, int my_id);
  void render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) override;
  bool empty() const override { return true; }
  void draw_debug_window() override {}
};

class SkipRenderer : public BucketRenderer {
 public:
  SkipRenderer(const std::string& name, int my_id);
  void render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) override;
  bool empty() const override { return true; }
  void draw_debug_window() override {}
};
