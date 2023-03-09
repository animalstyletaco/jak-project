#pragma once

#include "game/graphics/general_renderer/sprite/sprite_common.h"

class BaseGlowRenderer {
 public:
  BaseGlowRenderer();
  SpriteGlowOutput* alloc_sprite();
  void cancel_sprite();

  void flush(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void draw_debug_window();

  // Vertex can hold all possible values for all passes. The total number of vertices is very small
  // so it ends up a lot faster to do a single upload, even if the size is like 50% larger than it
  // could be.
  struct Vertex {
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
    float uu, vv;
  };

 protected:
  struct {
    bool show_probes = false;
    bool show_probe_copies = false;
    int num_sprites = 0;
    float glow_boost = 1.f;
  } m_debug;
  void add_sprite_pass_1(const SpriteGlowOutput& data);
  void add_sprite_pass_2(const SpriteGlowOutput& data, int sprite_idx);
  void add_sprite_pass_3(const SpriteGlowOutput& data, int sprite_idx);

  virtual void blit_depth(BaseSharedRenderState* render_state) = 0;

  virtual void draw_probes(BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof,
                   u32 idx_start,
                   u32 idx_end) = 0;

  virtual void debug_draw_probes(BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof,
                         u32 idx_start,
                         u32 idx_end) = 0;

  virtual void draw_probe_copies(BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof,
                         u32 idx_start,
                         u32 idx_end) = 0;

  virtual void debug_draw_probe_copies(BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof,
                               u32 idx_start,
                               u32 idx_end) = 0;
  virtual void downsample_chain(BaseSharedRenderState* render_state, ScopedProfilerNode& prof, u32 num_sprites) = 0;

  virtual void draw_sprites(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;

  std::vector<Vertex> m_vertex_buffer;
  std::vector<SpriteGlowOutput> m_sprite_data_buffer;
  u32 m_next_sprite = 0;

  std::vector<Vertex> m_downsample_vertices;
  std::vector<u32> m_downsample_indices;

  u32 m_next_vertex = 0;
  Vertex* alloc_vtx(int num);

  std::vector<u32> m_index_buffer;
  u32 m_next_index = 0;
  u32* alloc_index(int num);

  static constexpr int kDownsampleBatchWidth = 20;
  static constexpr int kMaxSprites = kDownsampleBatchWidth * kDownsampleBatchWidth;
  static constexpr int kMaxVertices = kMaxSprites * 32;  // check.
  static constexpr int kMaxIndices = kMaxSprites * 32;   // check.
  static constexpr int kDownsampleIterations = 5;
  static constexpr int kFirstDownsampleSize = 32;  // should be power of 2.

  DrawMode m_default_draw_mode;

  struct SpriteRecord {
    u32 tbp;
    DrawMode draw_mode;
    u32 idx;
  };

  std::array<SpriteRecord, kMaxSprites> m_sprite_records;
};
