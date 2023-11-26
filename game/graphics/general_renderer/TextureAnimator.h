#pragma once

#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "common/custom_data/Tfrag3Data.h"
#include "common/dma/dma_chain_read.h"
#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/ShaderCommon.h"
#include "game/graphics/texture/TextureConverter.h"
#include "game/graphics/texture/TextureID.h"

struct GpuTexture;

struct alignas(float) BaseTextureAnimationVertexUniformBufferData {
  std::array<std::array<float, 2>, 4> uvs;
  std::array<std::array<float, 3>, 4> positions;
};

struct alignas(float) BaseTextureAnimationFragmentPushConstant {
  std::array<u32, 4> rgba;
  int enable_tex;
  int tcc;
  std::array<s32, 4> channel_scramble;
  float alpha_multiply;
  float minimum;
  float maximum;
  float slime_scroll;
};

// IDs sent from GOAL telling us what texture operation to perform.
enum PcTextureAnimCodes {
  FINISH_ARRAY = 13,
  ERASE_DEST_TEXTURE = 14,
  UPLOAD_CLUT_16_16 = 15,
  GENERIC_UPLOAD = 16,
  SET_SHADER = 17,
  DRAW = 18,
  DARKJAK = 22,
  PRISON_JAK = 23,
  ORACLE_JAK = 24,
  NEST_JAK = 25,
  KOR_TRANSFORM = 26,
  SKULL_GEM = 27,
  BOMB = 28,
  CAS_CONVEYOR = 29,
  SECURITY = 30,
  WATERFALL = 31,
  WATERFALL_B = 32,
  LAVA = 33,
  LAVA_B = 34,
  STADIUMB = 35,
  FORTRESS_PRIS = 36,
  FORTRESS_WARP = 37,
  METKOR = 38,
  SHIELD = 39,
  KREW_HOLO = 40,
  CLOUDS_AND_FOG = 41,
  SLIME = 42,
  CLOUDS_HIRES = 43,
};

// metadata for an upload from GOAL memory
struct TextureAnimPcUpload {
  u32 data;  // goal pointer
  u16 width;
  u16 height;
  u32 dest;  // tbp address
  // PS2 texture format of the _upload_ that was used.
  // note that the data can be any format. They upload stuff in the wrong format sometimes, as
  // an optimization (ps2 is fastest at psmct32)
  u8 format;
  u8 force_to_gpu;
  u8 pad[2];
};
static_assert(sizeof(TextureAnimPcUpload) == 16);

// metadata for an operation that operates on a source/destination texture.
struct TextureAnimPcTransform {
  u32 src_tbp;
  u32 dst_tbp;
  u32 pad0;
  u32 pad1;
};

struct BaseVramEntry {
  enum class Kind {
    CLUT16_16_IN_PSM32,
    GENERIC_PSM32,
    GENERIC_PSMT8,
    GENERIC_PSMT4,
    GPU,
    INVALID
  } kind;
  std::vector<u8> data;

  int tex_width = 0;
  int tex_height = 0;
  int dest_texture_address = 0;
  int cbp = 0;
  // math::Vector<u8, 4> rgba_clear;

  bool needs_pool_update = false;

  virtual void reset() {
    data.clear();
    kind = Kind::INVALID;
    tex_height = 0;
    tex_width = 0;
    cbp = 0;
    // tex.reset();
    needs_pool_update = false;
    // pool_gpu_tex = nullptr;
  }
};

struct ShaderContext {
  GsTex0 tex0;
  GsTex1 tex1;
  GsTest test;
  bool clamp_u, clamp_v;
  GsAlpha alpha;
  bool source_texture_set = false;
};


struct Psm32ToPsm8Scrambler {
  Psm32ToPsm8Scrambler(int w, int h, int write_tex_width, int read_tex_width);
  std::vector<int> destinations_per_byte;
};

