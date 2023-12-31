#include "SkyRenderer.h"

#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

// The sky texture system blends together sky textures from different levels and times of day
// to create the final sky texture.

// The sequence is:
//  set-display-gs-state 8qw
//  copy-sky-textures (between 0 and 8, usually 2.)
//  copy-cloud-texture
//  set alpha state
//  reset display gs state
// and this happens twice: one for each level.  Note that the first call to either of the copy
// functions will use "draw" mode instead of "blend"
// The results are stored in special sky textures.

// size of the sky texture is 64x96, but it's actually a 64x64 (clouds) and a 32x32 (sky)

SkyBlendVulkanHandler::SkyBlendVulkanHandler(const std::string& name,
                                             int my_id,
                                             std::shared_ptr<GraphicsDeviceVulkan> device,
                                             VulkanInitializationInfo& vulkan_info,
                                             int level_id,
                                             std::shared_ptr<SkyBlendVulkanGPU> shared_blender,
                                             std::shared_ptr<SkyBlendCPU> shared_blender_cpu)
    : BaseSkyBlendHandler(name, my_id, level_id),
      BucketVulkanRenderer(device, vulkan_info),
      m_shared_gpu_blender(shared_blender),
      m_shared_cpu_blender(shared_blender_cpu) {}

SkyBlendVulkanHandlerJak1::SkyBlendVulkanHandlerJak1(
    const std::string& name,
    int my_id,
    std::shared_ptr<GraphicsDeviceVulkan> device,
    VulkanInitializationInfo& vulkan_info,
    int level_id,
    std::shared_ptr<SkyBlendVulkanGPU> shared_blender,
    std::shared_ptr<SkyBlendCPU> shared_blender_cpu,
    const std::vector<VulkanTexture*>* anim_slots)
    : SkyBlendVulkanHandler(name,
                            my_id,
                            device,
                            vulkan_info,
                            level_id,
                            shared_blender,
                            shared_blender_cpu),
      m_tfrag_renderer(fmt::format("tfrag-{}", name),
                       my_id,
                       device,
                       vulkan_info,
                       {tfrag3::TFragmentTreeKind::TRANS, tfrag3::TFragmentTreeKind::LOWRES_TRANS},
                       true,
                       level_id,
                       anim_slots) {}

SkyBlendVulkanHandlerJak2::SkyBlendVulkanHandlerJak2(
    const std::string& name,
    int my_id,
    std::shared_ptr<GraphicsDeviceVulkan> device,
    VulkanInitializationInfo& vulkan_info,
    int level_id,
    std::shared_ptr<SkyBlendVulkanGPU> shared_blender,
    std::shared_ptr<SkyBlendCPU> shared_blender_cpu,
    const std::vector<VulkanTexture*>* anim_slots)
    : SkyBlendVulkanHandler(name,
                            my_id,
                            device,
                            vulkan_info,
                            level_id,
                            shared_blender,
                            shared_blender_cpu),
      m_tfrag_renderer(fmt::format("tfrag-{}", name),
                       my_id,
                       device,
                       vulkan_info,
                       {tfrag3::TFragmentTreeKind::TRANS, tfrag3::TFragmentTreeKind::LOWRES_TRANS},
                       true,
                       level_id,
                       anim_slots) {}

void SkyBlendVulkanHandler::render(DmaFollower& dma,
                                   SharedVulkanRenderState* render_state,
                                   ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  m_command_buffer = command_buffer;
  BaseSkyBlendHandler::render(dma, render_state, prof);
}

void SkyBlendVulkanHandler::handle_sky_copies(DmaFollower& dma,
                                              SharedVulkanRenderState* render_state,
                                              ScopedProfilerNode& prof) {
  if (!m_enabled) {
    while (dma.current_tag().qwc == 6) {
      dma.read_and_advance();
      dma.read_and_advance();
    }
    return;
  } else {
    if (render_state->use_sky_cpu) {
      m_gpu_stats = m_shared_cpu_blender->do_sky_blends(dma, render_state, prof);

    } else {
      m_gpu_stats = m_shared_gpu_blender->do_sky_blends(dma, render_state, prof, m_command_buffer);
    }
  }
}

