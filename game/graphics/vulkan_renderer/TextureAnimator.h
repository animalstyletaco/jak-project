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
#include "game/graphics/texture/TexturePoolDataTypes.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/FramebufferHelper.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"

class VulkanGpuTextureMap;

class VulkanTextureAnimationPool {
 public:
  VulkanTextureAnimationPool(std::shared_ptr<GraphicsDeviceVulkan> device);
  VulkanTexture* allocate(u64 w, u64 h);
  void free(VulkanTexture* texture, u64 w, u64 h);
  VulkanTexture* GetTexture(u64 key);
  bool IsTextureAvailable(u64 key);

 private:
  std::unordered_map<u64, std::vector<VulkanTexture>> m_textures;
  std::shared_ptr<GraphicsDeviceVulkan> m_device;
};

struct VulkanFixedAnim : BaseFixedAnim {
  std::optional<FramebufferVulkanHelper> fbt;
  std::vector<VulkanTexture*> src_textures;

  // GpuTexture* pool_gpu_tex = nullptr;
};

struct VulkanFixedAnimArray {
  std::vector<VulkanFixedAnim> anims;
};

struct VulkanVramEntry : public BaseVramEntry {
  VulkanGpuTextureMap* pool_gpu_tex = nullptr;
  std::optional<FramebufferVulkanHelper> tex;

  void reset() override {
    BaseVramEntry::reset();
    pool_gpu_tex = nullptr;
  }
};

class ClutVulkanBlender : public BaseClutBlender {
 public:
  ClutVulkanBlender(const std::string& dest,
                    const std::array<std::string, 2>& sources,
                    const std::optional<std::string>& level_name,
                    const tfrag3::Level* level,
                    VulkanTextureAnimationPool* tpool);
  void run(const float* weights) override;
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
  VulkanTextureAnimator(std::shared_ptr<GraphicsDeviceVulkan> device,
                        VulkanInitializationInfo& vulkan_info,
                        const tfrag3::Level* common_level);
  ~VulkanTextureAnimator();
  void handle_texture_anim_data(DmaFollower& dma,
                                const u8* ee_mem,
                                VulkanTexturePool* texture_pool,
                                u64 frame_idx);
  VulkanTexture* get_by_slot(int idx);
  const std::vector<VulkanTexture*>* slots() { return &m_public_output_slots; }
  void clear_stale_textures(u64 frame_idx);

 private:
  void copy_private_to_public();
  void setup_texture_anims();
  void setup_sky();
  void handle_upload_clut_16_16(const DmaTransfer& tf, const u8* ee_mem);
  void handle_generic_upload(const DmaTransfer& tf, const u8* ee_mem) override;
  void handle_clouds_and_fog(const DmaTransfer& tf) override;
  void handle_slime(const DmaTransfer& tf) override;
  void handle_erase_dest(DmaFollower& dma);
  void handle_set_shader(DmaFollower& dma);
  void handle_draw(DmaFollower& dma, VulkanTexturePool& texture_pool);

  VulkanVramEntry* setup_vram_entry_for_gpu_texture(int w, int h, int tbp);
  void set_up_vulkan_for_fixed(const BaseFixedLayerDef& def, std::optional<VulkanTexture*> texture);
  bool set_up_vulkan_for_shader(const ShaderContext& shader,
                                std::optional<VulkanTexture*> texture,
                                bool prim_abe);
  VulkanTexture* make_temp_gpu_texture(const u32* data, u32 width, u32 height);

  VulkanTexture* make_or_get_gpu_texture_for_current_shader(VulkanTexturePool& texture_pool);
  const u32* get_clut_16_16_psm32(int cbp);
  void load_clut_to_converter();
  void force_to_gpu(int tbp);

  int create_fixed_anim_array(const std::vector<BaseFixedAnimDef>& defs) override;
  void run_fixed_animation_array(int idx,
                                 const DmaTransfer& transfer,
                                 VulkanTexturePool* texture_pool);
  void run_fixed_animation(BaseFixedAnim& anim, float time);

  void set_uniforms_from_draw_data(const DrawData& dd, int dest_w, int dest_h);
  void set_draw_data_from_interpolated(DrawData* result, const BaseLayerVals& vals, int w, int h);

  PcTextureId get_id_for_tbp(VulkanTexturePool* pool, u64 tbp, u64 other_id);
  void create_pipeline_layout();
  void InitializeVertexDescriptions();

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VulkanInitializationInfo& m_vulkan_info;

  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;

  BaseTextureAnimationVertexUniformBufferData
      m_vertex_shader_data{};  // local copy of data that gets passed to uniform buffer
  std::unique_ptr<UniformVulkanBuffer> m_vertex_uniform_buffer;
  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;

  BaseTextureAnimationFragmentPushConstant m_fragment_push_constant;

  VulkanVramEntry* m_tex_looking_for_clut = nullptr;
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
  VulkanTextureAnimationPool m_texture_animation_pool;
  int m_current_dest_tbp = -1;

  std::vector<VulkanTexture*> m_public_output_slots;
  std::vector<VulkanTexture*> m_private_output_slots;
  std::vector<int> m_skip_tbps;

  struct ClutVulkanBlenderGroup {
    std::vector<ClutVulkanBlender> blenders;
    std::vector<int> outputs;
    u64 last_updated_frame = 0;
  };
  std::vector<ClutVulkanBlenderGroup> m_clut_blender_groups;

  void set_uniform_vector_three_float(float* position) override;
  void set_uniform_vector_two_float(float* uv) override;
  void handle_graphics_erase_dest(DmaFollower& dma,
                                  int tex_width,
                                  int tex_height,
                                  int dest_texture_address,
                                  math::Vector<u32, 4> rgba_u32) override;

  int create_clut_blender_group(const std::vector<std::string>& textures,
                                const std::string& suffix0,
                                const std::string& suffix1,
                                const std::optional<std::string>& dgo);
  void add_to_clut_blender_group(int idx,
                                 const std::vector<std::string>& textures,
                                 const std::string& suffix0,
                                 const std::string& suffix1,
                                 const std::optional<std::string>& dgo);
  void run_clut_blender_group(DmaTransfer& tf, int idx, u64 frame_idx) override;
  VulkanTexture* run_clouds(const SkyInput& input);
  void run_slime(const SlimeInput& input);

  std::vector<VulkanFixedAnimArray> m_fixed_anim_arrays;

 private:
  void imgui_show_final_slime_tex() override;
  void imgui_show_final_slime_scroll_tex() override;

  void imgui_show_sky_blend_tex() override;
  void imgui_show_sky_final_tex() override;

  void imgui_show_private_output_slots_at_index(int idx) override;

  Vector16ub m_random_table[kRandomTableSize];
  int m_random_index = 0;

  BaseNoiseTexturePair m_sky_noise_textures[kNumSkyNoiseLayers];
  FramebufferVulkanHelper m_sky_blend_texture;
  FramebufferVulkanHelper m_sky_final_texture;
  VulkanGpuTextureMap* m_sky_pool_gpu_tex = nullptr;

  BaseNoiseTexturePair m_slime_noise_textures[kNumSkyNoiseLayers];
  FramebufferVulkanHelper m_slime_blend_texture;
  FramebufferVulkanHelper m_slime_final_texture, m_slime_final_scroll_texture;
  VulkanTexture* m_slime_pool_gpu_tex = nullptr;
  VulkanTexture* m_slime_scroll_pool_gpu_tex = nullptr;

  VulkanTexturePool* m_texture_pool = nullptr;
};