struct BaseClutReader {
  std::array<int, 256> addrs;
  BaseClutReader() {
    for (int i = 0; i < 256; i++) {
      u32 clut_chunk = i / 16;
      u32 off_in_chunk = i % 16;
      u8 clx = 0, cly = 0;
      if (clut_chunk & 1) {
        clx = 8;
      }
      cly = (clut_chunk >> 1) * 2;
      if (off_in_chunk >= 8) {
        off_in_chunk -= 8;
        cly++;
      }
      clx += off_in_chunk;

      // the x, y CLUT value is looked up in PSMCT32 mode
      u32 clut_addr = clx + cly * 16;
      ASSERT(clut_addr < 256);
      addrs[i] = clut_addr;
    }
  }
};

struct BaseLayerVals {
  math::Vector4f color = math::Vector4f::zero();
  math::Vector2f scale = math::Vector2f::zero();
  math::Vector2f offset = math::Vector2f::zero();
  math::Vector2f st_scale = math::Vector2f::zero();
  math::Vector2f st_offset = math::Vector2f::zero();
  math::Vector4f qs = math::Vector4f(1, 1, 1, 1);
  float rot = 0;
  float st_rot = 0;
  u8 pad[8];
};
static_assert(sizeof(BaseLayerVals) == 80);

/*!
 * A single layer in a FixedAnimationDef.
 */
struct BaseFixedLayerDef {
  enum class Kind {
    DEFAULT_ANIM_LAYER,
  } kind = Kind::DEFAULT_ANIM_LAYER;
  float start_time = 0;
  float end_time = 0;
  std::string tex_name;
  bool z_writes = false;
  bool z_test = false;
  bool clamp_u = false;
  bool clamp_v = false;
  bool blend_enable = true;
  bool channel_masks[4] = {true, true, true, true};
  GsAlpha::BlendMode blend_modes[4];  // abcd
  u8 blend_fix = 0;

  void set_blend_b2_d1() {
    blend_modes[0] = GsAlpha::BlendMode::SOURCE;
    blend_modes[1] = GsAlpha::BlendMode::ZERO_OR_FIXED;
    blend_modes[2] = GsAlpha::BlendMode::SOURCE;
    blend_modes[3] = GsAlpha::BlendMode::DEST;
    blend_fix = 0;
  }

  void set_blend_b1_d1() {
    blend_modes[0] = GsAlpha::BlendMode::SOURCE;
    blend_modes[1] = GsAlpha::BlendMode::DEST;
    blend_modes[2] = GsAlpha::BlendMode::SOURCE;
    blend_modes[3] = GsAlpha::BlendMode::DEST;
    blend_fix = 0;
  }
  void set_no_z_write_no_z_test() {
    z_writes = false;
    z_test = false;
  }
  void set_clamp() {
    clamp_v = true;
    clamp_u = true;
  }
};

struct BaseFixedAnimDef {
  math::Vector4<u8> color;  // clear color
  std::string tex_name;
  std::optional<math::Vector2<int>> override_size;
  // assuming (new 'static 'gs-test :ate #x1 :afail #x1 :zte #x1 :ztst (gs-ztest always))
  // alpha blend off, so alpha doesn't matter i think.
  std::vector<BaseFixedLayerDef> layers;
  bool move_to_pool = false;
};

struct DynamicLayerData {
  BaseLayerVals start_vals, end_vals;
};

struct BaseFixedAnim {
  BaseFixedAnimDef def;
  std::vector<DynamicLayerData> dynamic_data;
  int dest_slot;
};

/*
 (deftype sky-input (structure)
  ((fog-height float)
   (cloud-min  float)
   (cloud-max  float)
   (times      float 9)
   (cloud-dest int32)
   )
  )
 */

struct SkyInput {
  float fog_height;
  float cloud_min;
  float cloud_max;
  float times[9];
  int32_t cloud_dest;
};

