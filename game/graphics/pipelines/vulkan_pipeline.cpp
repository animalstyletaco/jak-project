/*!
 * @file vulkan.cpp
 * Lower-level vulkan interface. No actual rendering is performed here!
 */

#include "vulkan_pipeline.h"

#include <condition_variable>
#include <memory>
#include <mutex>

#include "common/dma/dma_copy.h"
#include "common/global_profiler/GlobalProfiler.h"
#include "common/goal_constants.h"
#include "common/log/log.h"
#include "common/util/FileUtil.h"
#include "common/util/FrameLimiter.h"
#include "common/util/Timer.h"
#include "common/util/compress.h"
#include "game/system/hid/input_manager.h"
#include "game/system/hid/sdl_util.h"

#include "third-party/SDL/include/SDL_vulkan.h"
#include "game/graphics/display.h"
#include "game/graphics/gfx.h"
#include "game/graphics/general_renderer/debug_gui.h"
#include "game/graphics/pipelines/vulkan_pipeline.h"
#include "game/graphics/vulkan_renderer/VulkanRenderer.h"
#include "game/graphics/texture/VulkanTexturePool.h"
#include "game/runtime.h"

#include "third-party/imgui/imgui.h"
#include "third-party/imgui/imgui_style.h"
#define STBI_WINDOWS_UTF8
#include "common/util/dialogs.h"
#include "third-party/stb_image/stb_image.h"

namespace {

struct VulkanGraphicsData {
  // vsync
  std::mutex sync_mutex;
  std::condition_variable sync_cv;

  // dma chain transfer
  std::mutex dma_mutex;
  std::condition_variable dma_cv;
  u64 frame_idx = 0;
  u64 frame_idx_of_input_data = 0;
  bool has_data_to_render = false;
  FixedChunkDmaCopier dma_copier;

  // texture pool
  std::shared_ptr<VulkanTexturePool> texture_pool;

  std::shared_ptr<VulkanLoader> loader;

  VulkanRenderer vulkan_renderer;
  GraphicsDebugGui debug_gui;

  FrameLimiter frame_limiter;
  Timer engine_timer;
  double last_engine_time = 1. / 60.;
  float pmode_alp = 0.f;

  std::string imgui_log_filename, imgui_filename;
  GameVersion version;

  VulkanGraphicsData(GameVersion version, std::unique_ptr<GraphicsDeviceVulkan>& vulkan_device)
      : dma_copier(EE_MAIN_MEM_SIZE),
        texture_pool(std::make_shared<VulkanTexturePool>(version, vulkan_device)),
        loader(std::make_shared<VulkanLoader>(
            vulkan_device,
            file_util::get_jak_project_dir() / "out" / game_version_names[version] / "fr3",
            pipeline_common::fr3_level_count[version])),
        vulkan_renderer(texture_pool, loader, version, vulkan_device), debug_gui(version),
        version(version) {}
};
}  // namespace

std::unique_ptr<VulkanGraphicsData> g_gfx_data;
std::unique_ptr<GraphicsDeviceVulkan> g_vulkan_device;

static bool vk_inited = false;
static int vk_init(GfxGlobalSettings& settings) {
  profiler::prof().instant_event("ROOT");
  Timer gl_init_timer;
  // Initialize SDL
  {
    auto p = profiler::scoped_prof("startup::sdl::init_sdl");
    // remove SDL garbage from hooking signal handler.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
      sdl_util::log_error("Could not initialize SDL, exiting");
      dialogs::create_error_message_dialog("Critical Error Encountered",
                                           "Could not initialize SDL, exiting");
      return 1;
    }
  }

  {
    auto p = profiler::scoped_prof("startup::sdl::get_version_info");
    SDL_version compiled;
    SDL_VERSION(&compiled);
    SDL_version linked;
    SDL_GetVersion(&linked);
    lg::info("SDL Initialized, compiled with version - {}.{}.{} | linked with version - {}.{}.{}",
             compiled.major, compiled.minor, compiled.patch, linked.major, linked.minor,
             linked.patch);
  }

  {
    auto p = profiler::scoped_prof("startup::sdl::set_gl_attributes");

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    if (settings.debug) {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    } else {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  }
  lg::info("gl init took {:.3f}s\n", gl_init_timer.getSeconds());
  return 0;
}

