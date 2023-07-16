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

// IDs sent from GOAL telling us what texture operation to perform.
enum PcTextureAnimCodes {
  FINISH_ARRAY = 13,
  ERASE_DEST_TEXTURE = 14,
  UPLOAD_CLUT_16_16 = 15,
  GENERIC_UPLOAD = 16,
  SET_SHADER = 17,
  DRAW = 18,
  MOVE_RG_TO_BA = 19,
  SET_CLUT_ALPHA = 20,
  COPY_CLUT_ALPHA = 21,
  DARKJAK = 22,
  PRISON_JAK = 23,
  ORACLE_JAK = 24,
  NEST_JAK = 25,
  KOR_TRANSFORM = 26
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
  u8 pad[3];
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
  enum class Kind { CLUT16_16_IN_PSM32, GENERIC_PSM32, GENERIC_PSMT8, GPU, INVALID } kind;
  std::vector<u8> data;

  int tex_width = 0;
  int tex_height = 0;
  int dest_texture_address = 0;
  int cbp = 0;
  // math::Vector<u8, 4> rgba_clear;

  bool needs_pool_update = false;
  GpuTexture* pool_gpu_tex = nullptr;

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

 protected:
  void handle_upload_clut_16_16(const DmaTransfer& tf, const u8* ee_mem);
  void handle_generic_upload(const DmaTransfer& tf, const u8* ee_mem);
  void handle_erase_dest(DmaFollower& dma);
  void handle_set_shader(DmaFollower& dma);
  void handle_rg_to_ba(const DmaTransfer& tf);
  void handle_set_clut_alpha(const DmaTransfer& tf);
  void handle_copy_clut_alpha(const DmaTransfer& tf);

  virtual BaseVramEntry* setup_vram_entry_for_gpu_texture(int w, int h, int tbp) = 0;

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

  BaseVramEntry* m_tex_looking_for_clut = nullptr;
  const tfrag3::Level* m_common_level = nullptr;
  std::unordered_map<u32, PcTextureId> m_ids_by_vram;

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

  struct alignas(float) VertexPushConstant {
    math::Vector2f uvs[4];
    math::Vector3f positions[4];
  };

  struct alignas(float) FragmentPushConstant {
    int enable_tex;
    int tcc;
    math::Vector4f rgba;
    math::Vector<int, 4> channel_scramble;
  };

  u8 m_index_to_clut_addr[256];

  virtual unsigned long get_clut_blender_groups_size() = 0;
  virtual void clut_blender_groups_emplace_back() = 0;

  virtual void set_uniform_vector_four_float(float, float, float, float) = 0;
  virtual void set_uniform_vector_three_float(float*) = 0;
  virtual void set_uniform_vector_two_float(float*) = 0;

  int m_darkjak_clut_blender_idx = -1;
  int m_jakb_prison_clut_blender_idx = -1;
  int m_jakb_oracle_clut_blender_idx = -1;
  int m_jakb_nest_clut_blender_idx = -1;
  int m_kor_transform_clut_blender_idx = -1;

  int create_clut_blender_group(const std::vector<std::string>& textures,
                                const std::string& suffix0,
                                const std::string& suffix1,
                                const std::optional<std::string>& dgo);
  virtual void add_to_clut_blender_group(int idx,
                                         const std::vector<std::string>& textures,
                                         const std::string& suffix0,
                                         const std::string& suffix1,
                                         const std::optional<std::string>& dgo) = 0;
  void run_clut_blender_group(DmaTransfer& tf, int idx);

  //  std::vector<ClutBlender> m_darkjak_blenders;
  //  std::vector<int> m_darkjak_output_slots;
  //
  //  std::vector<ClutBlender> m_jakb_prison_blenders;
  //  std::vector<int> m_jakb_prison_output_slots;
  //
  //  std::vector<ClutBlender> m_jakb_oracle_blenders;
  //  std::vector<int> m_jakb_oracle_slots;
  //
  //  std::vector<ClutBlender> m_jakb_nest_blenders;
  //  std::vector<int> m_jakb_nest_slots;
};
