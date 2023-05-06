#pragma once

#include <optional>

#include "common/util/FilteredValue.h"

#include "game/graphics/gfx.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"
#include "game/graphics/general_renderer/background/Tie3.h"

class Tie3Vulkan : public BaseTie3, public BucketVulkanRenderer {
 public:
  Tie3Vulkan(const std::string& name,
       int my_id,
       std::unique_ptr<GraphicsDeviceVulkan>& device,
       VulkanInitializationInfo& vulkan_info,
             int level_id,
             tfrag3::TieCategory category = tfrag3::TieCategory::NORMAL);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_shaders(VulkanShaderLibrary& shaders) override;
  ~Tie3Vulkan();

  bool try_loading_level(const std::string& str, BaseSharedRenderState* render_state) override;

  void draw_matching_draws_for_tree(int idx,
                                    int geom,
                                    const TfragRenderSettings& settings,
                                    BaseSharedRenderState* render_state,
                                    ScopedProfilerNode& prof,
                                    tfrag3::TieCategory category) override;

  void load_from_fr3_data(const LevelDataVulkan* loader_data);
  void setup_tree(int idx,
                  int geom,
                  const TfragRenderSettings& settings,
                  const u8* render_state,
                  size_t proto_vis_data_size,
                  bool use_multidraw,
                  ScopedProfilerNode& prof) override;

  int lod() const { return Gfx::g_global_settings.lod_tie; }

 private:
  void InitializeInputAttributes();
  void discard_tree_cache() override;
  void create_pipeline_layout() override;
  void render_tree_wind(int idx,
                        int geom,
                        const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  struct TreeVulkan : Tree {
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<background_common::DrawSettings> multidraw_idx_temp;
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
    std::unique_ptr<IndexBuffer> single_draw_index_buffer;
    std::unique_ptr<VulkanTexture> time_of_day_texture;

    std::unique_ptr<VertexBuffer> wind_vertex_buffer;
    std::unique_ptr<IndexBuffer> wind_index_buffer;
  };

  void envmap_second_pass_draw(TreeVulkan& tree,
                               const TfragRenderSettings& settings,
                               BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof,
                               tfrag3::TieCategory category,
                               int index);

  struct Cache {
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<background_common::DrawSettings> multidraw_offset_per_stripdraw;
    std::vector<u32> index_temp;
    std::vector<u8> vis_temp;
    std::vector<VkMultiDrawIndexedInfoEXT> multi_draw_indexed_infos;
  } m_cache;

  struct TiePushConstant {
    float height_scale;
    float scissor_adjust;
    int decal_mode = 0;
  } m_tie_push_constant;

  void PrepareVulkanDraw(TreeVulkan& tree, int index);
  size_t get_tree_count(int geom) override { return m_trees[geom].size(); }
  void init_etie_cam_uniforms(const BaseSharedRenderState* render_state);

  std::array<std::vector<TreeVulkan>, 4> m_trees;  // includes 4 lods!
  std::unordered_map<u32, VulkanTexture>* m_textures;
  std::vector<VulkanSamplerHelper> m_time_of_day_samplers;

  VkDescriptorBufferInfo m_time_of_day_descriptor_info;
  std::vector<GraphicsPipelineLayout> m_graphics_wind_pipeline_layouts;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer> m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_time_of_day_color_uniform_buffer;

  std::unique_ptr<BackgroundCommonEtieVertexUniformBuffer> m_etie_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_etie_time_of_day_color_uniform_buffer;

  std::unique_ptr<UniformVulkanBuffer> m_time_of_day_uniform_buffer;
  std::unordered_map<u32, VulkanTexture> texture_maps[tfrag3::TIE_GEOS];

  std::vector<VkDescriptorImageInfo> m_descriptor_image_infos;

  std::vector<VkDescriptorSet> m_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_fragment_shader_descriptor_sets;
};

class Tie3VulkanAnotherCategory : public BaseBucketRenderer, public BucketVulkanRenderer {
 public:
  Tie3VulkanAnotherCategory(const std::string& name,
                            int my_id,
                            std::unique_ptr<GraphicsDeviceVulkan>& device,
                            VulkanInitializationInfo& vulkan_info,
                            Tie3Vulkan* parent,
                            tfrag3::TieCategory category);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void render(DmaFollower& dma,
              BaseSharedRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 private:
  Tie3Vulkan* m_parent;
  tfrag3::TieCategory m_category;
};

/*!
 * Jak 1 - specific renderer that does TIE and TIE envmap in one.
 */
class Tie3VulkanWithEnvmapJak1 : public Tie3Vulkan {
 public:
  Tie3VulkanWithEnvmapJak1(const std::string& name,
                           int my_id,
                           std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VulkanInitializationInfo& vulkan_info,
                           int level_id);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 private:
  bool m_enable_envmap = true;
};
