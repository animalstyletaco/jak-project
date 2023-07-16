#pragma once

#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "common/custom_data/Tfrag3Data.h"
#include "common/dma/dma_chain_read.h"
#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/TextureAnimator.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

struct GpuTexture;

struct VramEntryVulkan : public BaseVramEntry {
  //std::optional<FramebufferTexturePair> tex;
  GpuVulkanTexture* pool_gpu_tex = nullptr;

  void reset() override{
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

class ClutBlenderVulkan : public BaseClutBlender {
 public:
  ClutBlenderVulkan(const std::string& dest,
                    const std::vector<std::string>& sources,
                    const std::optional<std::string>& level_name,
                    const tfrag3::Level* level, std::unique_ptr<GraphicsDeviceVulkan>& device);
  void run(const float* weight) override;

 protected:
   VulkanTexture m_texture;
};

class VulkanTextureAnimator : public BaseTextureAnimator {
 public:
  VulkanTextureAnimator(const tfrag3::Level* common_level, std::unique_ptr<GraphicsDeviceVulkan>&, VulkanInitializationInfo&);
  ~VulkanTextureAnimator();

 protected:
  void handle_set_shader(DmaFollower& dma);
  void handle_draw(DmaFollower& dma) override;
  void handle_rg_to_ba(const DmaTransfer& tf);
  void handle_set_clut_alpha(const DmaTransfer& tf);
  void handle_copy_clut_alpha(const DmaTransfer& tf);

  VramEntryVulkan* setup_vram_entry_for_gpu_texture(int w, int h, int tbp);

  void force_to_gpu(int tbp) override;

  std::unordered_map<u32, VramEntryVulkan> m_textures;
  std::vector<VulkanTexture> m_in_use_temp_textures;
  std::vector<VulkanTexture> m_output_slots;

  struct ClutBlenderGroup {
    std::vector<ClutBlenderVulkan> blenders;
    std::vector<int> outputs;
  };
  std::vector<ClutBlenderGroup> m_clut_blender_groups;

  void handle_texture_anim_data(DmaFollower& dma, const u8* ee_mem) override;
  int create_clut_blender_group(const std::vector<std::string>& textures,
                                const std::string& suffix0,
                                const std::string& suffix1,
                                const std::optional<std::string>& dgo);
  void add_to_clut_blender_group(int idx,
                                 const std::vector<std::string>& textures,
                                 const std::string& suffix0,
                                 const std::string& suffix1,
                                 const std::optional<std::string>& dgo) override;

  void handle_texture_anim_data(DmaFollower& dma,
                                const u8* ee_mem,
                                VulkanTexturePool* texture_pool);
  void clear_in_use_temp_texture() override;
  void update_and_move_texture_data_to_pool() override;
  BaseVramEntry& get_vram_entry_at_index(unsigned) override;
  bool is_vram_entry_available_at_index(unsigned) override;

 private:
  void InitializeInputVertexAttribute();
  void create_pipeline_layout();
  void init_shaders();

  VertexPushConstant m_vertex_push_constant{};
  FragmentPushConstant m_fragment_push_constant{};

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VulkanInitializationInfo& m_vulkan_info;
  VulkanTexture m_dummy_texture;

  std::unique_ptr<VertexBuffer> m_vertex_buffer;

  PipelineConfigInfo m_pipeline_config_info{};
};
