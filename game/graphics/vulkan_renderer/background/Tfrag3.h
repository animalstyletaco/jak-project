#pragma once

#include "common/custom_data/Tfrag3Data.h"
#include "common/math/Vector.h"

#include "game/graphics/gfx.h"
#include "game/graphics/general_renderer/background/Tfrag3.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

class Tfrag3Vulkan : public BaseTfrag3 {
 public:
  Tfrag3Vulkan(VulkanInitializationInfo& vulkan_info,
         PipelineConfigInfo& pipeline_config_info,
         GraphicsPipelineLayout& pipeline_layout,
         std::unique_ptr<DescriptorWriter>& vertex_description_writer,
         std::unique_ptr<DescriptorWriter>& fragment_description_writer,
         std::unique_ptr<BackgroundCommonVertexUniformBuffer>& vertex_shader_uniform_buffer,
         std::unique_ptr<BackgroundCommonFragmentUniformBuffer>& fragment_shader_uniform_buffer);
  ~Tfrag3Vulkan();
  void render_matching_trees(int geom,
                             const std::vector<tfrag3::TFragmentTreeKind>& trees,
                             const TfragRenderSettings& settings,
                             BaseSharedRenderState* render_state,
                             ScopedProfilerNode& prof);

  void render_all_trees(int geom,
                        const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  void render_tree(int geom,
                   const TfragRenderSettings& settings,
                   BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof);

  bool setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                       const std::string& level,
                       BaseSharedRenderState* render_state) override;

  void discard_tree_cache();

  void render_tree_cull_debug(const TfragRenderSettings& settings,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof);

  void update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                   const BaseLevelData* loader_data);

 private:
  static constexpr int GEOM_MAX = 3;

  struct TreeCacheVulkan : TreeCache {
    VertexBuffer* vertex_buffer = nullptr;
    IndexBuffer* index_buffer = nullptr;
    IndexBuffer* single_draw_index_buffer = nullptr;
    VulkanTexture* time_of_day_texture = nullptr;
  };

  const std::vector<VulkanTexture>* m_textures = nullptr;

  PipelineConfigInfo& m_pipeline_config_info;
  GraphicsPipelineLayout& m_pipeline_layout;
  VulkanInitializationInfo& m_vulkan_info;

  std::unique_ptr<DescriptorWriter>& m_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter>& m_fragment_descriptor_writer;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer>& m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer>& m_time_of_day_color;

  VkSamplerCreateInfo m_sampler_info;
  VkSampler m_sampler = VK_NULL_HANDLE;
};
