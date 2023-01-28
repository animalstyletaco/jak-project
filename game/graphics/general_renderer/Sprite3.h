
#pragma once

#include <map>

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/DirectRenderer.h"
#include "game/graphics/general_renderer/background/background_common.h"
#include "game/graphics/general_renderer/sprite_common.h"

class BaseSprite3 : public BaseBucketRenderer {
 public:
  BaseSprite3(const std::string& name, int my_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  virtual void setup_graphics_for_2d_group_0_render() = 0;
  virtual void direct_renderer_reset_state() = 0;
  virtual void direct_renderer_render_vif(u32 vif0,
                                          u32 vif1,
                                          const u8* data,
                                          u32 size,
                                          BaseSharedRenderState* render_state,
                                          ScopedProfilerNode& prof) = 0;
  virtual void direct_renderer_flush_pending(BaseSharedRenderState * render_state,
                                              ScopedProfilerNode& prof) = 0;
  virtual void SetSprite3UniformVertexFourFloatVector(const char* name,
                                         u32 numberOfFloats,
                                         float* data, u32 flags = 0) = 0;
  virtual void SetSprite3UniformMatrixFourFloatVector(const char* name,
                                         u32 numberOfFloats,
                                         bool isTransponsedMatrix,
                                         float* data, u32 flags = 0) = 0;

  void render_jak1(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void render_jak2(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

  virtual void graphics_setup() = 0;
  virtual void graphics_setup_normal() = 0;
  virtual void graphics_setup_distort() = 0;

  virtual void distort_draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) = 0;
  virtual void distort_draw_instanced(BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) = 0;
  virtual void distort_draw_common(BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) = 0;
  virtual void distort_setup_framebuffer_dims(BaseSharedRenderState* render_state) = 0;
  virtual void flush_sprites(BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof,
                             bool double_draw) = 0;
  virtual void EnableSprite3GraphicsBlending() = 0;  // TODO: May need to have game version passed
                                                     // in as parameter

  void render_2d_group0(DmaFollower& dma,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof);
  void render_distorter(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  void distort_dma(DmaFollower& dma, ScopedProfilerNode& prof);
  void distort_setup(ScopedProfilerNode& prof);
  void distort_setup_instanced(ScopedProfilerNode& prof);

  void handle_sprite_frame_setup(DmaFollower& dma, GameVersion version);
  void render_3d(DmaFollower& dma);

  void render_fake_shadow(DmaFollower& dma);
  void render_2d_group1(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  enum SpriteMode { Mode2D = 1, ModeHUD = 2, Mode3D = 3 };
  void do_block_common(SpriteMode mode,
                       u32 count,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof);

  void update_mode_from_alpha1(u64 val, DrawMode& mode);
  void handle_tex0(u64 val, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void handle_tex1(u64 val, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  // void handle_mip(u64 val, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void handle_zbuf(u64 val, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void handle_clamp(u64 val, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void handle_alpha(u64 val, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);


  struct SpriteDistorterSetup {
    GifTag gif_tag;
    GsZbuf zbuf;
    u64 zbuf_addr;
    GsTex0 tex0;
    u64 tex0_addr;
    GsTex1 tex1;
    u64 tex1_addr;
    u64 miptbp;
    u64 miptbp_addr;
    u64 clamp;
    u64 clamp_addr;
    GsAlpha alpha;
    u64 alpha_addr;
  };
  static_assert(sizeof(SpriteDistorterSetup) == (7 * 16));

  struct SpriteDistorterSineTables {
    Vector4f entry[128];
    math::Vector<u32, 4> ientry[9];
    GifTag gs_gif_tag;
    math::Vector<u32, 4> color;
  };
  static_assert(sizeof(SpriteDistorterSineTables) == (0x8b * 16));

  struct SpriteDistortFrameData {
    math::Vector3f xyz;  // position
    float num_255;       // always 255.0
    math::Vector2f st;   // texture coords
    float num_1;         // always 1.0
    u32 flag;            // 'resolution' of the sprite
    Vector4f rgba;       // ? (doesn't seem to be color)
  };
  static_assert(sizeof(SpriteDistortFrameData) == 16 * 3);

  struct SpriteDistortVertex {
    math::Vector3f xyz;
    math::Vector2f st;
  };

  struct SpriteDistortInstanceData {
    math::Vector4f x_y_z_s;     // position, S-texture coord
    math::Vector4f sx_sy_sz_t;  // scale, T-texture coord
  };

  struct {
    int total_sprites;
    int total_tris;
  } m_distort_stats;

  struct GraphicsDistortOgl {
    int fbo_width = 640;
    int fbo_height = 480;
  };

  struct GraphicsDistortInstancedOgl {
    float last_aspect_x = -1.0;
    float last_aspect_y = -1.0;
    bool vertex_data_changed = false;
  } m_distort_instanced_ogl;

  std::vector<SpriteDistortVertex> m_sprite_distorter_vertices;
  std::vector<u32> m_sprite_distorter_indices;
  SpriteDistorterSetup m_sprite_distorter_setup;  // direct data
  math::Vector4f m_sprite_distorter_sine_tables_aspect;
  SpriteDistorterSineTables m_sprite_distorter_sine_tables;
  std::vector<SpriteDistortFrameData> m_sprite_distorter_frame_data;
  std::vector<SpriteDistortVertex> m_sprite_distorter_vertices_instanced;
  std::map<int, std::vector<SpriteDistortInstanceData>> m_sprite_distorter_instances_by_res;

  u8 m_sprite_direct_setup[3 * 16];
  SpriteFrameData m_frame_data;  // qwa: 980
  Sprite3DMatrixData m_3d_matrix_data;
  SpriteHudMatrixData m_hud_matrix_data;

  SpriteVecData2d m_vec_data_2d[sprite_common::SPRITES_PER_CHUNK];
  AdGifData m_adgif[sprite_common::SPRITES_PER_CHUNK];

  struct DebugStats {
    int blocks_2d_grp0 = 0;
    int count_2d_grp0 = 0;
    int blocks_2d_grp1 = 0;
    int count_2d_grp1 = 0;
  } m_debug_stats;

  bool m_enable_distort_instancing = true;
  bool m_enable_culling = true;

  bool m_2d_enable = true;
  bool m_3d_enable = true;
  bool m_distort_enable = true;

  struct SpriteVertex3D {
    math::Vector4f xyz_sx;              // position + x scale
    math::Vector4f quat_sy;             // quaternion + y scale
    math::Vector4f rgba;                // color
    math::Vector<u16, 2> flags_matrix;  // flags + matrix... split
    math::Vector<u16, 4> info;
    math::Vector<u8, 4> pad;
  };
  static_assert(sizeof(SpriteVertex3D) == 64);

  std::vector<SpriteVertex3D> m_vertices_3d;

  DrawMode m_current_mode, m_default_mode;
  u32 m_current_tbp = 0;

  struct Bucket {
    std::vector<u32> ids;
    u32 offset_in_idx_buffer = 0;
    u64 key = -1;
  };

  std::map<u64, Bucket> m_sprite_buckets;
  std::vector<Bucket*> m_bucket_list;

  u64 m_last_bucket_key = UINT64_MAX;
  Bucket* m_last_bucket = nullptr;

  u64 m_sprite_idx = 0;

  std::vector<u32> m_index_buffer_data;
  static constexpr int SPRITE_RENDERER_MAX_SPRITES = 1920 * 10;
  static constexpr int SPRITE_RENDERER_MAX_DISTORT_SPRITES =
      256 * 10;  // size of sprite-aux-list in GOAL code * SPRITE_MAX_AMOUNT_MULT
};
