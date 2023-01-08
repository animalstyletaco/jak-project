#pragma once

#include <optional>

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
  void InitializeVertexBuffer();

 private:
  void create_pipeline_layout() override;
  void update_load(const LevelDataVulkan* loader_data);
  bool setup_for_level(const std::string& level, BaseSharedRenderState* render_state) override;
  void discard_tree_cache() override;

  struct Tree {
    std::unique_ptr<VulkanTexture> time_of_day_texture;
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
  std::vector<VulkanTexture>* m_textures;
  u64 m_load_id = -1;

  std::vector<VulkanTexture> m_time_of_day_textures;

  std::vector<math::Vector<u8, 4>> m_color_result;

  static constexpr int TIME_OF_DAY_COLOR_COUNT = 8192;
  bool m_has_level = false;

  struct Cache {
    std::vector<std::pair<int, int>> draw_idx_temp;
    std::vector<u32> index_temp;
    std::vector<std::pair<int, int>> multidraw_offset_per_stripdraw;
    std::vector<u32> multidraw_count_buffer;
    std::vector<void*> multidraw_index_offset_buffer;
  } m_cache;

  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<IndexBuffer>  m_index_buffer;
  std::unique_ptr<IndexBuffer>  m_single_draw_index_buffer;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer> m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_time_of_day_color_buffer;
};

