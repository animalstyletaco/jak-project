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
        std::unique_ptr<GraphicsDeviceVulkan>& device,
        VulkanInitializationInfo& vulkan_info);
  ~ShrubVulkan();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

 protected:
  void render_all_trees(const TfragRenderSettings& settings,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof) override;
  void render_tree(int idx,
                   const TfragRenderSettings& settings,
                   BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof);

 private:
  void InitializeVertexDescriptions();
  void InitializeShaders();

  void create_pipeline_layout() override;
  void update_load(const LevelDataVulkan* loader_data);
  bool setup_for_level(const std::string& level, BaseSharedRenderState* render_state) override;
  void discard_tree_cache() override;
  void PrepareVulkanDraw(int bucket);

  struct Tree {
    std::unique_ptr<VulkanTexture> time_of_day_texture;
    std::vector<MultiDrawVulkanBuffer> multidraw_buffers;

    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
    std::unique_ptr<IndexBuffer> single_draw_index_buffer;

    u32 vert_count;
    const std::vector<tfrag3::ShrubDraw>* draws = nullptr;
    const std::vector<tfrag3::TieWindInstance>* instance_info = nullptr;
    const std::vector<tfrag3::TimeOfDayColor>* colors = nullptr;
    const u32* index_data = nullptr;
    SwizzledTimeOfDay tod_cache;

    struct {
      u32 draws = 0;
      u32 wind_draws = 0;
      Filtered<float> cull_time;
      Filtered<float> index_time;
      Filtered<float> tod_time;
      Filtered<float> setup_time;
      Filtered<float> draw_time;
      Filtered<float> tree_time;
    } perf;
  };

  std::vector<Tree> m_trees;
  std::string m_level_name;
  std::unordered_map<u32, VulkanTexture>* m_textures;
  u64 m_load_id = -1;

  std::vector<math::Vector<u8, 4>> m_color_result;
  bool m_has_level = false;

  struct Cache {
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<background_common::DrawSettings> multidraw_offset_per_stripdraw;
    std::vector<u32> index_temp;
    std::vector<VkMultiDrawIndexedInfoEXT> multi_draw_indexed_infos;
  } m_cache;

  VkDescriptorBufferInfo m_vertex_shader_buffer_descriptor_info;
  VkDescriptorBufferInfo m_fragment_buffer_descriptor_info;

  std::vector<VkDescriptorImageInfo> m_time_of_day_descriptor_image_infos;
  std::vector<VkDescriptorImageInfo> m_shrub_descriptor_image_infos;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer> m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_time_of_day_color_buffer;

  std::vector<VkDescriptorSet> m_time_of_day_descriptor_sets;
  std::vector<VkDescriptorSet> m_shrub_descriptor_sets;

  std::vector<VulkanSamplerHelper> m_time_of_day_samplers;
};
