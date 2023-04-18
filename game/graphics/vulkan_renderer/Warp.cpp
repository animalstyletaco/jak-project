#include "Warp.h"

WarpVulkan::WarpVulkan(const std::string& name, int id, std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info) :
  BaseWarp(name, id), BucketVulkanRenderer(device, vulkan_info), m_generic(name, id, device, vulkan_info), m_fb_copier(device, vulkan_info.swap_chain) {

  VulkanTextureInput in;
  // point to fb copier's texture.
  in.texture = m_fb_copier.Texture();
  //in.w = 32;
  //in.h = 32;
  in.debug_page_name = "PC-WARP";
  in.debug_name = "PC-WARP";
  in.id = m_vulkan_info.texture_pool->allocate_pc_port_texture(m_vulkan_info.m_version);
  //m_warp_src_tex = m_vulkan_info.texture_pool->give_texture_and_load_to_vram(in, m_tbp);
}

WarpVulkan::~WarpVulkan() {}

void WarpVulkan::generic_draw_debug_window() {
  m_generic.draw_debug_window();
}

void WarpVulkan::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  //m_fb_copier.copy_now(render_state->render_fb_w, render_state->render_fb_h,
  //                     render_state->render_fb_x, render_state->render_fb_y,
  //                     render_state->render_fb);
  //m_vulkan_info.texture_pool->move_existing_to_vram(m_warp_src_tex, m_tbp);
  BaseWarp::render(dma, render_state, prof);
}

