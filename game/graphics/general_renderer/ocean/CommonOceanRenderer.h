#pragma once

#include "common/log/log.h"
#include "game/graphics/general_renderer/BucketRenderer.h"
#include <exception>

class BaseCommonOceanRenderer {
 public:
  BaseCommonOceanRenderer();
  ~BaseCommonOceanRenderer();

  void init_for_near();
  void kick_from_near(const u8* data);

  void init_for_mid();
  void kick_from_mid(const u8* data);

  // Move to public since neither Vulkan or DX12 have a getUniformLocation API like OpenGL does.
  // They'll need to know the memory offset of each member in the shader structure
  struct Vertex {
    math::Vector<float, 3> xyz;
    math::Vector<u8, 4> rgba;
    math::Vector<float, 3> stq;
    u8 fog;
    u8 pad[3];
  };
  static_assert(sizeof(Vertex) == 32);

 protected:
  void SetShaders(BaseSharedRenderState* render_state);
  void reverse_indices(u32* indices, u32 count);

 private:
  void handle_near_vertex_gif_data_fan(const u8* data, u32 offset, u32 loop);
  void handle_near_vertex_gif_data_strip(const u8* data, u32 offset, u32 loop);

  void handle_near_adgif(const u8* data, u32 offset, u32 count);

  void handle_mid_adgif(const u8* data, u32 offset);

  enum VertexBucket {
    RGB_TEXTURE = 0,
    ALPHA = 1,
    ENV_MAP = 2,
  };
  u32 m_current_bucket = VertexBucket::RGB_TEXTURE;

  static constexpr int NUM_BUCKETS = 3;

  std::vector<Vertex> m_vertices;
  u32 m_next_free_vertex = 0;

  std::vector<u32> m_indices[NUM_BUCKETS];
  u32 m_next_free_index[NUM_BUCKETS] = {0};

  u32 m_envmap_tex = 0;
};

namespace common_ocean_renderer_error {
void report_error(const char* description) {
  lg::error(description);
  throw std::exception(description);
}
}

class CommonOceanRendererInterface {
 public:
  virtual void common_ocean_renderer_init_for_near() {
    common_ocean_renderer_error::report_error("Call to incomplete common_ocean_renderer_init_for_near made\n");
  };
  virtual void common_ocean_renderer_kick_from_near(const u8* data) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_kick_from_near made\n");
  }

  virtual void common_ocean_renderer_init_for_mid() {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_mid made\n");
  }
  virtual void common_ocean_renderer_kick_from_mid(const u8* data) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_kick_from_mid made\n");
  }

  virtual void common_ocean_renderer_flush_near(BaseSharedRenderState* render_state,
                                                ScopedProfilerNode& prof) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void common_ocean_renderer_flush_mid(BaseSharedRenderState* render_state,
                                               ScopedProfilerNode& prof) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
};

class CommonOceanTextureRendererInterface {
 public:
  virtual void texture_renderer_draw_debug_window() {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void direct_renderer_draw_debug_window() {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }

  virtual void texture_renderer_initialize_textures(BaseTexturePool&) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void ocean_mid_renderer_run(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                     BaseSharedRenderState* render_state,
                                                     ScopedProfilerNode& prof) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }

  virtual void direct_renderer_reset_state() {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void direct_render_gif(const u8* data,
                                 u32 size,
                                 BaseSharedRenderState* render_state,
                                 ScopedProfilerNode& prof) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
  virtual void direct_renderer_set_mipmap(bool) {
    common_ocean_renderer_error::report_error(
        "Call to incomplete common_ocean_renderer_init_for_near made\n");
  }
};
