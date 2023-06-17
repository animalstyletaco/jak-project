#include "ProgressRenderer.h"

ProgressVulkanRenderer::ProgressVulkanRenderer(const std::string& name,
                                               int my_id,
                                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                                               VulkanInitializationInfo& vulkan_info,
                                               int batch_size)
    : DirectVulkanRenderer(name, my_id, device, vulkan_info, batch_size),
      m_minimap_fb(kMinimapWidth, kMinimapHeight, VK_FORMAT_R8G8B8A8_UNORM, device) {
  VulkanTextureInput in;
  in.texture = &m_minimap_fb.ColorAttachmentTexture();
  in.debug_page_name = "PC-MAP";
  in.debug_name = "map";
  in.id = m_vulkan_info.texture_pool->allocate_pc_port_texture(m_vulkan_info.m_version);
  // m_minimap_gpu_tex = m_vulkan_info.texture_pool->give_texture_and_load_to_vram(in,
  // kMinimapVramAddr);
}

void ProgressVulkanRenderer::pre_render() {
  m_current_fbp = kScreenFbp;
}

void ProgressVulkanRenderer::post_render() {
  //m_fb_ctxt.reset();
  m_offscreen_mode = false;
}

void ProgressVulkanRenderer::handle_frame(u64 val,
                                    BaseSharedRenderState* render_state,
                                    ScopedProfilerNode& prof) {
  GsFrame f(val);
  u32 fbp = f.fbp();
  bool flushed = false;
  if (fbp != m_current_fbp) {
    flush_pending(render_state, prof);
    flushed = true;
    m_prim_graphics_state_needs_graphics_update = true;
    m_current_fbp = fbp;
    switch (f.fbp()) {
      case kScreenFbp:  // 408
        //m_fb_ctxt.reset();
        m_offscreen_mode = false;
        break;
      case kMinimapFbp:  // 126
        //m_fb_ctxt.emplace(m_minimap_fb);
        m_offscreen_mode = true;
        break;
      default:
        fmt::print("Unknown fbp in ProgressRenderer: {}\n", f.fbp());
        ASSERT(false);
    }
  }

  bool write_rgb = f.fbmsk() != 0xffffff;
  if (write_rgb != m_test_state.write_rgb) {
    if (!flushed) {
      m_stats.flush_from_test++;
      flush_pending(render_state, prof);
    }

    m_test_state.write_rgb = write_rgb;
    m_test_state_needs_graphics_update = true;
    m_prim_graphics_state_needs_graphics_update = true;
  }
}
