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
#include "game/graphics/pipelines/vulkan.h"

#include "third-party/imgui/imgui.h"

// for the vif callback
#include "game/kernel/common/kmachine.h"
#include "game/runtime.h"

namespace {
std::string g_current_render;

}

const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};


VulkanRenderer::~VulkanRenderer() {
  cleanup();
}

VulkanRenderer::VulkanRenderer(std::shared_ptr<TexturePool> texture_pool,
                               std::shared_ptr<Loader> loader)
    : m_render_state(texture_pool, loader) {

  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  m_render_state.ResetShaderLibrary(device);
  createSwapChain();
  createImageViews();
  createRenderPass();
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createCommandPool();
  createColorResources();
  createDepthResources();
  createFramebuffers();
  createTextureImage();
  createTextureImageView();
  createTextureSampler();
  loadModel();
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createDescriptorPool();
  createDescriptorSets();
  createCommandBuffers();
  createSyncObjects();


  //glDebugMessageCallback(opengl_error_callback, nullptr);
  // disable specific errors
  //const GLuint gl_error_ignores_api_other[1] = {0x20071};
  //glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1,
  //                      &gl_error_ignores_api_other[0], GL_FALSE);

  //lg::debug("OpenGL context information: {}", (const char*)glGetString(GL_VERSION));

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
  auto sky_gpu_blender = std::make_shared<SkyBlendGPU>();
  auto sky_cpu_blender = std::make_shared<SkyBlendCPU>();

  //-------------
  // PRE TEXTURE
  //-------------
  // 0 : ??
  // 1 : ??
  // 2 : ??
  // 3 : SKY_DRAW
  init_bucket_renderer<SkyRenderer>("sky", BucketCategory::OTHER, BucketId::SKY_DRAW);
  // 4 : OCEAN_MID_AND_FAR
  init_bucket_renderer<OceanMidAndFar>("ocean-mid-far", BucketCategory::OCEAN,
                                       BucketId::OCEAN_MID_AND_FAR);

  //-----------------------
  // LEVEL 0 tfrag texture
  //-----------------------
  // 5 : TFRAG_TEX_LEVEL0
  init_bucket_renderer<TextureUploadHandler>("l0-tfrag-tex", BucketCategory::TEX,
                                             BucketId::TFRAG_TEX_LEVEL0);
  // 6 : TFRAG_LEVEL0
  init_bucket_renderer<TFragment>("l0-tfrag-tfrag", BucketCategory::TFRAG, BucketId::TFRAG_LEVEL0,
                                  normal_tfrags, false, 0);
  // 7 : TFRAG_NEAR_LEVEL0
  // 8 : TIE_NEAR_LEVEL0
  // 9 : TIE_LEVEL0
  init_bucket_renderer<Tie3>("l0-tfrag-tie", BucketCategory::TIE, BucketId::TIE_LEVEL0, 0);
  // 10 : MERC_TFRAG_TEX_LEVEL0
  init_bucket_renderer<Merc2>("l0-tfrag-merc", BucketCategory::MERC,
                              BucketId::MERC_TFRAG_TEX_LEVEL0);
  // 11 : GMERC_TFRAG_TEX_LEVEL0
  init_bucket_renderer<Generic2>("l0-tfrag-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_TFRAG_TEX_LEVEL0, 1500000, 10000, 10000, 800);

  //-----------------------
  // LEVEL 1 tfrag texture
  //-----------------------
  // 12 : TFRAG_TEX_LEVEL1
  init_bucket_renderer<TextureUploadHandler>("l1-tfrag-tex", BucketCategory::TEX,
                                             BucketId::TFRAG_TEX_LEVEL1);
  // 13 : TFRAG_LEVEL1
  init_bucket_renderer<TFragment>("l1-tfrag-tfrag", BucketCategory::TFRAG, BucketId::TFRAG_LEVEL1,
                                  normal_tfrags, false, 1);
  // 14 : TFRAG_NEAR_LEVEL1
  // 15 : TIE_NEAR_LEVEL1
  // 16 : TIE_LEVEL1
  init_bucket_renderer<Tie3>("l1-tfrag-tie", BucketCategory::TIE, BucketId::TIE_LEVEL1, 1);
  // 17 : MERC_TFRAG_TEX_LEVEL1
  init_bucket_renderer<Merc2>("l1-tfrag-merc", BucketCategory::MERC,
                              BucketId::MERC_TFRAG_TEX_LEVEL1);
  // 18 : GMERC_TFRAG_TEX_LEVEL1
  init_bucket_renderer<Generic2>("l1-tfrag-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_TFRAG_TEX_LEVEL1, 1500000, 10000, 10000, 800);

  //-----------------------
  // LEVEL 0 shrub texture
  //-----------------------
  // 19 : SHRUB_TEX_LEVEL0
  init_bucket_renderer<TextureUploadHandler>("l0-shrub-tex", BucketCategory::TEX,
                                             BucketId::SHRUB_TEX_LEVEL0);
  // 20 : SHRUB_NORMAL_LEVEL0
  init_bucket_renderer<Shrub>("l0-shrub", BucketCategory::SHRUB, BucketId::SHRUB_NORMAL_LEVEL0);
  // 21 : ???
  // 22 : SHRUB_BILLBOARD_LEVEL0
  // 23 : SHRUB_TRANS_LEVEL0
  // 24 : SHRUB_GENERIC_LEVEL0

  //-----------------------
  // LEVEL 1 shrub texture
  //-----------------------
  // 25 : SHRUB_TEX_LEVEL1
  init_bucket_renderer<TextureUploadHandler>("l1-shrub-tex", BucketCategory::TEX,
                                             BucketId::SHRUB_TEX_LEVEL1);
  // 26 : SHRUB_NORMAL_LEVEL1
  init_bucket_renderer<Shrub>("l1-shrub", BucketCategory::SHRUB, BucketId::SHRUB_NORMAL_LEVEL1);
  // 27 : ???
  // 28 : SHRUB_BILLBOARD_LEVEL1
  // 29 : SHRUB_TRANS_LEVEL1
  // 30 : SHRUB_GENERIC_LEVEL1
  init_bucket_renderer<Generic2>("mystery-generic", BucketCategory::GENERIC,
                                 BucketId::SHRUB_GENERIC_LEVEL1);

  //-----------------------
  // LEVEL 0 alpha texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l0-alpha-tex", BucketCategory::TEX,
                                             BucketId::ALPHA_TEX_LEVEL0);  // 31
  init_bucket_renderer<SkyBlendHandler>("l0-alpha-sky-blend-and-tfrag-trans", BucketCategory::OTHER,
                                        BucketId::TFRAG_TRANS0_AND_SKY_BLEND_LEVEL0, 0,
                                        sky_gpu_blender, sky_cpu_blender);  // 32
  // 33
  init_bucket_renderer<TFragment>("l0-alpha-tfrag", BucketCategory::TFRAG,
                                  BucketId::TFRAG_DIRT_LEVEL0, dirt_tfrags, false,
                                  0);  // 34
  // 35
  init_bucket_renderer<TFragment>("l0-alpha-tfrag-ice", BucketCategory::TFRAG,
                                  BucketId::TFRAG_ICE_LEVEL0, ice_tfrags, false, 0);
  // 37

  //-----------------------
  // LEVEL 1 alpha texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l1-alpha-tex", BucketCategory::TEX,
                                             BucketId::ALPHA_TEX_LEVEL1);  // 38
  init_bucket_renderer<SkyBlendHandler>("l1-alpha-sky-blend-and-tfrag-trans", BucketCategory::OTHER,
                                        BucketId::TFRAG_TRANS1_AND_SKY_BLEND_LEVEL1, 1,
                                        sky_gpu_blender, sky_cpu_blender);  // 39
  // 40
  init_bucket_renderer<TFragment>("l1-alpha-tfrag-dirt", BucketCategory::TFRAG,
                                  BucketId::TFRAG_DIRT_LEVEL1, dirt_tfrags, false,
                                  1);  // 41
  // 42
  init_bucket_renderer<TFragment>("l1-alpha-tfrag-ice", BucketCategory::TFRAG,
                                  BucketId::TFRAG_ICE_LEVEL1, ice_tfrags, false, 1);
  // 44

  init_bucket_renderer<Merc2>("common-alpha-merc", BucketCategory::MERC,
                              BucketId::MERC_AFTER_ALPHA);

  init_bucket_renderer<Generic2>("common-alpha-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_ALPHA);                                  // 46
  init_bucket_renderer<ShadowRenderer>("shadow", BucketCategory::OTHER, BucketId::SHADOW);  // 47

  //-----------------------
  // LEVEL 0 pris texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l0-pris-tex", BucketCategory::TEX,
                                             BucketId::PRIS_TEX_LEVEL0);  // 48
  init_bucket_renderer<Merc2>("l0-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_PRIS_LEVEL0);  // 49
  init_bucket_renderer<Generic2>("l0-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS_LEVEL0);  // 50

  //-----------------------
  // LEVEL 1 pris texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l1-pris-tex", BucketCategory::TEX,
                                             BucketId::PRIS_TEX_LEVEL1);  // 51
  init_bucket_renderer<Merc2>("l1-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_PRIS_LEVEL1);  // 52
  init_bucket_renderer<Generic2>("l1-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS_LEVEL1);  // 53

  // other renderers may output to the eye renderer
  m_render_state.eye_renderer = init_bucket_renderer<EyeRenderer>(
      "common-pris-eyes", BucketCategory::OTHER, BucketId::MERC_EYES_AFTER_PRIS);  // 54

  // hack: set to merc2 for debugging
  init_bucket_renderer<Merc2>("common-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_AFTER_PRIS);  // 55
  init_bucket_renderer<Generic2>("common-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS);  // 56

  //-----------------------
  // LEVEL 0 water texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l0-water-tex", BucketCategory::TEX,
                                             BucketId::WATER_TEX_LEVEL0);  // 57
  init_bucket_renderer<Merc2>("l0-water-merc", BucketCategory::MERC,
                              BucketId::MERC_WATER_LEVEL0);  // 58
  init_bucket_renderer<Generic2>("l0-water-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_WATER_LEVEL0);  // 59

  //-----------------------
  // LEVEL 1 water texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("l1-water-tex", BucketCategory::TEX,
                                             BucketId::WATER_TEX_LEVEL1);  // 60
  init_bucket_renderer<Merc2>("l1-water-merc", BucketCategory::MERC,
                              BucketId::MERC_WATER_LEVEL1);  // 61
  init_bucket_renderer<Generic2>("l1-water-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_WATER_LEVEL1);  // 62

  init_bucket_renderer<OceanNear>("ocean-near", BucketCategory::OCEAN, BucketId::OCEAN_NEAR);  // 63

  //-----------------------
  // DEPTH CUE
  //-----------------------
  init_bucket_renderer<DepthCue>("depth-cue", BucketCategory::OTHER, BucketId::DEPTH_CUE);  // 64

  //-----------------------
  // COMMON texture
  //-----------------------
  init_bucket_renderer<TextureUploadHandler>("common-tex", BucketCategory::TEX,
                                             BucketId::PRE_SPRITE_TEX);  // 65

  std::vector<std::unique_ptr<BucketRenderer>> sprite_renderers;
  // the first renderer added will be the default for sprite.
  sprite_renderers.push_back(std::make_unique<Sprite3>("sprite-3", BucketId::SPRITE));
  sprite_renderers.push_back(std::make_unique<SpriteRenderer>("sprite-renderer", BucketId::SPRITE));
  init_bucket_renderer<RenderMux>("sprite", BucketCategory::SPRITE, BucketId::SPRITE,
                                  std::move(sprite_renderers));  // 66

  init_bucket_renderer<DirectRenderer>("debug", BucketCategory::OTHER, BucketId::DEBUG, 0x20000);
  init_bucket_renderer<DirectRenderer>("debug-no-zbuf", BucketCategory::OTHER,
                                       BucketId::DEBUG_NO_ZBUF, 0x8000);
  // an extra custom bucket!
  init_bucket_renderer<DirectRenderer>("subtitle", BucketCategory::OTHER, BucketId::SUBTITLE, 6000);

  // for now, for any unset renderers, just set them to an EmptyBucketRenderer.
  for (size_t i = 0; i < m_bucket_renderers.size(); i++) {
    if (!m_bucket_renderers[i]) {
      init_bucket_renderer<EmptyBucketRenderer>(fmt::format("bucket{}", i), BucketCategory::OTHER,
                                                (BucketId)i);
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
      glFinish();
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
      glFinish();
    }
  }

  if (settings.draw_render_debug_window) {
    auto prof = m_profiler.root()->make_scoped_child("render-window");
    draw_renderer_selection_window();
    // add a profile bar for the imgui stuff
    vif_interrupt_callback();
    if (settings.gpu_sync) {
      glFinish();
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
    glFinish();
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
  auto& window_fb = m_fbo_state.resources.window;
  bool window_changed = window_fb.width != settings.window_framebuffer_width ||
                        window_fb.height != settings.window_framebuffer_height ||
                        window_fb.msaa_samples != settings.msaa_samples;
  window_fb.valid = true;
  window_fb.is_window = true;
  window_fb.fbo_id = 0;
  window_fb.width = settings.window_framebuffer_width;
  window_fb.height = settings.window_framebuffer_height;
  window_fb.multisample_count = 1;
  window_fb.multisampled = false;

  // see if the render FBO is still applicable
  if (window_changed) {
    // doesn't match, set up a new one for these settings
    lg::info("FBO Setup: requested {}x{}, msaa {}", settings.game_res_w, settings.game_res_h,
             settings.msaa_samples);

    // first, see if we can just render straight to the display framebuffer.
    if (window_fb.matches(settings.game_res_w, settings.game_res_h, settings.msaa_samples)) {
      // it matches - no need for extra framebuffers.
      lg::info("FBO Setup: rendering directly to window framebuffer");
      m_fbo_state.render_fbo = &m_fbo_state.resources.window;
    } else {
      lg::info("FBO Setup: window didn't match: {} {}", window_fb.width, window_fb.height);

      // create a fbo to render to, with the desired settings
      m_fbo_state.resources.render_buffer =
          MakeFbo(settings.game_res_w, settings.game_res_h, settings.msaa_samples, true);
      m_fbo_state.render_fbo = &m_fbo_state.resources.render_buffer;
    }
  }

  ASSERT_MSG(settings.game_res_w > 0 && settings.game_res_h > 0,
             fmt::format("Bad viewport size from game_res: {}x{}\n", settings.game_res_w,
                         settings.game_res_h));

  if (!m_fbo_state.render_fbo->is_window) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_fbo_state.resources.window.width, m_fbo_state.resources.window.height);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClearDepth(0.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDisable(GL_BLEND);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_state.render_fbo->fbo_id);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClearDepth(0.0);
  glClearStencil(0);
  glDepthMask(GL_TRUE);
  // Note: could rely on sky renderer to clear depth and color, but this causes problems with
  // letterboxing
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glDisable(GL_BLEND);

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

  m_render_state.render_fb = m_fbo_state.render_fbo->fbo_id;

  if (m_render_state.draw_region_w <= 0 || m_render_state.draw_region_h <= 0) {
    // trying to draw to 0 size region... opengl doesn't like this.
    m_render_state.draw_region_w = 640;
    m_render_state.draw_region_h = 480;
  }

  if (m_fbo_state.render_fbo->is_window) {
    m_render_state.render_fb_x = m_render_state.draw_offset_x;
    m_render_state.render_fb_y = m_render_state.draw_offset_y;
    m_render_state.render_fb_w = m_render_state.draw_region_w;
    m_render_state.render_fb_h = m_render_state.draw_region_h;
    glViewport(m_render_state.draw_offset_x, m_render_state.draw_offset_y,
               m_render_state.draw_region_w, m_render_state.draw_region_h);
  } else {
    m_render_state.render_fb_x = 0;
    m_render_state.render_fb_y = 0;
    m_render_state.render_fb_w = settings.game_res_w;
    m_render_state.render_fb_h = settings.game_res_h;
    glViewport(0, 0, settings.game_res_w, settings.game_res_h);
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
      glFinish();
    }

    // lg::info("Render: {} end", g_current_render);
    //  should have ended at the start of the next chain
    ASSERT(dma.current_tag_offset() == m_render_state.next_bucket);
    m_render_state.next_bucket += 16;
    vif_interrupt_callback();
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
                                       int y,
                                       GLuint fbo) {
  std::vector<u32> buffer(width * height);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  GLint oldbuf;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldbuf);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
  VkBuffer dstBuffer;
  VkDeviceMemory device_memory;

  if (vkBindBufferMemory(m_device, destination_buffer, device_memory, 0) != VK_SUCCESS) {
    throw std::runtime_error("Buffer Memory requested, but not available!");
  }

  VkBufferImageCopy bufferImageCopy{};
  vkCmdCopyImageToBuffer(commandBuffer, srcImage,
                         srcImageLayout, dstBuffer,
                         1, &bufferImageCopy);

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
  glBindFramebuffer(GL_READ_FRAMEBUFFER, oldbuf);
}

void VulkanRenderer::do_pcrtc_effects(float alp,
                                      SharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  if (m_fbo_state.render_fbo->is_window) {
    // nothing to do!
  } else {
    Fbo* window_blit_src = nullptr;
    if (m_fbo_state.resources.resolve_buffer.valid) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo_state.render_fbo->fbo_id);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo_state.resources.resolve_buffer.fbo_id);
      glBlitFramebuffer(0,                                            // srcX0
                        0,                                            // srcY0
                        m_fbo_state.render_fbo->width,                // srcX1
                        m_fbo_state.render_fbo->height,               // srcY1
                        0,                                            // dstX0
                        0,                                            // dstY0
                        m_fbo_state.resources.resolve_buffer.width,   // dstX1
                        m_fbo_state.resources.resolve_buffer.height,  // dstY1
                        GL_COLOR_BUFFER_BIT,                          // mask
                        GL_LINEAR                                     // filter
      );
      window_blit_src = &m_fbo_state.resources.resolve_buffer;
    } else {
      window_blit_src = &m_fbo_state.resources.render_buffer;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, window_blit_src->fbo_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,                                                          // srcX0
                      0,                                                          // srcY0
                      window_blit_src->width,                                     // srcX1
                      window_blit_src->height,                                    // srcY1
                      render_state->draw_offset_x,                                // dstX0
                      render_state->draw_offset_y,                                // dstY0
                      render_state->draw_offset_x + render_state->draw_region_w,  // dstX1
                      render_state->draw_offset_y + render_state->draw_region_h,  // dstY1
                      GL_COLOR_BUFFER_BIT,                                        // mask
                      GL_LINEAR                                                   // filter
    );
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  if (alp < 1) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    glBlendEquation(GL_FUNC_ADD);
    glViewport(0, 0, m_fbo_state.resources.window.width, m_fbo_state.resources.window.height);

    m_blackout_renderer.draw(Vector4f(0, 0, 0, 1.f - alp), render_state, prof);

    glEnable(GL_DEPTH_TEST);
  }
}


 void VulkanRenderer::cleanupSwapChain() {
  vkDestroyImageView(device, depthImageView, nullptr);
  vkDestroyImage(device, depthImage, nullptr);
  vkFreeMemory(device, depthImageMemory, nullptr);

  vkDestroyImageView(device, colorImageView, nullptr);
  vkDestroyImage(device, colorImage, nullptr);
  vkFreeMemory(device, colorImageMemory, nullptr);

  for (auto framebuffer : swapChainFramebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }

  for (auto imageView : swapChainImageViews) {
    vkDestroyImageView(device, imageView, nullptr);
  }

  vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void VulkanRenderer::cleanup() {
  cleanupSwapChain();

  vkDestroyPipeline(device, graphicsPipeline, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyRenderPass(device, renderPass, nullptr);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroyBuffer(device, uniformBuffers[i], nullptr);
    vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
  }

  vkDestroyDescriptorPool(device, descriptorPool, nullptr);

  vkDestroySampler(device, textureSampler, nullptr);
  vkDestroyImageView(device, textureImageView, nullptr);

  vkDestroyImage(device, textureImage, nullptr);
  vkFreeMemory(device, textureImageMemory, nullptr);

  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

  vkDestroyBuffer(device, indexBuffer, nullptr);
  vkFreeMemory(device, indexBufferMemory, nullptr);

  vkDestroyBuffer(device, vertexBuffer, nullptr);
  vkFreeMemory(device, vertexBufferMemory, nullptr);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(device, inFlightFences[i], nullptr);
  }

  vkDestroyCommandPool(device, commandPool, nullptr);

  vkDestroyDevice(device, nullptr);

  if (enableValidationLayers) {
    DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
  }

  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);

  glfwDestroyWindow(window);

  glfwTerminate();
}

void VulkanRenderer::recreateSwapChain() {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window, &width, &height);
    glfwWaitEvents();
  }

  vkDeviceWaitIdle(device);

  cleanupSwapChain();

  createSwapChain();
  createImageViews();
  createColorResources();
  createDepthResources();
  createFramebuffers();
}

