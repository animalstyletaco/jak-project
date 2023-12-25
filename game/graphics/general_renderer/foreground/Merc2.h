#pragma once
#include "game/graphics/general_renderer/BucketRenderer.h"

struct BaseMercDebugStats {
  int num_models = 0;
  int num_missing_models = 0;
  int num_chains = 0;
  int num_effects = 0;
  int num_predicted_draws = 0;
  int num_predicted_tris = 0;
  int num_bones_uploaded = 0;
  int num_lights = 0;
  int num_draw_flush = 0;

  int num_envmap_effects = 0;
  int num_envmap_tris = 0;

  int num_upload_bytes = 0;
  int num_uploads = 0;

  struct DrawDebug {
    DrawMode mode;
    int num_tris;
  };
  struct EffectDebug {
    bool envmap = false;
    DrawMode envmap_mode;
    std::vector<DrawDebug> draws;
  };
  struct ModelDebug {
    std::string name;
    std::string level;
    std::vector<EffectDebug> effects;
  };

  std::vector<ModelDebug> model_list;

  bool collect_debug_model_list = false;
};

class BaseMerc2 {
 public:
  BaseMerc2();
  void draw_debug_window(BaseMercDebugStats*);
  void render(DmaFollower& dma,
              BaseSharedRenderState* render_state,
              ScopedProfilerNode& prof,
              BaseMercDebugStats* stats);
  static constexpr int kMaxBlerc = 40;

 protected:
  virtual void flush_draw_buckets(BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof,
                                  BaseMercDebugStats*) = 0;
  virtual void handle_pc_model(const DmaTransfer& setup,
                               BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof,
                               BaseMercDebugStats*) = 0;
  virtual void set_merc_uniform_buffer_data(const DmaTransfer& dma) = 0;
  void handle_merc_chain(DmaFollower& dma,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof,
                         BaseMercDebugStats*);
  void handle_mod_vertices(const DmaTransfer& setup,
                           const tfrag3::MercEffect& effect,
                           const u8* input_data,
                           uint32_t index,
                           const tfrag3::MercModel* model);
  void blerc_avx(const u32* i_data,
                 const u32* i_data_end,
                 const tfrag3::BlercFloatData* floats,
                 const float* weights,
                 tfrag3::MercVertex* out,
                 float multiplier);

  std::mutex g_merc_data_mutex;
  bool m_debug_mode = false;

  enum MercDataMemory {
    LOW_MEMORY = 0,
    BUFFER_BASE = 442,
    // this negative offset is what broke jak graphics in Dobiestation for a long time.
    BUFFER_OFFSET = -442
  };

  struct LowMemory {
    u8 tri_strip_tag[16];
    u8 ad_gif_tag[16];
    math::Vector4f hvdf_offset;
    math::Vector4f perspective[4];
    math::Vector4f fog;
  } m_low_memory;
  static_assert(sizeof(LowMemory) == 0x80);

  struct VuLights {
    math::Vector3f direction0;
    u32 w0;
    math::Vector3f direction1;
    u32 w1;
    math::Vector3f direction2;
    u32 w2;
    math::Vector4f color0;
    math::Vector4f color1;
    math::Vector4f color2;
    math::Vector4f ambient;
  };

  void handle_all_dma(DmaFollower& dma,
                      BaseSharedRenderState* render_state,
                      ScopedProfilerNode& prof,
                      BaseMercDebugStats* debug_stats);
  void handle_setup_dma(DmaFollower& dma, BaseSharedRenderState* render_state);
  u32 alloc_lights(const VuLights& lights);
  void set_lights(const DmaTransfer& dma);
  void handle_matrix_dma(const DmaTransfer& dma);

  static constexpr int kMaxEffect = 32;
  bool m_effect_debug_mask[kMaxEffect];

  struct MercMat {
    math::Vector4f tmat[4];
    math::Vector4f nmat[3];
  };

  struct ShaderMercMat {
    math::Vector4f tmat[4];
    math::Vector4f nmat[3];
    math::Vector4f pad;

    std::string to_string() const;
  };

  static constexpr int MAX_SKEL_BONES = 128;
  static constexpr int BONE_VECTORS_PER_BONE = 7;
  static constexpr int MAX_SHADER_BONE_VECTORS = 1024 * 32;  // ??

  static constexpr int MAX_LEVELS = 3;
  static constexpr int MAX_DRAWS_PER_LEVEL = 1024;
  static constexpr int MAX_ENVMAP_DRAWS_PER_LEVEL = 1024;

  math::Vector4f m_shader_bone_vector_buffer[MAX_SHADER_BONE_VECTORS];
  ShaderMercMat m_skel_matrix_buffer[MAX_SKEL_BONES];

  u32 alloc_bones(int count, ShaderMercMat* data);

  struct Stats {
    int num_models = 0;
    int num_missing_models = 0;
    int num_chains = 0;
    int num_effects = 0;
    int num_predicted_draws = 0;
    int num_predicted_tris = 0;
    int num_bones_uploaded = 0;
    int num_lights = 0;
    int num_draw_flush = 0;
    int num_envmap_effects = 0;
    int num_envmap_tris = 0;
    int num_upload_bytes = 0;
    int num_uploads = 0;
  } m_stats;

  enum DrawFlags {
    IGNORE_ALPHA = 1,
    MOD_VTX = 2,
  };

  struct Draw {
    u32 first_index;
    u32 index_count;
    DrawMode mode;
    u32 texture;
    u32 num_triangles;
    u16 first_bone;
    u16 light_idx;
    u8 flags;
    u8 fade[4];
  };

  static constexpr int MAX_MOD_VTX = UINT16_MAX;
  std::vector<tfrag3::MercVertex> m_mod_vtx_temp;

  struct UnpackTempVtx {
    float pos[4];
    float nrm[4];
    float uv[2];
  };
  std::vector<UnpackTempVtx> m_mod_vtx_unpack_temp;

  static constexpr int MAX_LIGHTS = 1024;
  VuLights m_lights_buffer[MAX_LIGHTS];
  u32 m_next_free_light = 0;
  VuLights m_current_lights;

  u32 m_next_free_level_bucket = 0;
  u32 m_next_free_bone_vector = 0;
  size_t m_graphics_buffer_alignment = 0;
  u32 m_next_mod_vtx_buffer = 0;

  struct PcMercFlags {
    u64 enable_mask;
    u64 ignore_alpha_mask;
    u8 effect_count;
    u8 bitflags;
  };

  struct ModSettings {
    u64 first_bone = 0;
    u64 lights = 0;
    u64 uses_jak1_water = 0;
    bool model_uses_mod = false;
    bool model_disables_fog = false;
    bool model_uses_pc_blerc = false;
    bool model_disables_envmap = false;
    bool ignore_alpha = false;
  };

  void validate_merc_vertices(const tfrag3::MercEffect& effect);
  void populate_normal_draw(const tfrag3::MercDraw& mdraw,
                            const ModSettings& settings,
                            BaseMerc2::Draw* draw);

  void populate_envmap_draw(const tfrag3::MercDraw& mdraw,
                            DrawMode envmap_mode,
                            u32 envmap_texture,
                            const ModSettings& settings,
                            const u8* fade,
                            BaseMerc2::Draw* draw);

  void setup_mod_vertex_dma(const tfrag3::MercEffect& effect,
                            const u8* input_data,
                            const u32 index,
                            const tfrag3::MercModel* model,
                            const DmaTransfer& setup);
};