SkyBlendStats SkyBlendVulkanHandler::cpu_blender_do_sky_blends(DmaFollower& dma,
                                                               BaseSharedRenderState* render_state,
                                                               ScopedProfilerNode& prof) {
  return m_shared_cpu_blender->do_sky_blends(dma, render_state, prof);
}

SkyBlendStats SkyBlendVulkanHandler::gpu_blender_do_sky_blends(DmaFollower& dma,
                                                               BaseSharedRenderState* render_state,
                                                               ScopedProfilerNode& prof) {
  return m_shared_gpu_blender->do_sky_blends(dma, render_state, prof, m_command_buffer);
}

void SkyBlendVulkanHandler::tfrag_renderer_render(DmaFollower& dma,
                                                  BaseSharedRenderState* render_state,
                                                  ScopedProfilerNode& tfrag_prof) {
  GetTFragmentRenderer().render(dma, (SharedVulkanRenderState*)render_state, tfrag_prof, m_command_buffer);
}

void SkyBlendVulkanHandler::tfrag_renderer_draw_debug_window() {
  GetTFragmentRenderer().draw_debug_window();
}

SkyVulkanRenderer::SkyVulkanRenderer(const std::string& name,
                                     int my_id,
                                     std::shared_ptr<GraphicsDeviceVulkan> device,
                                     VulkanInitializationInfo& vulkan_info)
    : BaseSkyRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info) {}

SkyVulkanRendererJak1::SkyVulkanRendererJak1(const std::string& name,
                                             int my_id,
                                             std::shared_ptr<GraphicsDeviceVulkan> device,
                                             VulkanInitializationInfo& vulkan_info)
    : SkyVulkanRenderer(name, my_id, device, vulkan_info),
      m_direct_renderer("sky-direct", my_id, device, vulkan_info, 100) {}

SkyVulkanRendererJak2::SkyVulkanRendererJak2(const std::string& name,
                                             int my_id,
                                             std::shared_ptr<GraphicsDeviceVulkan> device,
                                             VulkanInitializationInfo& vulkan_info)
    : SkyVulkanRenderer(name, my_id, device, vulkan_info),
      m_direct_renderer("sky-direct", my_id, device, vulkan_info, 100) {}

void SkyVulkanRenderer::render(DmaFollower& dma,
                               SharedVulkanRenderState* render_state,
                               ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  m_direct_renderer_call_count = 0;
  GetDirectRenderer().set_command_buffer(command_buffer);
  GetDirectRenderer().set_current_index(m_direct_renderer_call_count);
  BaseSkyRenderer::render(dma, render_state, prof);
}

void SkyVulkanRenderer::direct_renderer_reset_state() {
  GetDirectRenderer().reset_state();
}

void SkyVulkanRenderer::direct_renderer_draw_debug_window() {
  GetDirectRenderer().draw_debug_window();
}

void SkyVulkanRenderer::direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                                      ScopedProfilerNode& prof) {
  GetDirectRenderer().set_current_index(m_direct_renderer_call_count++);
  GetDirectRenderer().flush_pending(render_state, prof);
}

void SkyVulkanRenderer::direct_renderer_render_gif(const u8* data,
                                                   u32 size,
                                                   BaseSharedRenderState* render_state,
                                                   ScopedProfilerNode& prof) {
  GetDirectRenderer().set_current_index(m_direct_renderer_call_count++);
  GetDirectRenderer().render_gif(data, size, render_state, prof);
}

void SkyVulkanRenderer::direct_renderer_render_vif(u32 vif0,
                                                   u32 vif1,
                                                   const u8* data,
                                                   u32 size,
                                                   BaseSharedRenderState* render_state,
                                                   ScopedProfilerNode& prof) {
  GetDirectRenderer().set_current_index(m_direct_renderer_call_count++);
  GetDirectRenderer().render_vif(vif0, vif1, data, size, render_state, prof);
}