void VulkanRenderer::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Hello Triangle";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;

    createInfo.pNext = nullptr;
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
  }
}

void VulkanRenderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
}

void VulkanRenderer::setupDebugMessenger() {
  if (!enableValidationLayers)
    return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(createInfo);

  if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
    throw std::runtime_error("failed to set up debug messenger!");
  }
}

void VulkanRenderer::createSurface() {
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface!");
  }
}

void VulkanRenderer::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

  for (const auto& device : devices) {
    if (isDeviceSuitable(device)) {
      physicalDevice = device;
      msaaSamples = getMaxUsableSampleCount();
      break;
    }
  }

  if (physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("failed to find a suitable GPU!");
  }

  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
}

void VulkanRenderer::createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                            indices.presentFamily.value()};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures{};
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

  createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  createInfo.pEnabledFeatures = &deviceFeatures;

  createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
  }

  vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
  vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
}

void VulkanRenderer::createSwapChain() {
  SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

  VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface;

  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
  uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

  if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
  }

  vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
  swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

  swapChainImageFormat = surfaceFormat.format;
  swapChainExtent = extent;
}

void VulkanRenderer::createImageViews() {
  swapChainImageViews.resize(swapChainImages.size());

  for (uint32_t i = 0; i < swapChainImages.size(); i++) {
    swapChainImageViews[i] =
        createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}

void VulkanRenderer::createRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = swapChainImageFormat;
  colorAttachment.samples = msaaSamples;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = findDepthFormat();
  depthAttachment.samples = msaaSamples;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription colorAttachmentResolve{};
  colorAttachmentResolve.format = swapChainImageFormat;
  colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentResolveRef{};
  colorAttachmentResolveRef.attachment = 2;
  colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;
  subpass.pResolveAttachments = &colorAttachmentResolveRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 3> attachments = {colorAttachment, depthAttachment,
                                                        colorAttachmentResolve};
  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}

