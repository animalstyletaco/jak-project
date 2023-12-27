#pragma once

#include <optional>

#include "common/util/FilteredValue.h"

#include "game/graphics/general_renderer/background/Tie3.h"
#include "game/graphics/gfx.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

class Tie3Vulkan : public BaseTie3, public BucketVulkanRenderer {
 public:
  Tie3Vulkan(const std::string& name,
             int my_id,
             std::shared_ptr<GraphicsDeviceVulkan> device,
             VulkanInitializationInfo& vulkan_info,
             int level_id,
             tfrag3::TieCategory category = tfrag3::TieCategory::NORMAL);
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
  virtual ~Tie3Vulkan();

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
  void setup_shader(ShaderId);

  int lod() const { return Gfx::g_global_settings.lod_tie; }

 protected:
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
    VertexBuffer* vertex_buffer;
    IndexBuffer* index_buffer;
    std::unique_ptr<IndexBuffer> single_draw_index_buffer;
    std::unique_ptr<VulkanTexture> time_of_day_texture;
    IndexBuffer* wind_index_buffer;

    std::unique_ptr<VulkanSamplerHelper> time_of_day_sampler_helper;
    std::vector<VulkanSamplerHelper> instanced_wind_sampler_helpers;
    std::vector<VulkanSamplerHelper> sampler_helpers_categories;

    uint32_t etie_base_vertex_shader_descriptor_set_start_index;
    uint32_t etie_vertex_shader_descriptor_set_start_index;
    uint32_t vertex_shader_descriptor_set_start_index;
    uint32_t fragment_shader_descriptor_set_start_index;
    uint32_t instanced_wind_vertex_shader_descriptor_set_start_index;
    uint32_t instanced_wind_fragment_shader_descriptor_set_start_index;

    std::vector<VkDescriptorBufferInfo> etie_descriptor_buffer_infos;
    std::vector<VkDescriptorImageInfo> time_of_day_descriptor_image_infos;
    std::vector<VkDescriptorImageInfo> time_of_day_instanced_wind_descriptor_image_infos;
    std::vector<VkDescriptorImageInfo> descriptor_image_infos;
    std::vector<VkDescriptorImageInfo> instanced_wind_descriptor_image_infos;

    std::vector<VkDescriptorSet> etie_base_vertex_shader_descriptor_sets;
    std::vector<VkDescriptorSet> etie_vertex_shader_descriptor_sets;
    std::vector<VkDescriptorSet> vertex_shader_descriptor_sets;
    std::vector<VkDescriptorSet> fragment_shader_descriptor_sets;
    std::vector<VkDescriptorSet> instanced_wind_vertex_shader_descriptor_sets;
    std::vector<VkDescriptorSet> instanced_wind_fragment_shader_descriptor_sets;

