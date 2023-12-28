#pragma once

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/background/TFragment.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/background/Tie3.h"

class TFragmentVulkan : public BaseTFragment, public BucketVulkanRenderer {
 public:
  TFragmentVulkan(const std::string& name,
                  int my_id,
                  std::shared_ptr<GraphicsDeviceVulkan> device,
                  VulkanInitializationInfo& vulkan_info,
                  const std::vector<tfrag3::TFragmentTreeKind>& trees,
                  bool child_mode,
                  int level_id,
                  const std::vector<VulkanTexture*>* anim_slots);
  virtual ~TFragmentVulkan();
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void render_matching_trees(int geom,
                             const std::vector<tfrag3::TFragmentTreeKind>& trees,
                             const TfragRenderSettings& settings,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof) override;

  void render_tree(int geom,
                   const TfragRenderSettings& settings,
                   BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof) override;

  bool setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                       const std::string& level,
                       BaseSharedRenderState* render_state) override;

  void discard_tree_cache() override;

  void render_tree_cull_debug(const TfragRenderSettings& settings,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) override;

  void update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                   const LevelDataVulkan* loader_data);

 protected:
  void InitializeInputVertexAttribute();
  void InitializeDebugInputVertexAttribute();
  void create_pipeline_layout();

  TreeCache& get_cached_tree(int bucket_index, int cache_index) override;
  size_t get_total_cached_trees_count(int bucket_index) override;
  void initialize_debug_pipeline();

  struct Cache {
    std::vector<u8> vis_temp;
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<u32> index_temp;
  } m_cache;

  struct TreeCacheVulkan : TreeCache {
    std::unique_ptr<VulkanTexture> time_of_day_texture;
    VertexBuffer* vertex_buffer;
    IndexBuffer* index_buffer;
    std::unique_ptr<IndexBuffer> single_draw_index_buffer;
    std::vector<VulkanSamplerHelper> time_of_day_samplers;
    std::vector<VulkanSamplerHelper> sampler_helpers;

    std::vector<VkDescriptorImageInfo> vertex_descriptor_image_infos;
    std::vector<VkDescriptorImageInfo> fragment_descriptor_image_infos;

    std::vector<VkDescriptorSet> vertex_shader_descriptor_sets;
    std::vector<VkDescriptorSet> fragment_shader_descriptor_sets;

    std::vector<VulkanDrawIndirectCommandSet> multi_draw_indexed_infos_collection;
  };

  struct alignas(float) TiePushConstant : BackgroundCommonVertexUniformShaderData {
    float height_scale;
    float scissor_adjust;
    int decal_mode = 0;
  } m_vertex_push_constant;

  void PrepareVulkanDraw(TreeCacheVulkan& tree, VulkanTexture& time_of_day_texture, int index);
  void AllocateDescriptorSets(std::vector<VkDescriptorSet>& descriptorSets,
                              VkDescriptorSetLayout& layout,
                              u32 descriptorSetCount);

  std::array<std::vector<TreeCacheVulkan>, GEOM_MAX> m_cached_trees;
  std::unordered_map<u32, VulkanTexture>* m_textures = nullptr;

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VulkanInitializationInfo& m_vulkan_info;

  PipelineConfigInfo m_debug_pipeline_config_info{};
  VkDescriptorBufferInfo m_vertex_shader_buffer_descriptor_info;

  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;

  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;

  std::vector<VkDescriptorSet> m_global_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_global_fragment_shader_descriptor_sets;

  BackgroundCommonFragmentPushConstantShaderData m_time_of_day_color_push_constant;

  std::unique_ptr<VertexBuffer> m_debug_vertex_buffer;

  std::unique_ptr<VulkanTexture> m_placeholder_texture;
  std::unique_ptr<VulkanSamplerHelper> m_placeholder_sampler;
  std::unique_ptr<MultiDrawVulkanBuffer> m_multi_draw_buffer;

  const std::vector<VulkanTexture*>* m_anim_slot_array = nullptr;
  static constexpr unsigned kMaxVulkanIndirectDraw = 30000;
};

class TFragmentVulkanJak1 : public TFragmentVulkan {
 public:
  TFragmentVulkanJak1(const std::string& name,
                      int my_id,
                      std::shared_ptr<GraphicsDeviceVulkan> device,
                      VulkanInitializationInfo& vulkan_info,
                      const std::vector<tfrag3::TFragmentTreeKind>& trees,
                      bool child_mode,
                      int level_id,
                      const std::vector<VulkanTexture*>* anim_slots)
      : TFragmentVulkan(name, my_id, device, vulkan_info, trees, child_mode, level_id, anim_slots) {
    m_vertex_push_constant.height_scale = 1;
    m_vertex_push_constant.scissor_adjust = -512 / 448.0;
  }
  ~TFragmentVulkanJak1() = default;
};
