#include "VulkanRenderer.h"

#include "common/log/log.h"
#include "common/util/FileUtil.h"

#include "game/graphics/vulkan_renderer/DepthCue.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/EyeRenderer.h"
#include "game/graphics/vulkan_renderer/ShadowRenderer.h"
#include "game/graphics/vulkan_renderer/SkyRenderer.h"
#include "game/graphics/vulkan_renderer/Sprite3.h"
#include "game/graphics/vulkan_renderer/SpriteRenderer.h"
#include "game/graphics/vulkan_renderer/TextureUploadHandler.h"
#include "game/graphics/vulkan_renderer/background/Shrub.h"
#include "game/graphics/vulkan_renderer/background/TFragment.h"
#include "game/graphics/vulkan_renderer/background/Tie3.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"
#include "game/graphics/vulkan_renderer/foreground/Merc2.h"
#include "game/graphics/vulkan_renderer/ocean/OceanMidAndFar.h"
#include "game/graphics/vulkan_renderer/ocean/OceanNear.h"
#include "game/graphics/pipelines/vulkan_pipeline.h"

#include "third-party/imgui/imgui.h"

// for the vif callback
#include "game/kernel/common/kmachine.h"
#include "game/runtime.h"

namespace {
std::string g_current_render;

}

VulkanRenderer::~VulkanRenderer() {
}

VulkanRenderer::VulkanRenderer(std::shared_ptr<TexturePool> texture_pool,
                               std::shared_ptr<Loader> loader,
                               std::unique_ptr<GraphicsDeviceVulkan>& device)
    : m_render_state(texture_pool, loader, device), m_device(device) {
  createCommandBuffers();

  m_extents = {640, 480};
  recreateSwapChain();

  //May be overkill for descriptor pool
  std::vector<VkDescriptorPoolSize> poolSizes = {
    {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100}
  };

  uint32_t maxSets = 0;
  for (auto& poolSize : poolSizes) {
    maxSets += poolSize.descriptorCount;
  }
  m_vulkan_info.descriptor_pool = std::make_unique<DescriptorPool>(m_device, maxSets, 0, poolSizes);

  // initialize all renderers
  init_bucket_renderers();
}

/*!
 * Construct bucket renderers.  We can specify different renderers for different buckets
 */
