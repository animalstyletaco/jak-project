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
       int level_id);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_shaders(VulkanShaderLibrary& shaders) override;
  ~Tie3Vulkan();

  void update_load(const LevelDataVulkan* loader_data);
  void render_tree(int idx,
                   int geom,
                   const TfragRenderSettings& settings,
                   BaseSharedRenderState* render_state,
                   ScopedProfilerNode& prof) override;
  bool setup_for_level(const std::string& str, BaseSharedRenderState* render_state) override;

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

  struct Tree {
    VertexBuffer* vertex_buffer = nullptr;
    IndexBuffer* index_buffer = nullptr;
    IndexBuffer* single_draw_index_buffer = nullptr;
    VulkanTexture* time_of_day_texture = nullptr;
    u32 vert_count;
    const std::vector<tfrag3::StripDraw>* draws = nullptr;
    const std::vector<tfrag3::InstancedStripDraw>* wind_draws = nullptr;
    const std::vector<tfrag3::TieWindInstance>* instance_info = nullptr;
    const std::vector<tfrag3::TimeOfDayColor>* colors = nullptr;
    const tfrag3::BVH* vis = nullptr;
    const u32* index_data = nullptr;
    SwizzledTimeOfDay tod_cache;

    std::vector<std::array<math::Vector4f, 4>> wind_matrix_cache;

    bool has_wind = false;
    IndexBuffer* wind_vertex_index_buffer;
    std::vector<u32> wind_vertex_index_offsets;

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

  struct Cache {
    std::vector<background_common::DrawSettings> draw_idx_temp;
    std::vector<background_common::DrawSettings> multidraw_offset_per_stripdraw;
    std::vector<u32> index_temp;
    std::vector<u8> vis_temp;
    std::vector<VkMultiDrawIndexedInfoEXT> multi_draw_indexed_infos;
  } m_cache;

  std::array<std::vector<Tree>, 4> m_trees;  // includes 4 lods!
  std::vector<VulkanTexture>* m_textures;
  std::vector<VulkanSamplerHelper> m_time_of_day_samplers;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer> m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_time_of_day_color;
  std::vector<VulkanTexture> textures[tfrag3::TIE_GEOS];
};