void VulkanRenderer::createDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.pImmutableSamplers = nullptr;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 1;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.pImmutableSamplers = nullptr;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};
  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void VulkanRenderer::createGraphicsPipeline() {
  auto vertShaderCode = readFile("shaders/vert.spv");
  auto fragShaderCode = readFile("shaders/frag.spv");

  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  auto bindingDescription = Vertex::getBindingDescription();
  auto attributeDescriptions = Vertex::getAttributeDescriptions();

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = msaaSamples;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                &graphicsPipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }

  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanRenderer::createFramebuffers() {
  swapChainFramebuffers.resize(swapChainImageViews.size());

  for (size_t i = 0; i < swapChainImageViews.size(); i++) {
    std::array<VkImageView, 3> attachments = {colorImageView, depthImageView,
                                              swapChainImageViews[i]};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
  }
}

void VulkanRenderer::createCommandPool() {
  QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics command pool!");
  }
}

void VulkanRenderer::createColorResources() {
  VkFormat colorFormat = swapChainImageFormat;

  createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
  colorImageView = createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void VulkanRenderer::createDepthResources() {
  VkFormat depthFormat = findDepthFormat();

  createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat,
              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
  depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                             VkImageTiling tiling,
                             VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("failed to find supported format!");
}

VkFormat findDepthFormat() {
  return findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool hasStencilComponent(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void VulkanRenderer::createTextureImage() {
  int texWidth, texHeight, texChannels;
  stbi_uc* pixels =
      stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  VkDeviceSize imageSize = texWidth * texHeight * 4;
  mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image!");
  }

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void* data;
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);

  stbi_image_free(pixels);

  createImage(texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

  transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
  copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth),
                    static_cast<uint32_t>(texHeight));
  // transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);

  generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevels);
}

