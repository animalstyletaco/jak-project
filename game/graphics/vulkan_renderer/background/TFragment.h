#pragma once

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/background/TFragment.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/background/Tfrag3.h"
#include "game/graphics/vulkan_renderer/background/Tie3.h"

class TFragmentVulkan : public BaseTFragment, public BucketVulkanRenderer {
 public:
  TFragmentVulkan(const std::string& name,
                  int my_id,
                  std::unique_ptr<GraphicsDeviceVulkan>& device,
                  VulkanInitializationInfo& vulkan_info,
                  const std::vector<tfrag3::TFragmentTreeKind>& trees,
                  bool child_mode,
                  int level_id);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 private:
  void tfrag3_setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                              const std::string& level,
                              BaseSharedRenderState* render_state) override;
  void tfrag3_render_matching_trees(int geom,
                                    const std::vector<tfrag3::TFragmentTreeKind>& trees,
                                    const TfragRenderSettings& settings,
                                    BaseSharedRenderState* render_state,
                                    ScopedProfilerNode& prof) override;

  int tfrag3_lod() override;

  void InitializeInputVertexAttribute();
  void InitializeDebugInputVertexAttribute();
  void handle_initialization(DmaFollower& dma);

  bool m_child_mode = false;
  bool m_override_time_of_day = false;
  float m_time_of_days[8] = {1, 0, 0, 0, 0, 0, 0, 0};

  // GS setup data
  u8 m_test_setup[32];

  // VU data
  TFragData m_tfrag_data;

  TfragPcPortData m_pc_port_data;

  // buffers
  TFragBufferedData m_buffered_data[2];

  enum TFragDataMem {
    Buffer0_Start = 0,
    TFragMatrix0 = 5,

    Buffer1_Start = 328,
    TFragMatrix1 = TFragMatrix0 + Buffer1_Start,

    TFragFrameData = 656,
    TFragKickZoneData = 670,
  };

  enum TFragProgMem {
    TFragSetup = 0,
  };

  std::unique_ptr<Tfrag3Vulkan> m_tfrag3;
  std::vector<tfrag3::TFragmentTreeKind> m_tree_kinds;
  int m_level_id;

  std::unique_ptr<BackgroundCommonVertexUniformBuffer> m_vertex_shader_uniform_buffer;
  std::unique_ptr<BackgroundCommonFragmentUniformBuffer> m_time_of_day_color;
};

