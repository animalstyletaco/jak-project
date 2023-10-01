#pragma once

#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "common/custom_data/Tfrag3Data.h"
#include "common/dma/dma_chain_read.h"
#include "common/dma/gs.h"
#include "common/math/Vector.h"
#include "common/texture/texture_conversion.h"
#include "game/graphics/general_renderer/TextureAnimator.h"

#include "game/graphics/texture/TextureConverter.h"
#include "game/graphics/texture/TextureID.h"

#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"

struct GpuTexture;

struct VulkanTextureAnimationPool {
  VulkanTextureAnimationPool();
  ~VulkanTextureAnimationPool();
  VulkanTexture* allocate(u64 w, u64 h);
  void free(VulkanTexture* texture, u64 w, u64 h);
  std::unordered_map<u64, std::vector<VulkanTexture*>> textures;
};

class ClutVulkanBlender {
 public:
  ClutVulkanBlender(const std::string& dest,
              const std::array<std::string, 2>& sources,
              const std::optional<std::string>& level_name,
              const tfrag3::Level* level,
              VulkanTextureAnimationPool* tpool);
  VulkanTexture* run(const float* weights);
  VulkanTexture* texture() const { return m_texture; }
  bool at_default() const { return m_current_weights[0] == 1.f && m_current_weights[1] == 0.f; }

 private:
  const tfrag3::IndexTexture* m_dest;
  std::array<const std::array<math::Vector4<u8>, 256>*, 2> m_cluts;
  std::array<float, 2> m_current_weights;
  VulkanTexture* m_texture;
  std::array<math::Vector4<u8>, 256> m_temp_clut;
  std::vector<u32> m_temp_rgba;
};

class VulkanTexturePool;

class VulkanTextureAnimator : public BaseTextureAnimator {
 public:
  VulkanTextureAnimator(VulkanShaderLibrary& shaders, const tfrag3::Level* common_level);
  ~VulkanTextureAnimator();
  void handle_texture_anim_data(DmaFollower& dma,
                                const u8* ee_mem,
                                VulkanTexturePool* texture_pool,
                                u64 frame_idx);
  VulkanTexture* get_by_slot(int idx);
  void draw_debug_window();
  const std::vector<VulkanTexture*>* slots() { return &m_public_output_slots; }
  void clear_stale_textures(u64 frame_idx);

 private:
  void copy_private_to_public();
  void setup_texture_anims();
  void setup_sky();
  void handle_upload_clut_16_16(const DmaTransfer& tf, const u8* ee_mem);
  void handle_generic_upload(const DmaTransfer& tf, const u8* ee_mem);
  void handle_clouds_and_fog(const DmaTransfer& tf, BaseTexturePool* texture_pool);
  void handle_slime(const DmaTransfer& tf, BaseTexturePool* texture_pool);
  void handle_erase_dest(DmaFollower& dma);
  void handle_set_shader(DmaFollower& dma);
  void handle_draw(DmaFollower& dma, TexturePool& texture_pool);

  BaseVramEntry* setup_vram_entry_for_gpu_texture(int w, int h, int tbp);
  void set_up_opengl_for_fixed(const FixedLayerDef& def, std::optional<VulkanTexture*> texture);
  bool set_up_opengl_for_shader(const ShaderContext& shader,
                                std::optional<VulkanTexture*> texture,
                                bool prim_abe);
  VulkanTexture* make_temp_gpu_texture(const u32* data, u32 width, u32 height);

  VulkanTexture* make_or_get_gpu_texture_for_current_shader(VulkanTexturePool& texture_pool);
  const u32* get_clut_16_16_psm32(int cbp);
  void load_clut_to_converter();
  void force_to_gpu(int tbp);

  int create_fixed_anim_array(const std::vector<FixedAnimDef>& defs);
  void run_fixed_animation_array(int idx, const DmaTransfer& transfer, VulkanTexturePool* texture_pool);
  void run_fixed_animation(FixedAnim& anim, float time);