    std::vector<std::vector<VkMultiDrawIndexedInfoEXT>> multi_draw_indexed_infos_collection;
  };

  void PrepareDescriptorSets(std::vector<VkDescriptorSet>& descriptorSets,
                              VkDescriptorSetLayout& layout,
                              u32 descriptorSetCount,
                              DescriptorWriter* writer);
  void envmap_second_pass_draw(TreeVulkan& tree,
                               const TfragRenderSettings& settings,
                               BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof,
                               tfrag3::TieCategory category,
                               int index,
                               int geom);

  struct Cache {
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<u32> index_temp;
    std::vector<u8> vis_temp;
  } m_cache;

  struct TiePushConstant : BackgroundCommonVertexUniformShaderData {
    float height_scale;
    float scissor_adjust;
    int settings = 0;
  };

  void PrepareVulkanDraw(TreeVulkan& tree,
                         VkImageView textureImageView,
                         VkSampler sampler,
                         VkDescriptorImageInfo& time_of_day_descriptor_info,
                         VkDescriptorImageInfo& descriptor_info,
                         VkDescriptorSet vertex_descriptor_set,
                         VkDescriptorSet fragment_descriptor_set,
                         std::unique_ptr<DescriptorWriter>& vertex_descriptor_writer,
                         std::unique_ptr<DescriptorWriter>& fragment_descriptor_writer);

  size_t get_tree_count(int geom) override { return m_trees[geom].size(); }
  void init_etie_cam_uniforms(GoalBackgroundCameraData& render_state);
  GraphicsPipelineLayout* GetSelectedGraphicsPipelineLayout();

  std::array<std::vector<TreeVulkan>, 4> m_trees;  // includes 4 lods!
  std::unordered_map<u32, VulkanTexture>* m_textures;

  VkDescriptorBufferInfo m_etie_vertex_buffer_descriptor_info{};

  std::unique_ptr<DescriptorLayout> m_etie_vertex_descriptor_layout;
  std::unique_ptr<DescriptorLayout> m_etie_base_vertex_descriptor_layout;

  std::unique_ptr<DescriptorWriter> m_etie_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_etie_base_vertex_descriptor_writer;

    GraphicsPipelineLayout m_tfrag3_no_tex_graphics_pipeline_layout{m_device};
  GraphicsPipelineLayout m_etie_base_graphics_pipeline_layout{m_device};
  GraphicsPipelineLayout m_etie_graphics_pipeline_layout{m_device};

  VkPipelineLayout m_tie_pipeline_layout;
  VkPipelineLayout m_etie_pipeline_layout;
  VkPipelineLayout m_etie_base_pipeline_layout;

  TiePushConstant m_tie_vertex_push_constant;
  BackgroundCommonFragmentPushConstantShaderData m_time_of_day_color_push_constant;

  std::unique_ptr<BackgroundCommonEtieBaseVertexUniformBuffer>
      m_etie_base_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonEtieVertexUniformBuffer> m_etie_vertex_shader_uniform_buffer;

  VkDescriptorBufferInfo m_etie_base_descriptor_buffer_info{};
  VkDescriptorBufferInfo m_etie_descriptor_buffer_info{};

  std::unordered_map<u32, VulkanTexture> texture_maps[tfrag3::TIE_GEOS];

  std::array<VkVertexInputAttributeDescription, 3> tfrag_attribute_descriptions{};
  std::array<VkVertexInputAttributeDescription, 5> etie_attribute_descriptions{};

  std::vector<VkDescriptorSet> m_global_etie_base_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_global_etie_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_global_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_global_fragment_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_global_instanced_wind_vertex_shader_descriptor_sets;
  std::vector<VkDescriptorSet> m_global_instanced_wind_fragment_shader_descriptor_sets;
};

class Tie3VulkanJak1 : public Tie3Vulkan {
 public:
  Tie3VulkanJak1(const std::string& name,
                 int my_id,
                 std::shared_ptr<GraphicsDeviceVulkan> device,
                 VulkanInitializationInfo& vulkan_info,
                 int level_id,
                 tfrag3::TieCategory category = tfrag3::TieCategory::NORMAL)
      : Tie3Vulkan(name, my_id, device, vulkan_info, level_id, category) {
    m_tie_vertex_push_constant.height_scale = 1;
    m_tie_vertex_push_constant.scissor_adjust = -512 / 448.f;
  }
  ~Tie3VulkanJak1() = default;
};

class Tie3VulkanAnotherCategory : public BaseBucketRenderer, public BucketVulkanRenderer {
 public:
  Tie3VulkanAnotherCategory(const std::string& name,
                            int my_id,
                            std::shared_ptr<GraphicsDeviceVulkan> device,
                            VulkanInitializationInfo& vulkan_info,
                            Tie3Vulkan* parent,
                            tfrag3::TieCategory category);
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
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
                           std::shared_ptr<GraphicsDeviceVulkan> device,
                           VulkanInitializationInfo& vulkan_info,
                           int level_id);
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 private:
  bool m_enable_envmap = true;
};