struct SlimeInput {
  // float alphas[4];
  float times[9];
  int32_t dest;
  int32_t scroll_dest;
};

using Vector16ub = math::Vector<u8, 16>;

namespace texture_utils {
int make_noise_texture(u8* dest, Vector16ub* random_table, int dim, int random_index_in);
}

struct BaseNoiseTexturePair {
  std::vector<u8> temp_data;
  int dim = 0;
  float scale = 0;
  float last_time = 0;
  float max_time = 0;
};


class BaseClutBlender {
 public:
  BaseClutBlender(const std::string& dest,
              const std::vector<std::string>& sources,
              const std::optional<std::string>& level_name,
              const tfrag3::Level* level);
  virtual void run(const float* weight) = 0;

protected:
  const tfrag3::IndexTexture* m_dest;
  std::vector<const std::array<math::Vector4<u8>, 256>*> m_cluts;
  std::vector<float> m_current_weights;
  std::array<math::Vector4<u8>, 256> m_temp_clut;
  std::vector<u32> m_temp_rgba;
};

class BaseTextureAnimator {
 public:
  BaseTextureAnimator(const tfrag3::Level* common_level);
  ~BaseTextureAnimator();

  // note: for now these can't be easily changed because each layer has its own hand-tuned
  // parameters from the original game. If you want to change it, you'll need to make up parameters
  // for those new layers.
  // must be power of 2 - number of 16-byte rows in random table. (original
  // game has 8)
  static constexpr int kRandomTableSize = 8;

  // must be power of 2 - dimensions of the final clouds textures
  static constexpr int kFinalSkyTextureSize = 128;
  static constexpr int kFinalSlimeTextureSize = 128;

  // number of small sub-textures. Must be less than log2(kFinalTextureSize).
  static constexpr int kNumSkyNoiseLayers = 4;
  static constexpr int kNumSlimeNoiseLayers = 4;

 protected:
  void handle_upload_clut_16_16(const DmaTransfer& tf, const u8* ee_mem);
  virtual void handle_generic_upload(const DmaTransfer& tf, const u8* ee_mem) = 0;
  void handle_erase_dest(DmaFollower& dma);
  void handle_set_shader(DmaFollower& dma);

  virtual void handle_graphics_erase_dest(DmaTransfer& dma,
                                          int tex_width,
                                          int tex_height,
                                          int dest_texture_address,
                                          math::Vector<u32, 4> rgba_u32) = 0;

  void load_clut_to_converter();
  const u32* get_clut_16_16_psm32(int cbp);

  virtual void force_to_gpu(int tbp) = 0;

  struct DrawData {
    u8 tmpl1[16];
    math::Vector<u32, 4> color;

    math::Vector<float, 4> st0;
    math::Vector<u32, 4> pos0;

    math::Vector<float, 4> st1;
    math::Vector<u32, 4> pos1;

    math::Vector<float, 4> st2;
    math::Vector<u32, 4> pos2;

    math::Vector<float, 4> st3;
    math::Vector<u32, 4> pos3;
  };

  void set_output_slots_at_index(unsigned, float);
  virtual void set_uniforms_from_draw_data(const DrawData& dd, int dest_w, int dest_h);
  virtual void clear_in_use_temp_texture() = 0;
  virtual void handle_texture_anim_data(DmaFollower& dma, const u8* ee_mem) = 0;
  virtual void handle_draw(DmaFollower& dma) = 0;

  virtual BaseVramEntry& get_vram_entry_at_index(unsigned) = 0;
  virtual bool is_vram_entry_available_at_index(unsigned) = 0;
  virtual void update_and_move_texture_data_to_pool() = 0;

  virtual unsigned long get_clut_blender_groups_size() = 0;
  virtual void clut_blender_groups_emplace_back() = 0;

  virtual void set_uniform_vector_four_float(float, float, float, float) = 0;
  virtual void set_uniform_vector_three_float(float*) = 0;
  virtual void set_uniform_vector_two_float(float*) = 0;