  void set_uniforms_from_draw_data(const DrawData& dd, int dest_w, int dest_h);
  void set_draw_data_from_interpolated(DrawData* result, const LayerVals& vals, int w, int h);

  PcTextureId get_id_for_tbp(TexturePool* pool, u64 tbp, u64 other_id);

  BaseVramEntry* m_tex_looking_for_clut = nullptr;
  const tfrag3::Level* m_common_level = nullptr;
  std::unordered_map<u32, VulkanVramEntry> m_textures;
  std::unordered_map<u64, PcTextureId> m_ids_by_vram;

  std::set<u32> m_force_to_gpu;  // rename? or rework to not need?

  TextureConverter m_converter;
  std::vector<VulkanTexture> m_in_use_temp_textures;
  ShaderContext m_current_shader;

  std::unique_ptr<VertexBuffer> m_vertex_buffer;

  std::unique_ptr<VulkanTexture> m_shader_id;
  std::unique_ptr<VulkanTexture> m_dummy_texture;

  u8 m_index_to_clut_addr[256];
  VulkanTextureAnimationPool m_opengl_texture_pool;
  int m_current_dest_tbp = -1;

  std::vector<VulkanTexture*> m_private_output_slots;
  std::vector<VulkanTexture*> m_public_output_slots;
  std::vector<int> m_skip_tbps;

  struct Bool {
    bool b = false;
  };
  std::vector<Bool> m_output_debug_flags;

  struct ClutBlenderGroup {
    std::vector<ClutBlender> blenders;
    std::vector<int> outputs;
    u64 last_updated_frame = 0;
  };
  std::vector<ClutBlenderGroup> m_clut_blender_groups;

  int m_darkjak_clut_blender_idx = -1;
  int m_jakb_prison_clut_blender_idx = -1;
  int m_jakb_oracle_clut_blender_idx = -1;
  int m_jakb_nest_clut_blender_idx = -1;
  int m_kor_transform_clut_blender_idx = -1;

  int create_clut_blender_group(const std::vector<std::string>& textures,
                                const std::string& suffix0,
                                const std::string& suffix1,
                                const std::optional<std::string>& dgo);
  void add_to_clut_blender_group(int idx,
                                 const std::vector<std::string>& textures,
                                 const std::string& suffix0,
                                 const std::string& suffix1,
                                 const std::optional<std::string>& dgo);
  void run_clut_blender_group(DmaTransfer& tf, int idx, u64 frame_idx);
  VulkanTexture* run_clouds(const SkyInput& input);
  void run_slime(const SlimeInput& input);

  Psm32ToPsm8Scrambler m_psm32_to_psm8_8_8, m_psm32_to_psm8_16_16, m_psm32_to_psm8_32_32,
      m_psm32_to_psm8_64_64;
  ClutReader m_clut_table;

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

  std::vector<FixedAnimArray> m_fixed_anim_arrays;

 public:
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

 private:
  Vector16ub m_random_table[kRandomTableSize];
  int m_random_index = 0;

  SkyInput m_debug_sky_input;
  VulkanNoiseTexturePair m_sky_noise_textures[kNumSkyNoiseLayers];
  FramebufferVulkanTexture m_sky_blend_texture;
  FramebufferVulkanTexture m_sky_final_texture;
  GpuTexture* m_sky_pool_gpu_tex = nullptr;

  SlimeInput m_debug_slime_input;
  NoiseTexturePair m_slime_noise_textures[kNumSkyNoiseLayers];
  FramebufferVulkanTexture m_slime_blend_texture;
  FramebufferVulkanTexture m_slime_final_texture, m_slime_final_scroll_texture;
  GpuTexture* m_slime_pool_gpu_tex = nullptr;
  GpuTexture* m_slime_scroll_pool_gpu_tex = nullptr;
  int m_slime_output_slot = -1;
  int m_slime_scroll_output_slot = -1;
};