void VulkanRenderer::generateMipmaps(VkImage image,
                     VkFormat imageFormat,
                     int32_t texWidth,
                     int32_t texHeight,
                     uint32_t mipLevels) {
  // Check if image format supports linear blitting
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

  if (!(formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    throw std::runtime_error("texture image format does not support linear blitting!");
  }

  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  int32_t mipWidth = texWidth;
  int32_t mipHeight = texHeight;

  for (uint32_t i = 1; i < mipLevels; i++) {
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    if (mipWidth > 1)
      mipWidth /= 2;
    if (mipHeight > 1)
      mipHeight /= 2;
  }

  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  endSingleTimeCommands(commandBuffer);
}

VkSampleCountFlagBits getMaxUsableSampleCount() {
  VkPhysicalDeviceProperties physicalDeviceProperties;
  vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

  VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                              physicalDeviceProperties.limits.framebufferDepthSampleCounts;
  if (counts & VK_SAMPLE_COUNT_64_BIT) {
    return VK_SAMPLE_COUNT_64_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_32_BIT) {
    return VK_SAMPLE_COUNT_32_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_16_BIT) {
    return VK_SAMPLE_COUNT_16_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_8_BIT) {
    return VK_SAMPLE_COUNT_8_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_4_BIT) {
    return VK_SAMPLE_COUNT_4_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_2_BIT) {
    return VK_SAMPLE_COUNT_2_BIT;
  }

  return VK_SAMPLE_COUNT_1_BIT;
}

void VulkanRenderer::createTextureImageView() {
  textureImageView =
      createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
}

void VulkanRenderer::createTextureSampler() {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}

VkImageView createImageView(VkImage image,
                            VkFormat format,
                            VkImageAspectFlags aspectFlags,
                            uint32_t mipLevels) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }

  return imageView;
}