static std::shared_ptr<GfxDisplay> vk_make_display(int width,
                                                   int height,
                                                   const char* title,
                                                   GfxGlobalSettings& /*settings*/,
                                                   GameVersion game_version,
                                                   bool is_main) {
  // Setup the window
  profiler::prof().instant_event("ROOT");
  profiler::prof().begin_event("startup::sdl::create_window");
  // TODO - SDL2 doesn't seem to support HDR (and neither does windows)
  //   Related -
  //   https://answers.microsoft.com/en-us/windows/forum/all/hdr-monitor-low-brightness-after-exiting-full/999f7ee9-7ba3-4f9c-b812-bbeb9ff8dcc1
  SDL_Window* window =
      SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  profiler::prof().end_event();
  if (!window) {
    sdl_util::log_error("gl_make_display failed - Could not create display window");
    dialogs::create_error_message_dialog(
        "Critical Error Encountered",
        "Unable to create OpenGL window.\nOpenGOAL requires OpenGL 4.3.\nEnsure your GPU "
        "supports this and your drivers are up to date.");
    return NULL;
  }

  // Make an OpenGL Context
  profiler::prof().begin_event("startup::sdl::create_context");
  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  profiler::prof().end_event();
  if (!gl_context) {
    sdl_util::log_error("gl_make_display failed - Could not create OpenGL Context");
    dialogs::create_error_message_dialog(
        "Critical Error Encountered",
        "Unable to create OpenGL context.\nOpenGOAL requires OpenGL 4.3.\nEnsure your GPU "
        "supports this and your drivers are up to date.");
    return NULL;
  }

  {
    auto p = profiler::scoped_prof("startup::sdl::assign_context");
    if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
      sdl_util::log_error("gl_make_display failed - Could not associated context with window");
      dialogs::create_error_message_dialog("Critical Error Encountered",
                                           "Unable to create OpenGL window with context.\nOpenGOAL "
                                           "requires OpenGL 4.3.\nEnsure your GPU "
                                           "supports this and your drivers are up to date.");
      return NULL;
    }
  }

  if (!vk_inited) {
    auto p = profiler::scoped_prof("startup::sdl::gfx_data_init");
    g_vulkan_device = std::make_unique<GraphicsDeviceVulkan>(window);
    g_gfx_data = std::make_unique<VulkanGraphicsData>(game_version, g_vulkan_device);
    vk_inited = true;
  }

  {
    auto p = profiler::scoped_prof("startup::sdl::window_extras");
    // Setup Window Icon
    // TODO - hiDPI icon
    // https://sourcegraph.com/github.com/dfranx/SHADERed/-/blob/main.cpp?L422:24&subtree=true
    int icon_width;
    int icon_height;
    std::string image_path =
        (file_util::get_jak_project_dir() / "game" / "assets" / "appicon.png").string();
    auto icon_data =
        stbi_load(image_path.c_str(), &icon_width, &icon_height, nullptr, STBI_rgb_alpha);
    if (icon_data) {
      SDL_Surface* icon_surf = SDL_CreateRGBSurfaceWithFormatFrom(
          (void*)icon_data, icon_width, icon_height, 32, 4 * icon_width, SDL_PIXELFORMAT_RGBA32);
      SDL_SetWindowIcon(window, icon_surf);
      SDL_FreeSurface(icon_surf);
      stbi_image_free(icon_data);
    } else {
      lg::error("Could not load icon for OpenGL window");
    }
  }

  profiler::prof().begin_event("startup::sdl::create_GLDisplay");
  auto display =
      std::make_shared<VkDisplay>(window, g_gfx_data->vulkan_renderer.GetSwapChain(),
                                  Display::g_display_manager, Display::g_input_manager, is_main);
  display->set_imgui_visible(Gfx::g_debug_settings.show_imgui);
  profiler::prof().end_event();

  {
    auto p = profiler::scoped_prof("startup::sdl::init_imgui");
    // check that version of the library is okay
    IMGUI_CHECKVERSION();

    // this does initialization for stuff like the font data
    ImGui::CreateContext();

    // Init ImGui settings
    g_gfx_data->imgui_filename = file_util::get_file_path({"imgui.ini"});
    g_gfx_data->imgui_log_filename = file_util::get_file_path({"imgui_log.txt"});
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // We manage the mouse cursor!
    if (!Gfx::g_debug_settings.monospaced_font) {
      // TODO - add or switch to Noto since it supports the entire unicode range
      std::string font_path =
          (file_util::get_jak_project_dir() / "game" / "assets" / "fonts" / "NotoSansJP-Medium.ttf")
              .string();
      if (file_util::file_exists(font_path)) {
        static const ImWchar ranges[] = {
            0x0020, 0x00FF,  // Basic Latin + Latin Supplement
            0x0400, 0x052F,  // Cyrillic + Cyrillic Supplement
            0x2000, 0x206F,  // General Punctuation
            0x2DE0, 0x2DFF,  // Cyrillic Extended-A
            0x3000, 0x30FF,  // CJK Symbols and Punctuations, Hiragana, Katakana
            0x3131, 0x3163,  // Korean alphabets
            0x31F0, 0x31FF,  // Katakana Phonetic Extensions
            0x4E00, 0x9FAF,  // CJK Ideograms
            0xA640, 0xA69F,  // Cyrillic Extended-B
            0xAC00, 0xD7A3,  // Korean characters
            0xFF00, 0xFFEF,  // Half-width characters
            0xFFFD, 0xFFFD,  // Invalid
            0,
        };
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), Gfx::g_debug_settings.imgui_font_size,
                                     nullptr, ranges);
      }
    }

    io.IniFilename = g_gfx_data->imgui_filename.c_str();
    io.LogFilename = g_gfx_data->imgui_log_filename.c_str();

    if (Gfx::g_debug_settings.alternate_style) {
      ImGui::applyAlternateStyle();
    }

    // set up to get inputs for this window
    ImGui_ImplSDL2_InitForVulkan(window);

    // NOTE: imgui's setup calls functions that may fail intentionally, and attempts to disable
    // error reporting so these errors are invisible. But it does not work, and some weird X11
    // default cursor error is set here that we clear.
    SDL_ClearError();
  }

  return std::static_pointer_cast<GfxDisplay>(display);
}