void VulkanRenderer::init_bucket_renderers() {
  m_bucket_categories.fill(BucketCategory::OTHER);
  std::vector<tfrag3::TFragmentTreeKind> normal_tfrags = {tfrag3::TFragmentTreeKind::NORMAL,
                                                          tfrag3::TFragmentTreeKind::LOWRES};
  std::vector<tfrag3::TFragmentTreeKind> dirt_tfrags = {tfrag3::TFragmentTreeKind::DIRT};
  std::vector<tfrag3::TFragmentTreeKind> ice_tfrags = {tfrag3::TFragmentTreeKind::ICE};
  auto sky_gpu_blender = std::make_shared<SkyBlendGPU>(m_device);
  auto sky_cpu_blender = std::make_shared<SkyBlendCPU>(m_device);

  //-------------
  // PRE TEXTURE
  //-------------
  // 0 : ??
  // 1 : ??
  // 2 : ??
  // 3 : SKY_DRAW
  init_bucket_renderer<SkyRenderer>("sky", BucketCategory::OTHER, BucketId::SKY_DRAW, m_device, m_vulkan_info);
  // 4 : OCEAN_MID_AND_FAR
  init_bucket_renderer<OceanMidAndFar>("ocean-mid-far", BucketCategory::OCEAN,
                                       BucketId::OCEAN_MID_AND_FAR, m_device, m_vulkan_info);

  //-----------------------
  // LEVEL 0 tfrag texture
  //-----------------------
  // 5 : TFRAG_TEX_LEVEL0
  init_bucket_renderer<TextureUploadHandler>("l0-tfrag-tex", BucketCategory::TEX,
                                             BucketId::TFRAG_TEX_LEVEL0, m_device, m_vulkan_info);
  // 6 : TFRAG_LEVEL0
  init_bucket_renderer<TFragment>("l0-tfrag-tfrag", BucketCategory::TFRAG, BucketId::TFRAG_LEVEL0, m_device, m_vulkan_info,
                                  normal_tfrags, false, 0);
  // 7 : TFRAG_NEAR_LEVEL0
  // 8 : TIE_NEAR_LEVEL0
  // 9 : TIE_LEVEL0
  init_bucket_renderer<Tie3>("l0-tfrag-tie", BucketCategory::TIE, BucketId::TIE_LEVEL0, m_device, m_vulkan_info, 0);
  // 10 : MERC_TFRAG_TEX_LEVEL0
  init_bucket_renderer<Merc2>("l0-tfrag-merc", BucketCategory::MERC,
                              BucketId::MERC_TFRAG_TEX_LEVEL0, m_device, m_vulkan_info);
  // 11 : GMERC_TFRAG_TEX_LEVEL0
  init_bucket_renderer<Generic2>("l0-tfrag-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_TFRAG_TEX_LEVEL0, m_device, m_vulkan_info, 1500000, 10000, 10000, 800);

  //-----------------------
  // LEVEL 1 tfrag texture
  //-----------------------
  // 12 : TFRAG_TEX_LEVEL1
  init_bucket_renderer<TextureUploadHandler>("l1-tfrag-tex", BucketCategory::TEX,
                                             BucketId::TFRAG_TEX_LEVEL1, m_device, m_vulkan_info);
  // 13 : TFRAG_LEVEL1
  init_bucket_renderer<TFragment>("l1-tfrag-tfrag", BucketCategory::TFRAG, BucketId::TFRAG_LEVEL1, m_device, m_vulkan_info,
                                  normal_tfrags, false, 1);
  // 14 : TFRAG_NEAR_LEVEL1
  // 15 : TIE_NEAR_LEVEL1
  // 16 : TIE_LEVEL1
  init_bucket_renderer<Tie3>("l1-tfrag-tie", BucketCategory::TIE, BucketId::TIE_LEVEL1, m_device, m_vulkan_info, 1);
  // 17 : MERC_TFRAG_TEX_LEVEL1
  init_bucket_renderer<Merc2>("l1-tfrag-merc", BucketCategory::MERC,
                              BucketId::MERC_TFRAG_TEX_LEVEL1, m_device, m_vulkan_info);
  // 18 : GMERC_TFRAG_TEX_LEVEL1
  init_bucket_renderer<Generic2>("l1-tfrag-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_TFRAG_TEX_LEVEL1, m_device, m_vulkan_info, 1500000, 10000, 10000, 800);

  //-----------------------
  // LEVEL 0 shrub texture
  //-----------------------
  // 19 : SHRUB_TEX_LEVEL0
  init_bucket_renderer<TextureUploadHandler>("l0-shrub-tex", BucketCategory::TEX,
                                             BucketId::SHRUB_TEX_LEVEL0, m_device, m_vulkan_info);
  // 20 : SHRUB_NORMAL_LEVEL0
  init_bucket_renderer<Shrub>("l0-shrub", BucketCategory::SHRUB, BucketId::SHRUB_NORMAL_LEVEL0, m_device, m_vulkan_info);
  // 21 : ???
  // 22 : SHRUB_BILLBOARD_LEVEL0
  // 23 : SHRUB_TRANS_LEVEL0
  // 24 : SHRUB_GENERIC_LEVEL0

  //-----------------------
  // LEVEL 1 shrub texture
  //-----------------------
  // 25 : SHRUB_TEX_LEVEL1
  init_bucket_renderer<TextureUploadHandler>("l1-shrub-tex", BucketCategory::TEX,
                                             BucketId::SHRUB_TEX_LEVEL1, m_device, m_vulkan_info);
  // 26 : SHRUB_NORMAL_LEVEL1
  init_bucket_renderer<Shrub>("l1-shrub", BucketCategory::SHRUB, BucketId::SHRUB_NORMAL_LEVEL1, m_device, m_vulkan_info);
  // 27 : ???
  // 28 : SHRUB_BILLBOARD_LEVEL1
  // 29 : SHRUB_TRANS_LEVEL1
  // 30 : SHRUB_GENERIC_LEVEL1
  init_bucket_renderer<Generic2>("mystery-generic", BucketCategory::GENERIC,
                                 BucketId::SHRUB_GENERIC_LEVEL1, m_device, m_vulkan_info);

  //-----------------------
  // LEVEL 0 alpha texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l0-alpha-tex", BucketCategory::TEX,
                                             BucketId::ALPHA_TEX_LEVEL0, m_device, m_vulkan_info);  // 31
  init_bucket_renderer<SkyBlendHandler>("l0-alpha-sky-blend-and-tfrag-trans", BucketCategory::OTHER,
                                        BucketId::TFRAG_TRANS0_AND_SKY_BLEND_LEVEL0, m_device, m_vulkan_info, 0,
                                        sky_gpu_blender, sky_cpu_blender);  // 32
  // 33
  init_bucket_renderer<TFragment>("l0-alpha-tfrag", BucketCategory::TFRAG,
                                  BucketId::TFRAG_DIRT_LEVEL0, m_device, m_vulkan_info, dirt_tfrags, false,
                                  0);  // 34
  // 35
  init_bucket_renderer<TFragment>("l0-alpha-tfrag-ice", BucketCategory::TFRAG,
                                  BucketId::TFRAG_ICE_LEVEL0, m_device, m_vulkan_info, ice_tfrags, false, 0);
  // 37

  //-----------------------
  // LEVEL 1 alpha texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l1-alpha-tex", BucketCategory::TEX,
                                             BucketId::ALPHA_TEX_LEVEL1, m_device, m_vulkan_info);  // 38
  init_bucket_renderer<SkyBlendHandler>("l1-alpha-sky-blend-and-tfrag-trans", BucketCategory::OTHER,
                                        BucketId::TFRAG_TRANS1_AND_SKY_BLEND_LEVEL1, m_device, m_vulkan_info, 1,
                                        sky_gpu_blender, sky_cpu_blender);  // 39
  // 40
  init_bucket_renderer<TFragment>("l1-alpha-tfrag-dirt", BucketCategory::TFRAG,
                                  BucketId::TFRAG_DIRT_LEVEL1, m_device, m_vulkan_info, dirt_tfrags, false,
                                  1);  // 41
  // 42
  init_bucket_renderer<TFragment>("l1-alpha-tfrag-ice", BucketCategory::TFRAG,
                                  BucketId::TFRAG_ICE_LEVEL1, m_device, m_vulkan_info, ice_tfrags, false, 1);
  // 44

  init_bucket_renderer<Merc2>("common-alpha-merc", BucketCategory::MERC,
                              BucketId::MERC_AFTER_ALPHA, m_device, m_vulkan_info);

  init_bucket_renderer<Generic2>("common-alpha-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_ALPHA, m_device, m_vulkan_info);                                  // 46
  init_bucket_renderer<ShadowRenderer>("shadow", BucketCategory::OTHER, BucketId::SHADOW, m_device, m_vulkan_info);  // 47

  //-----------------------
  // LEVEL 0 pris texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l0-pris-tex", BucketCategory::TEX,
                                             BucketId::PRIS_TEX_LEVEL0, m_device, m_vulkan_info);  // 48
  init_bucket_renderer<Merc2>("l0-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_PRIS_LEVEL0, m_device, m_vulkan_info);  // 49
  init_bucket_renderer<Generic2>("l0-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS_LEVEL0, m_device, m_vulkan_info);  // 50

  //-----------------------
  // LEVEL 1 pris texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l1-pris-tex", BucketCategory::TEX,
                                             BucketId::PRIS_TEX_LEVEL1, m_device, m_vulkan_info);  // 51
  init_bucket_renderer<Merc2>("l1-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_PRIS_LEVEL1, m_device, m_vulkan_info);  // 52
  init_bucket_renderer<Generic2>("l1-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS_LEVEL1, m_device, m_vulkan_info);  // 53

  // other renderers may output to the eye renderer
  m_render_state.eye_renderer = init_bucket_renderer<EyeRenderer>(
      "common-pris-eyes", BucketCategory::OTHER, BucketId::MERC_EYES_AFTER_PRIS, m_device, m_vulkan_info);  // 54

  // hack: set to merc2 for debugging
  init_bucket_renderer<Merc2>("common-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_AFTER_PRIS, m_device, m_vulkan_info);  // 55
  init_bucket_renderer<Generic2>("common-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS, m_device, m_vulkan_info);  // 56

  //-----------------------
  // LEVEL 0 water texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l0-water-tex", BucketCategory::TEX,
                                             BucketId::WATER_TEX_LEVEL0, m_device, m_vulkan_info);  // 57
  init_bucket_renderer<Merc2>("l0-water-merc", BucketCategory::MERC,
                              BucketId::MERC_WATER_LEVEL0, m_device, m_vulkan_info);  // 58
  init_bucket_renderer<Generic2>("l0-water-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_WATER_LEVEL0, m_device, m_vulkan_info);  // 59

  //-----------------------
  // LEVEL 1 water texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l1-water-tex", BucketCategory::TEX,
                                             BucketId::WATER_TEX_LEVEL1, m_device, m_vulkan_info);  // 60
  init_bucket_renderer<Merc2>("l1-water-merc", BucketCategory::MERC,
                              BucketId::MERC_WATER_LEVEL1, m_device, m_vulkan_info);  // 61
  init_bucket_renderer<Generic2>("l1-water-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_WATER_LEVEL1, m_device, m_vulkan_info);  // 62

  init_bucket_renderer<OceanNear>("ocean-near", BucketCategory::OCEAN, BucketId::OCEAN_NEAR, m_device, m_vulkan_info);  // 63

  //-----------------------
  // DEPTH CUE
  //-----------------------
  init_bucket_renderer<DepthCue>("depth-cue", BucketCategory::OTHER, BucketId::DEPTH_CUE, m_device, m_vulkan_info);  // 64

  //-----------------------
  // COMMON texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("common-tex", BucketCategory::TEX,
                                             BucketId::PRE_SPRITE_TEX, m_device, m_vulkan_info);  // 65

  std::vector<std::unique_ptr<BucketRenderer>> sprite_renderers;
  // the first renderer added will be the default for sprite.
  sprite_renderers.push_back(std::make_unique<Sprite3>("sprite-3", BucketId::SPRITE, m_device, m_vulkan_info));
  sprite_renderers.push_back(std::make_unique<SpriteRenderer>("sprite-renderer", BucketId::SPRITE, m_device, m_vulkan_info));
  init_bucket_renderer<RenderMux>("sprite", BucketCategory::SPRITE, BucketId::SPRITE, m_device, m_vulkan_info,
                                  std::move(sprite_renderers));  // 66

  init_bucket_renderer<DirectRenderer>("debug", BucketCategory::OTHER, BucketId::DEBUG, m_device, m_vulkan_info, 0x20000);
  init_bucket_renderer<DirectRenderer>("debug-no-zbuf", BucketCategory::OTHER,
                                       BucketId::DEBUG_NO_ZBUF, m_device, m_vulkan_info, 0x8000);
  // an extra custom bucket!
  init_bucket_renderer<DirectRenderer>("subtitle", BucketCategory::OTHER, BucketId::SUBTITLE, m_device, m_vulkan_info, 6000);

  // for now, for any unset renderers, just set them to an EmptyBucketRenderer.
  for (size_t i = 0; i < m_bucket_renderers.size(); i++) {
    if (!m_bucket_renderers[i]) {
      init_bucket_renderer<EmptyBucketRenderer>(fmt::format("bucket{}", i), BucketCategory::OTHER,
                                                (BucketId)i, m_device, m_vulkan_info);
    }

    m_bucket_renderers[i]->init_shaders(m_render_state.shaders);
    m_bucket_renderers[i]->init_textures(*m_render_state.texture_pool);
  }
  sky_cpu_blender->init_textures(*m_render_state.texture_pool);
  sky_gpu_blender->init_textures(*m_render_state.texture_pool);
  m_render_state.loader->load_common(*m_render_state.texture_pool, "GAME");
}

/*!
 * Main render function. This is called from the gfx loop with the chain passed from the game.
 */
void VulkanRenderer::render(DmaFollower dma, const RenderOptions& settings) {
  m_profiler.clear();
  m_render_state.reset();
  m_render_state.ee_main_memory = g_ee_main_mem;
  m_render_state.offset_of_s7 = offset_of_s7();

  {
    auto prof = m_profiler.root()->make_scoped_child("frame-setup");
    setup_frame(settings);
    if (settings.gpu_sync) {
      //glFinish();
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }
  }

  {
    auto prof = m_profiler.root()->make_scoped_child("loader");
    if (m_last_pmode_alp == 0 && settings.pmode_alp_register != 0 && m_enable_fast_blackout_loads) {
      // blackout, load everything and don't worry about frame rate
      m_render_state.loader->update_blocking(*m_render_state.texture_pool);

    } else {
      m_render_state.loader->update(*m_render_state.texture_pool);
    }
  }

  // render the buckets!
  {
    auto prof = m_profiler.root()->make_scoped_child("buckets");
    dispatch_buckets(dma, prof, settings.gpu_sync);
  }

  // apply effects done with PCRTC registers
  {
    auto prof = m_profiler.root()->make_scoped_child("pcrtc");
    do_pcrtc_effects(settings.pmode_alp_register, &m_render_state, prof);
    if (settings.gpu_sync) {
      //glFinish();
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }
  }

  if (settings.draw_render_debug_window) {
    auto prof = m_profiler.root()->make_scoped_child("render-window");
    draw_renderer_selection_window();
    // add a profile bar for the imgui stuff
    //vif_interrupt_callback(0);
    if (settings.gpu_sync) {
      vkQueueWaitIdle(m_device->graphicsQueue()); //TODO: Verify that this is correct
    }
  }

  m_last_pmode_alp = settings.pmode_alp_register;

  m_profiler.finish();
  if (settings.draw_profiler_window) {
    m_profiler.draw();
  }

  //  if (m_profiler.root_time() > 0.018) {
  //    fmt::print("Slow frame: {:.2f} ms\n", m_profiler.root_time() * 1000);
  //    fmt::print("{}\n", m_profiler.to_string());
  //  }

  if (settings.draw_small_profiler_window) {
    SmallProfilerStats stats;
    stats.draw_calls = m_profiler.root()->stats().draw_calls;
    stats.triangles = m_profiler.root()->stats().triangles;
    for (int i = 0; i < (int)BucketCategory::MAX_CATEGORIES; i++) {
      stats.time_per_category[i] = m_category_times[i];
    }
    m_small_profiler.draw(m_render_state.load_status_debug, stats);
  }

  if (settings.draw_subtitle_editor_window) {
    m_subtitle_editor.draw_window();
  }

  if (settings.save_screenshot) {
    finish_screenshot(settings.screenshot_path, settings.game_res_w, settings.game_res_h, 0, 0);
  }
  if (settings.gpu_sync) {
    vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
  }
}

/*!
 * Draw the per-renderer debug window
 */
void VulkanRenderer::draw_renderer_selection_window() {
  ImGui::Begin("Renderer Debug");

  ImGui::Checkbox("Use old single-draw", &m_render_state.no_multidraw);
  ImGui::SliderFloat("Fog Adjust", &m_render_state.fog_intensity, 0, 10);
  ImGui::Checkbox("Sky CPU", &m_render_state.use_sky_cpu);
  ImGui::Checkbox("Occlusion Cull", &m_render_state.use_occlusion_culling);
  ImGui::Checkbox("Merc XGKICK", &m_render_state.enable_merc_xgkick);
  ImGui::Checkbox("Blackout Loads", &m_enable_fast_blackout_loads);

  for (size_t i = 0; i < m_bucket_renderers.size(); i++) {
    auto renderer = m_bucket_renderers[i].get();
    if (renderer && !renderer->empty()) {
      ImGui::PushID(i);
      if (ImGui::TreeNode(renderer->name_and_id().c_str())) {
        ImGui::Checkbox("Enable", &renderer->enabled());
        renderer->draw_debug_window();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
  }
  if (ImGui::TreeNode("Texture Pool")) {
    m_render_state.texture_pool->draw_debug_window();
    ImGui::TreePop();
  }
  ImGui::End();
}

/*!
 * Pre-render frame setup.
 */
void VulkanRenderer::setup_frame(const RenderOptions& settings) {
  // glfw controls the window framebuffer, so we just update the size:
  bool window_changed = m_swap_chain->width() != settings.window_framebuffer_width ||
                        m_swap_chain->height() != settings.window_framebuffer_height ||
                        m_device->getMsaaCount() != settings.msaa_samples;
  bool isValidWindow = true;

  if (window_changed) {
    m_extents.height = settings.window_framebuffer_height;
    m_extents.width = settings.window_framebuffer_width;
    recreateSwapChain();
  }

  ASSERT_MSG(settings.game_res_w > 0 && settings.game_res_h > 0,
             fmt::format("Bad viewport size from game_res: {}x{}\n", settings.game_res_w,
                         settings.game_res_h));

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthWriteEnable = VK_TRUE;
  
  // Note: could rely on sky renderer to clear depth and color, but this causes problems with
  // letterboxing

  // setup the draw region to letterbox later
  m_render_state.draw_region_w = settings.draw_region_width;
  m_render_state.draw_region_h = settings.draw_region_height;

  // center the letterbox
  m_render_state.draw_offset_x =
      (settings.window_framebuffer_width - m_render_state.draw_region_w) / 2;
  m_render_state.draw_offset_y =
      (settings.window_framebuffer_height - m_render_state.draw_region_h) / 2;

  if (settings.borderless_windows_hacks) {
    // pretend the framebuffer is 1 pixel shorter on borderless. fullscreen issues!
    // add one pixel of vertical letterbox on borderless to make up for extra line
    m_render_state.draw_offset_y++;
  }

  if (m_render_state.draw_region_w <= 0 || m_render_state.draw_region_h <= 0) {
    // trying to draw to 0 size region... opengl doesn't like this.
    m_render_state.draw_region_w = 640;
    m_render_state.draw_region_h = 480;
  }

  if (isValidWindow) {
    m_render_state.render_fb_x = m_render_state.draw_offset_x;
    m_render_state.render_fb_y = m_render_state.draw_offset_y;
    m_render_state.render_fb_w = m_render_state.draw_region_w;
    m_render_state.render_fb_h = m_render_state.draw_region_h;
    m_swap_chain->setSwapChainOffsetExtent(
        {m_render_state.draw_offset_x, -1 * m_render_state.draw_offset_y}); //Y-axis is inverse in Vulkan compared to OpenGL
    m_swap_chain->setSwapChainOffsetExtent(
        {m_render_state.draw_region_w, m_render_state.draw_region_h});
  } else {
    m_render_state.render_fb_x = 0;
    m_render_state.render_fb_y = 0;
    m_render_state.render_fb_w = settings.game_res_w;
    m_render_state.render_fb_h = settings.game_res_h;
    m_swap_chain->setSwapChainOffsetExtent({0, 0});
    m_swap_chain->setSwapChainOffsetExtent({settings.game_res_w, settings.game_res_h});
  }
}

/*!
 * This function finds buckets and dispatches them to the appropriate part.
 */
void VulkanRenderer::dispatch_buckets(DmaFollower dma,
                                      ScopedProfilerNode& prof,
                                      bool sync_after_buckets) {
  // The first thing the DMA chain should be a call to a common default-registers chain.
  // this chain resets the state of the GS. After this is buckets
  m_category_times.fill(0);

  m_render_state.buckets_base =
      dma.current_tag_offset() + 16;  // offset by 1 qw for the initial call
  m_render_state.next_bucket = m_render_state.buckets_base;

  // Find the default regs buffer
  auto initial_call_tag = dma.current_tag();
  ASSERT(initial_call_tag.kind == DmaTag::Kind::CALL);
  auto initial_call_default_regs = dma.read_and_advance();
  ASSERT(initial_call_default_regs.transferred_tag == 0);  // should be a nop.
  m_render_state.default_regs_buffer = dma.current_tag_offset();
  auto default_regs_tag = dma.current_tag();
  ASSERT(default_regs_tag.kind == DmaTag::Kind::CNT);
  ASSERT(default_regs_tag.qwc == 10);
  // TODO verify data in here.
  auto default_data = dma.read_and_advance();
  ASSERT(default_data.size_bytes > 148);
  memcpy(m_render_state.fog_color.data(), default_data.data + 144, 4);
  auto default_ret_tag = dma.current_tag();
  ASSERT(default_ret_tag.qwc == 0);
  ASSERT(default_ret_tag.kind == DmaTag::Kind::RET);
  dma.read_and_advance();

  // now we should point to the first bucket!
  ASSERT(dma.current_tag_offset() == m_render_state.next_bucket);
  m_render_state.next_bucket += 16;

  // loop over the buckets!
  for (int bucket_id = 0; bucket_id < (int)BucketId::MAX_BUCKETS; bucket_id++) {
    auto& renderer = m_bucket_renderers[bucket_id];
    auto bucket_prof = prof.make_scoped_child(renderer->name_and_id());
    g_current_render = renderer->name_and_id();
    // lg::info("Render: {} start", g_current_render);
    renderer->render(dma, &m_render_state, bucket_prof);
    if (sync_after_buckets) {
      auto pp = scoped_prof("finish");
      //glFinish();
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }

    // lg::info("Render: {} end", g_current_render);
    //  should have ended at the start of the next chain
    ASSERT(dma.current_tag_offset() == m_render_state.next_bucket);
    m_render_state.next_bucket += 16;
    vif_interrupt_callback(bucket_id);
    m_category_times[(int)m_bucket_categories[bucket_id]] += bucket_prof.get_elapsed_time();

    // hack to draw the collision mesh in the middle the drawing
    if (bucket_id == (int)BucketId::ALPHA_TEX_LEVEL0 - 1 &&
        Gfx::g_global_settings.collision_enable) {
      auto p = prof.make_scoped_child("collision-draw");
      m_collide_renderer.render(&m_render_state, p);
    }
  }
  g_current_render = "";

  // TODO ending data.
}

void VulkanRenderer::finish_screenshot(const std::string& output_name,
                                       int width,
                                       int height,
                                       int x,
                                       int y) {
  VkDeviceSize device_memory_size = sizeof(u32) * width * height;
  std::vector<u32> buffer(width * height);

  VkImage srcImage = m_swap_chain->getImage(currentImageIndex);

  Buffer screenshotBuffer(m_device, device_memory_size, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  if (screenshotBuffer.map() != VK_SUCCESS) {
    lg::error("Error can't get screenshot memory buffer");
  }

  m_device->copyBufferToImage(screenshotBuffer.getBuffer(), srcImage, static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height), x, y, 1);

  VkDeviceSize memory_offset = sizeof(u32) * ((y * width) + x);

  void* data = screenshotBuffer.getMappedMemory();
  ::memcpy(buffer.data(), data, device_memory_size - memory_offset);
  screenshotBuffer.unmap();

  // flip upside down in place
  for (int h = 0; h < height / 2; h++) {
    for (int w = 0; w < width; w++) {
      std::swap(buffer[h * width + w], buffer[(height - h - 1) * width + w]);
    }
  }

  // set alpha. For some reason, image viewers do weird stuff with alpha.
  for (auto& px : buffer) {
    px |= 0xff000000;
  }
  file_util::write_rgba_png(output_name, buffer.data(), width, height);
}

void VulkanRenderer::do_pcrtc_effects(float alp,
                                      SharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;


  if (alp < 1) {
    depthStencil.depthTestEnable = VK_FALSE;
    colorBlendAttachment.blendEnable = VK_TRUE;

    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_swap_chain->setSwapChainOffsetExtent({0, 0});

    //m_blackout_renderer.draw(Vector4f(0, 0, 0, 1.f - alp), render_state, prof);

    depthStencil.depthTestEnable = VK_TRUE;
  }
}

void VulkanRenderer::createCommandBuffers() {
  commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_device->getCommandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

  if (vkAllocateCommandBuffers(m_device->getLogicalDevice(), &allocInfo, commandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void VulkanRenderer::recreateSwapChain() {
  while (m_extents.width == 0 || m_extents.height == 0) {
    glfwWaitEvents();
  }
  vkDeviceWaitIdle(m_device->getLogicalDevice());

  if (m_swap_chain == nullptr) {
    m_swap_chain = std::make_unique<SwapChain>(m_device, m_extents);
  } else {
    std::shared_ptr<SwapChain> oldSwapChain = std::move(m_swap_chain);
    m_swap_chain = std::make_unique<SwapChain>(m_device, m_extents, oldSwapChain);

    if (!oldSwapChain->compareSwapFormats(*m_swap_chain.get())) {
      throw std::runtime_error("Swap chain image(or depth) format has changed!");
    }
  }
}

VkCommandBuffer VulkanRenderer::beginFrame() {
  assert(!isFrameStarted && "Can't call beginFrame while already in progress");

  auto result = m_swap_chain->acquireNextImage(&currentImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return nullptr;
  }

  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  isFrameStarted = true;

  auto commandBuffer = getCurrentCommandBuffer();
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }
  return commandBuffer;
}

void VulkanRenderer::endFrame() {
  assert(isFrameStarted && "Can't call endFrame while frame is not in progress");
  auto commandBuffer = getCurrentCommandBuffer();
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }

  if(m_swap_chain->submitCommandBuffers(&commandBuffer, &currentImageIndex) != VK_SUCCESS){
    throw std::runtime_error("failed to present swap chain image!");
  }

  isFrameStarted = false;
  currentFrame = (currentFrame + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::freeCommandBuffers() {
  vkFreeCommandBuffers(m_device->getLogicalDevice(), m_device->getCommandPool(),
                       static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
  commandBuffers.clear();
}