void VulkanRenderer::createImage(uint32_t width,
                 uint32_t height,
                 uint32_t mipLevels,
                 VkSampleCountFlagBits numSamples,
                 VkFormat format,
                 VkImageTiling tiling,
                 VkImageUsageFlags usage,
                 VkMemoryPropertyFlags properties,
                 VkImage& image,
                 VkDeviceMemory& imageMemory) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = numSamples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}

void VulkanRenderer::transitionImageLayout(VkImage image,
                           VkFormat format,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           uint32_t mipLevels) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  endSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);

  endSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::loadModel() {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())) {
    throw std::runtime_error(warn + err);
  }

  std::unordered_map<Vertex, uint32_t> uniqueVertices{};

  for (const auto& shape : shapes) {
    for (const auto& index : shape.mesh.indices) {
      Vertex vertex{};

      vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]};

      vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
                         1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

      vertex.color = {1.0f, 1.0f, 1.0f};

      if (uniqueVertices.count(vertex) == 0) {
        uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
        vertices.push_back(vertex);
      }

      indices.push_back(uniqueVertices[vertex]);
    }
  }
}

void VulkanRenderer::createVertexBuffer() {
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void* data;
  vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, vertices.data(), (size_t)bufferSize);
  vkUnmapMemory(device, stagingBufferMemory);

  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

  copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VulkanRenderer::createIndexBuffer() {
  VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void* data;
  vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, indices.data(), (size_t)bufferSize);
  vkUnmapMemory(device, stagingBufferMemory);

  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

  copyBuffer(stagingBuffer, indexBuffer, bufferSize);

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VulkanRenderer::createUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(UniformBufferObject);

  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 uniformBuffers[i], uniformBuffersMemory[i]);
  }
}