static void vk_exit() {
  g_gfx_data.reset();
  vk_inited = false;
}

VkDisplay::VkDisplay(SDL_Window* window,
          std::unique_ptr<SwapChain>& swapChain,
          std::shared_ptr<DisplayManager> display_manager,
          std::shared_ptr<InputManager> input_manager,
          bool is_main)
    : m_window(window),
      m_imgui_helper(swapChain),
      m_display_manager(display_manager),
      m_input_manager(input_manager) {
  m_main = is_main;
  m_display_manager->set_input_manager(m_input_manager);
  // Register commands
  m_input_manager->register_command(CommandBinding::Source::KEYBOARD,
                                    CommandBinding(SDLK_F12, [&]() {
                                      if (!Gfx::g_debug_settings.ignore_hide_imgui) {
                                        set_imgui_visible(!is_imgui_visible());
                                      }
                                    }));
  m_input_manager->register_command(
      CommandBinding::Source::KEYBOARD,
      CommandBinding(SDLK_F2, [&]() { m_take_screenshot_next_frame = true; }));
}

VkDisplay::~VkDisplay() {
    // Cleanup ImGUI
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    // Cleanup SDL
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    if (m_main) {
      vk_exit();
    }
  }

namespace {
std::string make_full_screenshot_output_file_path(const std::string& file_name) {
  file_util::create_dir_if_needed(file_util::get_file_path({"screenshots"}));
  return file_util::get_file_path({"screenshots", file_name});
}
}  // namespace

