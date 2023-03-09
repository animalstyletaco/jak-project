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

  std::unique_ptr<Tfrag3Vulkan> m_tfrag3;
};