void VulkanRenderer::createDescriptorPool() {
  std::array<VkDescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

void VulkanRenderer::createDescriptorSets() {
  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  allocInfo.pSetLayouts = layouts.data();

  descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
  if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate descriptor sets!");
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffers[i];
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureImageView;
    imageInfo.sampler = textureSampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  }
}

void VulkanRenderer::createBuffer(VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& buffer,
                  VkDeviceMemory& bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate buffer memory!");
  }

  vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

VkCommandBuffer beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanRenderer::createCommandBuffers() {
  commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

  if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = swapChainExtent;

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clearValues[1].depthStencil = {1.0f, 0};

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)swapChainExtent.width;
  viewport.height = (float)swapChainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  VkBuffer vertexBuffers[] = {vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &descriptorSets[currentFrame], 0, nullptr);

  vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

  vkCmdEndRenderPass(commandBuffer);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}

void VulkanRenderer::createSyncObjects() {
  imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) !=
            VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) !=
            VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create synchronization objects for a frame!");
    }
  }
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto currentTime = std::chrono::high_resolution_clock::now();
  float time =
      std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

  UniformBufferObject ubo{};
  ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                         glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.proj = glm::perspective(glm::radians(45.0f),
                              swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 10.0f);
  ubo.proj[1][1] *= -1;

  void* data;
  vkMapMemory(device, uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
  memcpy(data, &ubo, sizeof(ubo));
  vkUnmapMemory(device, uniformBuffersMemory[currentImage]);
}

