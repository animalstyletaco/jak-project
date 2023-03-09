#pragma once

#include "common/custom_data/Tfrag3Data.h"
#include "common/math/Vector.h"

#include "game/graphics/gfx.h"
#include "game/graphics/general_renderer/background/Tfrag3.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

class Tfrag3Vulkan : public BaseTfrag3 {
 public:
  Tfrag3Vulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo & vulkan_info);
  ~Tfrag3Vulkan();
  void render_matching_trees(int geom,
                             const std::vector<tfrag3::TFragmentTreeKind>& trees,
                             const TfragRenderSettings& settings,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof);

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

 private:
  void InitializeInputVertexAttribute();
  void InitializeDebugInputVertexAttribute();
  void create_pipeline_layout();

  TreeCache& get_cached_tree(int bucket_index, int cache_index) override;
  size_t get_total_cached_trees_count(int bucket_index) override;
  void initialize_debug_pipeline();

  struct Cache {
    std::vector<u8> vis_temp;
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<background_common::DrawSettings> multidraw_offset_per_stripdraw;
    std::vector<u32> index_temp;
    std::vector<VkMultiDrawIndexedInfoEXT> multi_draw_indexed_infos;
  } m_cache;

  struct TreeCacheVulkan : TreeCache {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
    std::unique_ptr<IndexBuffer> single_draw_index_buffer;
  };

  struct PushConstant {
    float height_scale;
    float scissor_adjust;
    int index;
  };

  PushConstant m_push_constant;

  void PrepareVulkanDraw(TreeCacheVulkan& tree, int index);

  std::array<std::vector<TreeCacheVulkan>, GEOM_MAX> m_cached_trees;
  std::unordered_map<u32, VulkanTexture>* m_textures = nullptr;
  std::vector<VulkanSamplerHelper> m_time_of_day_samplers;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VulkanInitializationInfo& m_vulkan_info;

  PipelineConfigInfo m_debug_pipeline_config_info{};
  PipelineConfigInfo m_pipeline_config_info{};
  std::vector<GraphicsPipelineLayout> m_pipeline_layouts;

  VkDescriptorBufferInfo m_vertex_shader_buffer_descriptor_info;
  VkDescriptorBufferInfo m_vertex_time_of_day_buffer_descriptor_info;
  VkDescriptorBufferInfo m_fragment_buffer_descriptor_info;

  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;

  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer> m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_time_of_day_color_uniform_buffer;
  // Ideally wanted this to be a texel buffer but dynamic texel buffer is not supported in Vulkan yet
  std::unique_ptr<UniformVulkanBuffer> m_time_of_day_uniform_buffer;

  std::vector<VkDescriptorSet> m_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_fragment_shader_descriptor_sets;

  std::unique_ptr<VertexBuffer> m_debug_vertex_buffer;
  std::vector<VkDescriptorSet> m_descriptor_sets;

  std::vector<VkDescriptorImageInfo> m_descriptor_image_infos;

  std::unique_ptr<VulkanTexture> m_placeholder_texture;
  std::unique_ptr<VulkanSamplerHelper> m_placeholder_sampler;
  VkDescriptorImageInfo m_placeholder_descriptor_image_info;
};