static std::string get_current_timestamp() {
  auto current_time = std::time(0);
  auto local_current_time = *std::localtime(&current_time);
  // Remember to increase size of result if the date format is changed
  char result[20];
  std::strftime(result, sizeof(result), "%Y_%m_%d_%H_%M_%S", &local_current_time);
  return std::string(result);
}

static std::string make_hotkey_screenshot_file_name() {
  return version_to_game_name(g_game_version) + "_" + get_current_timestamp() + ".png";
}

static bool endsWith(std::string_view str, std::string_view suffix) {
  return str.size() >= suffix.size() &&
         0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

void render_vk_game_frame(int game_width,
                       int game_height,
                       int window_fb_width,
                       int window_fb_height,
                       int draw_region_width,
                       int draw_region_height,
                       int msaa_samples,
                       bool take_screenshot) {
  // wait for a copied chain.
  bool got_chain = false;
  {
    auto p = profiler::scoped_prof("wait-for-dma");
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    // there's a timeout here, so imgui can still be responsive even if we don't render anything
    got_chain = g_gfx_data->dma_cv.wait_for(lock, std::chrono::milliseconds(40),
                                            [=] { return g_gfx_data->has_data_to_render; });
  }
  // render that chain.
  if (got_chain) {
    g_gfx_data->frame_idx_of_input_data = g_gfx_data->frame_idx;
    RenderOptions options;
    options.game_res_w = game_width;
    options.game_res_h = game_height;
    options.window_framebuffer_width = window_fb_width;
    options.window_framebuffer_height = window_fb_height;
    options.draw_region_width = draw_region_width;
    options.draw_region_height = draw_region_height;
    options.msaa_samples = msaa_samples;
    options.draw_render_debug_window = g_gfx_data->debug_gui.should_draw_render_debug();
    options.draw_profiler_window = g_gfx_data->debug_gui.should_draw_profiler();
    options.draw_loader_window = g_gfx_data->debug_gui.should_draw_loader_menu();
    options.draw_subtitle_editor_window = g_gfx_data->debug_gui.should_draw_subtitle_editor();
    options.draw_filters_window = g_gfx_data->debug_gui.should_draw_filters_menu();
    options.save_screenshot = false;
    options.quick_screenshot = false;
    options.internal_res_screenshot = false;
    options.gpu_sync = g_gfx_data->debug_gui.should_gl_finish();

    if (take_screenshot) {
      options.save_screenshot = true;
      options.quick_screenshot = true;
      options.screenshot_path = file_util::make_screenshot_filepath(g_game_version);
    }
    if (g_gfx_data->debug_gui.get_screenshot_flag()) {
      options.save_screenshot = true;
      options.game_res_w = g_gfx_data->debug_gui.screenshot_width;
      options.game_res_h = g_gfx_data->debug_gui.screenshot_height;
      options.draw_region_width = options.game_res_w;
      options.draw_region_height = options.game_res_h;
      options.msaa_samples = g_gfx_data->debug_gui.screenshot_samples;
      options.screenshot_path = file_util::make_screenshot_filepath(
          g_game_version, g_gfx_data->debug_gui.screenshot_name());
    }

    options.draw_small_profiler_window =
        g_gfx_data->debug_gui.master_enable && g_gfx_data->debug_gui.small_profiler;
    options.pmode_alp_register = g_gfx_data->pmode_alp;

    if constexpr (pipeline_common::run_dma_copy) {
      auto& chain = g_gfx_data->dma_copier.get_last_result();
      g_gfx_data->vulkan_renderer.render(DmaFollower(chain.data.data(), chain.start_offset), options);
    } else {
      auto p = profiler::scoped_prof("vulkan-render");
      g_gfx_data->vulkan_renderer.render(DmaFollower(g_gfx_data->dma_copier.get_last_input_data(),
                                                  g_gfx_data->dma_copier.get_last_input_offset()),
                                      options);
    }
  }

  // before vsync, mark the chain as rendered.
  {
    // should be fine to remove this mutex if the game actually waits for vsync to call
    // send_chain again. but let's be safe for now.
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    g_gfx_data->engine_timer.start();
    g_gfx_data->has_data_to_render = false;
    g_gfx_data->sync_cv.notify_all();
  }
}
/*!
 * Main function called to render graphics frames. This is called in a loop.
 */
void VkDisplay::render() {
  // Before we process the current frames SDL events we for keyboard/mouse button inputs.
  //
  // This technically means that keyboard/mouse button inputs will be a frame behind but the
  // event-based code is buggy and frankly not worth stressing over.  Leaving this as a note incase
  // someone complains. Binding handling is still taken care of by the event code though.
  {
    auto p = profiler::scoped_prof("sdl-input-monitor-poll-for-kb-mouse");
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) {
      m_input_manager->clear_keyboard_actions();
    } else {
      m_input_manager->poll_keyboard_data();
    }
    if (io.WantCaptureMouse) {
      m_input_manager->clear_mouse_actions();
    } else {
      m_input_manager->poll_mouse_data();
    }
    m_input_manager->finish_polling();
  }
  // Now process SDL Events
  process_sdl_events();
  // Also process any display related events received from the EE (the game)
  // this is done here so they run from the perspective of the graphics thread
  {
    auto p = profiler::scoped_prof("display-manager-ee-events");
    m_display_manager->process_ee_events();
  }
  {
    auto p = profiler::scoped_prof("input-manager-ee-events");
    m_input_manager->process_ee_events();
  }

  // imgui start of frame
  {
    auto p = profiler::scoped_prof("imgui-new-frame");
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
  }

  // framebuffer size
  int fbuf_w, fbuf_h;
  SDL_Vulkan_GetDrawableSize(m_window, &fbuf_w, &fbuf_h);

  // render game!
  g_gfx_data->debug_gui.master_enable = is_imgui_visible();
  if (g_gfx_data->debug_gui.should_advance_frame()) {
    auto p = profiler::scoped_prof("game-render");
    int game_res_w = Gfx::g_global_settings.game_res_w;
    int game_res_h = Gfx::g_global_settings.game_res_h;
    if (game_res_w <= 0 || game_res_h <= 0) {
      // if the window size reports 0, the game will ask for a 0 sized window, and nothing likes
      // that.
      game_res_w = 640;
      game_res_h = 480;
    }
    render_vk_game_frame(
        game_res_w, game_res_h, fbuf_w, fbuf_h, Gfx::g_global_settings.lbox_w,
        Gfx::g_global_settings.lbox_h, Gfx::g_global_settings.msaa_samples,
        m_take_screenshot_next_frame && g_gfx_data->debug_gui.screenshot_hotkey_enabled);
    // If we took a screenshot, stop taking them now!
    if (m_take_screenshot_next_frame) {
      m_take_screenshot_next_frame = false;
    }
  }

  // render debug
  if (is_imgui_visible()) {
    auto p = profiler::scoped_prof("debug-gui");
    g_gfx_data->debug_gui.draw(g_gfx_data->dma_copier.get_last_result().stats);
  }
  {
    auto p = profiler::scoped_prof("imgui-render");
    m_imgui_helper.Render(Gfx::g_global_settings.game_res_w, Gfx::g_global_settings.game_res_h,
                          g_gfx_data->vulkan_renderer.GetSwapChain());
  }

  // actual vsync
  g_gfx_data->debug_gui.finish_frame();
  if (Gfx::g_global_settings.framelimiter) {
    auto p = profiler::scoped_prof("frame-limiter");
    g_gfx_data->frame_limiter.run(
        Gfx::g_global_settings.target_fps, Gfx::g_global_settings.experimental_accurate_lag,
        Gfx::g_global_settings.sleep_in_frame_limiter, g_gfx_data->last_engine_time);
  }

  // Start timing for the next frame.
  g_gfx_data->debug_gui.start_frame();
  profiler::prof().instant_event("ROOT");

  // toggle even odd and wake up engine waiting on vsync.
  // TODO: we could play with moving this earlier, right after the final bucket renderer.
  //       it breaks the VIF-interrupt profiling though.
  {
    profiler::prof().instant_event("engine-notify");
    std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
    g_gfx_data->frame_idx++;
    g_gfx_data->sync_cv.notify_all();
  }

  // reboot whole game, if requested
  if (g_gfx_data->debug_gui.want_reboot_in_debug) {
    g_gfx_data->debug_gui.want_reboot_in_debug = false;
    MasterExit = RuntimeExitStatus::RESTART_IN_DEBUG;
  }

  {
    auto p = profiler::scoped_prof("check-close-window");
    // exit if display window was closed
    if (m_should_quit) {
      std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
      MasterExit = RuntimeExitStatus::EXIT;
      g_gfx_data->sync_cv.notify_all();
    }
  }
}