void VulkanRenderer::drawFrame() {
  vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

  uint32_t imageIndex;
  VkResult result =
      vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
                            VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  updateUniformBuffer(currentFrame);

  vkResetFences(device, 1, &inFlightFences[currentFrame]);

  vkResetCommandBuffer(commandBuffers[currentFrame], /*VkCommandBufferResetFlagBits*/ 0);
  recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
    throw std::runtime_error("failed to submit draw command buffer!");
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;

  presentInfo.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(presentQueue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }

  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkShaderModule createShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
  for (const auto& availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }

  return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
  for (const auto& availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return availablePresentMode;
    }
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);

    return actualExtent;
  }
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
  SwapChainSupportDetails details;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
  }

  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount,
                                              details.presentModes.data());
  }

  return details;
}

bool isDeviceSuitable(VkPhysicalDevice device) {
  QueueFamilyIndices indices = findQueueFamilies(device);

  bool extensionsSupported = checkDeviceExtensionSupport(device);

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
  }

  VkPhysicalDeviceFeatures supportedFeatures;
  vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

  return indices.isComplete() && extensionsSupported && swapChainAdequate &&
         supportedFeatures.samplerAnisotropy;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
  uint32_t extensionCount;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

  for (const auto& extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

  int i = 0;
  for (const auto& queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
    }

    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

    if (presentSupport) {
      indices.presentFamily = i;
    }

    if (indices.isComplete()) {
      break;
    }

    i++;
  }

  return indices;
}

std::vector<const char*> getRequiredExtensions() {
  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

bool checkValidationLayerSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char* layerName : validationLayers) {
    bool layerFound = false;

    for (const auto& layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}