  int create_clut_blender_group(const std::vector<std::string>& textures,
                                const std::string& suffix0,
                                const std::string& suffix1,
                                const std::optional<std::string>& dgo);
  virtual void add_to_clut_blender_group(int idx,
                                         const std::vector<std::string>& textures,
                                         const std::string& suffix0,
                                         const std::string& suffix1,
                                         const std::optional<std::string>& dgo) = 0;
  virtual void run_clut_blender_group(DmaTransfer& tf, int idx) = 0;
  virtual void run_clut_blender_group(DmaTransfer& tf, int idx, u64 frame_idx) = 0;

  void setup_texture_anims();
  virtual int create_fixed_anim_array(const std::vector<BaseFixedAnimDef>& defs) = 0;

  virtual void run_fixed_animation_array(int idx, DmaTransfer& tf) = 0;

  void loop_over_dma_tex_anims_operations(DmaFollower& dma, const u8* ee_mem, u64 frame_idx);
  virtual void draw_debug_window();

  virtual void imgui_show_final_slime_tex() = 0;
  virtual void imgui_show_final_slime_scroll_tex() = 0;

  virtual void imgui_show_sky_blend_tex() = 0;
  virtual void imgui_show_sky_final_tex() = 0;

  virtual void handle_slime(const DmaTransfer& tf) = 0;
  virtual void handle_clouds_and_fog(const DmaTransfer& tf) = 0;
  virtual void set_tex_looking_for_clut() = 0;

  virtual int get_private_output_slots_id(int idx);
  virtual void imgui_show_private_output_slots_at_index(int idx) = 0;
  virtual void handle_graphics_erase_dest(DmaFollower& dma,
                                          int tex_width,
                                          int tex_height,
                                          int dest_texture_address,
                                          math::Vector<u32, 4> rgba_u32) = 0;

  const tfrag3::Level* m_common_level = nullptr;
  std::unordered_map<u32, PcTextureId> m_ids_by_vram;

  std::vector<u8> m_output_debug_flags;

  std::set<u32> m_erased_on_this_frame;

  ShaderContext m_current_shader;
  TextureConverter m_converter;
  int m_current_dest_tbp = -1;

  struct Vertex {
    u32 index;
    u32 pad1;
    u32 pad2;
    u32 pad3;
  };

  u8 m_index_to_clut_addr[256];

  Psm32ToPsm8Scrambler m_psm32_to_psm8_8_8, m_psm32_to_psm8_16_16, m_psm32_to_psm8_32_32,
      m_psm32_to_psm8_64_64;

  SkyInput m_debug_sky_input;
  SlimeInput m_debug_slime_input;

  int m_skull_gem_fixed_anim_array_idx = -1;
  int m_bomb_fixed_anim_array_idx = -1;
  int m_cas_conveyor_anim_array_idx = -1;
  int m_security_anim_array_idx = -1;
  int m_waterfall_anim_array_idx = -1;
  int m_waterfall_b_anim_array_idx = -1;
  int m_lava_anim_array_idx = -1;
  int m_lava_b_anim_array_idx = -1;
  int m_stadiumb_anim_array_idx = -1;
  int m_fortress_pris_anim_array_idx = -1;
  int m_fortress_warp_anim_array_idx = -1;
  int m_metkor_anim_array_idx = -1;
  int m_shield_anim_array_idx = -1;
  int m_krew_holo_anim_array_idx = -1;

  int m_darkjak_clut_blender_idx = -1;
  int m_jakb_prison_clut_blender_idx = -1;
  int m_jakb_oracle_clut_blender_idx = -1;
  int m_jakb_nest_clut_blender_idx = -1;
  int m_kor_transform_clut_blender_idx = -1;

  int m_slime_output_slot = -1;
  int m_slime_scroll_output_slot = -1;
};