/*!
 * Wait for the next vsync. Returns 0 or 1 depending on if frame is even or odd.
 * Called from the game thread, on a GOAL stack.
 */
u32 vk_vsync() {
  if (!g_gfx_data) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
  auto init_frame = g_gfx_data->frame_idx_of_input_data;
  g_gfx_data->sync_cv.wait(lock, [=] {
    return (MasterExit != RuntimeExitStatus::RUNNING) || g_gfx_data->frame_idx > init_frame;
  });
  return g_gfx_data->frame_idx & 1;
}

u32 vk_sync_path() {
  if (!g_gfx_data) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
  g_gfx_data->last_engine_time = g_gfx_data->engine_timer.getSeconds();
  if (!g_gfx_data->has_data_to_render) {
    return 0;
  }
  g_gfx_data->sync_cv.wait(lock, [=] { return !g_gfx_data->has_data_to_render; });
  return 0;
}

/*!
 * Send DMA to the renderer.
 * Called from the game thread, on a GOAL stack.
 */
void vk_send_chain(const void* data, u32 offset) {
  if (g_gfx_data) {
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    if (g_gfx_data->has_data_to_render) {
      lg::error(
          "Gfx::send_chain called when the graphics renderer has pending data. Was this called "
          "multiple times per frame?");
      return;
    }

    // we copy the dma data and give a copy of it to the render.
    // the copy has a few advantages:
    // - if the game code has a bug and corrupts the DMA buffer, the renderer won't see it.
    // - the copied DMA is much smaller than the entire game memory, so it can be dumped to a file
    //    separate of the entire RAM.
    // - it verifies the DMA data is valid early on.
    // but it may also be pretty expensive. Both the renderer and the game wait on this to complete.

    // The renderers should just operate on DMA chains, so eliminating this step in the future may
    // be easy.

    g_gfx_data->dma_copier.set_input_data(data, offset, pipeline_common::run_dma_copy);

    g_gfx_data->has_data_to_render = true;
    g_gfx_data->dma_cv.notify_all();
  }
}

