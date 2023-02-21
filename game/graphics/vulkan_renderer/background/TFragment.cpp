#include "TFragment.h"

#include "third-party/imgui/imgui.h"

TFragmentVulkan::TFragmentVulkan(const std::string& name,
                     int my_id,
                     std::unique_ptr<GraphicsDeviceVulkan>& device,
                     VulkanInitializationInfo& vulkan_info,
                     const std::vector<tfrag3::TFragmentTreeKind>& trees,
                     bool child_mode,
                     int level_id)
    : BaseTFragment(name, my_id, trees, child_mode, level_id), BucketVulkanRenderer(device, vulkan_info) {


  m_tfrag3 = std::make_unique<Tfrag3Vulkan>(device, vulkan_info);
}

void TFragmentVulkan::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  BaseTFragment::render(dma, render_state, prof);
}


void TFragmentVulkan::tfrag3_setup_for_level(
    const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
    const std::string& level,
    BaseSharedRenderState* render_state) {
  m_tfrag3->setup_for_level(tree_kinds, level, render_state);
}

void TFragmentVulkan::tfrag3_render_matching_trees(
    int geom,
    const std::vector<tfrag3::TFragmentTreeKind>& trees,
    const TfragRenderSettings& settings,
    BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) {
  m_tfrag3->render_matching_trees(geom, trees, settings, render_state, prof);
}

void TFragmentVulkan::draw_debug_window() {
  m_tfrag3->draw_debug_window();
}

int TFragmentVulkan::tfrag3_lod() {
  return m_tfrag3->lod();
}
