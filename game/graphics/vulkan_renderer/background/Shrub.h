#pragma once

#include <optional>
#include <unordered_map>

#include "common/util/FilteredValue.h"

#include "game/graphics/general_renderer/background/Shrub.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

class ShrubVulkan : public BaseShrub, public BucketVulkanRenderer {
 public:
  ShrubVulkan(const std::string& name,
              int my_id,
              std::shared_ptr<GraphicsDeviceVulkan> device,
              VulkanInitializationInfo& vulkan_info);
  ~ShrubVulkan();
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof, VkCommandBuffer command_buffer) override;

 protected:
  void render_all_trees(const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof) override;
  void render_tree(int idx,
                   const TfragRenderSettings& settings,
                   BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof);

 protected:
  void InitializeVertexDescriptions();
  void InitializeShaders();

  void create_pipeline_layout() override;
  void update_load(const LevelDataVulkan* loader_data);
  bool setup_for_level(const std::string& level, BaseSharedRenderState* render_state) override;
  void discard_tree_cache() override;

  struct TreeVulkan : Tree {
    std::unique_ptr<VulkanTexture> time_of_day_texture;
    std::vector<std::unique_ptr<MultiDrawVulkanBuffer>> multidraw_buffers;

    VertexBuffer* vertex_buffer;
    IndexBuffer* index_buffer;
    IndexBuffer* single_draw_index_buffer;

    std::unique_ptr<VulkanSamplerHelper> time_of_day_sampler_helper;
    std::vector<VulkanSamplerHelper> sampler_helpers;

    VkDescriptorSet vertex_shader_descriptor_set;
    std::vector<VkDescriptorSet> fragment_shader_descriptor_sets;

    VkDescriptorImageInfo time_of_day_descriptor_image_info;
    VkDescriptorImageInfo descriptor_image_info;

    std::vector<VulkanDrawIndirectCommandSet> multi_draw_indexed_infos_collection;
  };

  void PrepareVulkanDraw(TreeVulkan& tree, unsigned index);

  struct alignas(float) ShrubPushConstant : BackgroundCommonVertexUniformShaderData {
    float height_scale;
    float scissor_adjust;
    int decal_mode = 0;
  } m_vertex_shrub_push_constant;

  std::vector<TreeVulkan> m_trees;
  std::string m_level_name;
  std::unordered_map<u32, VulkanTexture>* m_textures;
  u64 m_load_id = -1;

  std::vector<math::Vector<u8, 4>> m_color_result;
  bool m_has_level = false;

  struct Cache {
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<u32> index_temp;
  } m_cache;

  BackgroundCommonFragmentPushConstantShaderData m_time_of_day_push_constant;

  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::unique_ptr<MultiDrawVulkanBuffer> m_multi_draw_buffer;

  static constexpr unsigned kMaxVulkanIndirectDraw = 10000;
};

class ShrubVulkanJak1 : public ShrubVulkan {
 public:
  ShrubVulkanJak1(const std::string& name,
                  int my_id,
                  std::shared_ptr<GraphicsDeviceVulkan> device,
                  VulkanInitializationInfo& vulkan_info)
      : ShrubVulkan(name, my_id, device, vulkan_info) {
    m_vertex_shrub_push_constant.height_scale = 1;
    m_vertex_shrub_push_constant.scissor_adjust = -512 / 448.0;
  }
};