void vk_texture_upload_now(const u8* tpage, int mode, u32 s7_ptr) {
  // block
  if (g_gfx_data) {
    // just pass it to the texture pool.
    // the texture pool will take care of locking.
    // we don't want to lock here for the entire duration of the conversion.
    g_gfx_data->texture_pool->handle_upload_now(tpage, mode, g_ee_main_mem, s7_ptr);
  }
}

void vk_texture_relocate(u32 destination, u32 source, u32 format) {
  if (g_gfx_data) {
    g_gfx_data->texture_pool->relocate(destination, source, format);
  }
}

void vk_set_levels(const std::vector<std::string>& levels) {
  g_gfx_data->loader->set_want_levels(levels);
}

void vk_set_pmode_alp(float val) {
  g_gfx_data->pmode_alp = val;
}

const GfxRendererModule gRendererVulkan = {
    vk_init,                // init
    vk_make_display,        // make_display
    vk_exit,                // exit
    vk_vsync,               // vsync
    vk_sync_path,           // sync_path
    vk_send_chain,          // send_chain
    vk_texture_upload_now,  // texture_upload_now
    vk_texture_relocate,    // texture_relocate
    vk_set_levels,          // set_levels
    vk_set_pmode_alp,       // set_pmode_alp
    GfxPipeline::Vulkan,    // pipeline
    "Vulkan 1.0"            // name
};
