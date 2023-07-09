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

#include "third-party/SDL/include/SDL.h"
#include "game/graphics/display.h"
#include "game/graphics/gfx.h"
#include "game/graphics/general_renderer/debug_gui.h"
#include "game/graphics/vulkan_renderer/VulkanRenderer.h"
#include "game/graphics/texture/VulkanTexturePool.h"
#include "game/runtime.h"

#include "third-party/imgui/imgui.h"
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

  FrameLimiter frame_limiter;
  Timer engine_timer;
  double last_engine_time = 1. / 60.;
  float pmode_alp = 0.f;

  std::string imgui_log_filename, imgui_filename;
  GameVersion version;

  VulkanGraphicsData(GameVersion version, std::unique_ptr<GraphicsDeviceVulkan>& vulkan_device)
      : dma_copier(EE_MAIN_MEM_SIZE),
        texture_pool(std::make_shared<VulkanTexturePool>(version, vulkan_device)),
        loader(std::make_shared<VulkanLoader>(vulkan_device, file_util::get_jak_project_dir() / "out" / game_version_names[version] / "fr3",
            pipeline_common::fr3_level_count[version])),
        vulkan_renderer(texture_pool, loader, version, vulkan_device),
        version(version) {}
};

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

static void gl_exit() {
  g_gfx_data.reset();
  vk_inited = false;
}

static void init_imgui(SDL_Window* window,
                       SDL_GLContext gl_context,
                       const std::string& glsl_version) {

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
    {
      auto p = profiler::scoped_prof("startup::sdl::glad_init");
      gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
      if (!gladLoadGL()) {
        lg::error("GL init fail");
        dialogs::create_error_message_dialog("Critical Error Encountered",
                                             "Unable to initialize OpenGL API.\nOpenGOAL requires "
                                             "OpenGL 4.3.\nEnsure your GPU "
                                             "supports this and your drivers are up to date.");
        return NULL;
      }
    }
    {
      auto p = profiler::scoped_prof("startup::sdl::gfx_data_init");
      g_gfx_data = std::make_unique<VulkanGraphicsData>(game_version);
    }
    vk_inited = true;
    const char* gl_version = (const char*)glGetString(GL_VERSION);
    lg::info("OpenGL initialized - v{}.{} | Renderer: {}", GLVersion.major, GLVersion.minor,
             gl_version);
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
  auto display = std::make_shared<VkDisplay>(window, gl_context, is_main);
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
          std::shared_ptr<DisplayManager>,
          std::shared_ptr<InputManager> inputManager,
          bool is_main) {
    : m_imgui_helper(swapChain),
      m_window(window),
      m_display_manager(std::make_shared<DisplayManager>(window)),
      m_input_manager(std::make_shared<InputManager>()) {
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

void vulkan_render_game_frame(int game_width,
                              int game_height,
                              int window_fb_width,
                              int window_fb_height,
                              int draw_region_width,
                              int draw_region_height,
                              int msaa_samples,
                              bool windows_borderless_hack) {
  // wait for a copied chain.
  bool got_chain = false;
  {
    auto p = scoped_prof("wait-for-dma");
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    // note: there's a timeout here. If the engine is messed up and not sending us frames,
    // we still want to run the glfw loop.
    got_chain = g_gfx_data->dma_cv.wait_for(lock, std::chrono::milliseconds(50),
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
    options.draw_render_debug_window = Gfx::debug_gui.should_draw_render_debug();
    options.draw_profiler_window = Gfx::debug_gui.should_draw_profiler();
    options.draw_loader_window = Gfx::debug_gui.should_draw_loader_menu();
    options.draw_subtitle_editor_window = Gfx::debug_gui.should_draw_subtitle_editor();
    options.save_screenshot = false;
    options.gpu_sync = Gfx::debug_gui.should_gl_finish();
    options.borderless_windows_hacks = windows_borderless_hack;

    want_hotkey_screenshot =
        want_hotkey_screenshot && Gfx::debug_gui.screenshot_hotkey_enabled;
    if (want_hotkey_screenshot) {
      want_hotkey_screenshot = false;
      options.save_screenshot = true;
      std::string screenshot_file_name = make_hotkey_screenshot_file_name();
      options.screenshot_path = make_full_screenshot_output_file_path(screenshot_file_name);
    }
    if (Gfx::debug_gui.get_screenshot_flag()) {
      options.save_screenshot = true;
      options.game_res_w = Gfx::debug_gui.screenshot_width;
      options.game_res_h = Gfx::debug_gui.screenshot_height;
      options.draw_region_width = options.game_res_w;
      options.draw_region_height = options.game_res_h;
      options.msaa_samples = Gfx::debug_gui.screenshot_samples;
      std::string screenshot_file_name = Gfx::debug_gui.screenshot_name();
      if (!endsWith(screenshot_file_name, ".png")) {
        screenshot_file_name += ".png";
      }
      options.screenshot_path = make_full_screenshot_output_file_path(screenshot_file_name);
    }
    options.draw_small_profiler_window = Gfx::debug_gui.small_profiler;
    options.pmode_alp_register = g_gfx_data->pmode_alp;

    auto msaa_max = g_gfx_data->vulkan_renderer.GetMaxUsableSampleCount();
    if (options.msaa_samples > msaa_max) {
      options.msaa_samples = msaa_max;
    }

    if constexpr (pipeline_common::run_dma_copy) {
      auto& chain = g_gfx_data->dma_copier.get_last_result();
      g_gfx_data->vulkan_renderer.render(DmaFollower(chain.data.data(), chain.start_offset),
                                         options);
    } else {
      auto p = scoped_prof("ogl-render");
      g_gfx_data->vulkan_renderer.render(
          DmaFollower(g_gfx_data->dma_copier.get_last_input_data(),
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

void VkDisplay::get_position(int* x, int* y) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (x) {
    *x = m_display_state.window_pos_x;
  }
  if (y) {
    *y = m_display_state.window_pos_y;
  }
}
void VkDisplay::get_size(int* width, int* height) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (width) {
    *width = m_display_state.window_size_width;
  }
  if (height) {
    *height = m_display_state.window_size_height;
  }
}
void VkDisplay::get_scale(float* xs, float* ys) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (xs) {
    *xs = m_display_state.window_scale_x;
  }
  if (ys) {
    *ys = m_display_state.window_scale_y;
  }
}

void VkDisplay::set_size(int width, int height) {
  // glfwSetWindowSize(m_window, width, height);
  m_pending_size.width = width;
  m_pending_size.height = height;
  m_pending_size.pending = true;
  if (windowed()) {
    m_last_windowed_width = width;
    m_last_windowed_height = height;
  }
}

void VkDisplay::update_fullscreen(GfxDisplayMode mode, int screen) {
  GLFWmonitor* monitor = get_monitor(screen);

  switch (mode) {
    case GfxDisplayMode::Windowed: {
      // windowed
      // TODO - display mode doesn't re-position the window
      int x, y, width, height;

      if (m_last_fullscreen_mode == GfxDisplayMode::Windowed) {
        // windowed -> windowed, keep position and size
        width = m_last_windowed_width;
        height = m_last_windowed_height;
        x = m_last_windowed_xpos;
        y = m_last_windowed_ypos;
        lg::debug("Windowed -> Windowed - x:{} | y:{}", x, y);
      } else {
        // fullscreen -> windowed, use last windowed size but on the monitor previously fullscreened
        //
        // glfwGetMonitorWorkarea will return the width/height of the scaled fullscreen window
        // - for example, you full screened a 1280x720 game on a 4K monitor -- you won't get the 4k
        // resolution!
        //
        // Additionally, the coordinates for the top left seem very weird in stacked displays (you
        // get a negative Y coordinate)
        int monitorX, monitorY, monitorWidth, monitorHeight;
        glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth, &monitorHeight);

        width = m_last_windowed_width;
        height = m_last_windowed_height;
        if (monitorX < 0) {
          x = monitorX - 50;
        } else {
          x = monitorX + 50;
        }
        if (monitorY < 0) {
          y = monitorY - 50;
        } else {
          y = monitorY + 50;
        }
        lg::debug("FS -> Windowed screen: {} - x:{}:{}/{} | y:{}:{}/{}", screen, monitorX, x, width,
                  monitorY, y, height);
      }

      glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_TRUE);
      glfwSetWindowFocusCallback(m_window, NULL);
      glfwSetWindowAttrib(m_window, GLFW_FLOATING, GLFW_FALSE);
      glfwSetWindowMonitor(m_window, NULL, x, y, width, height, GLFW_DONT_CARE);
    } break;
    case GfxDisplayMode::Fullscreen: {
      // TODO - when transitioning from fullscreen to windowed, it will use the old primary display
      // which is to say, dragging the window to a different monitor won't update the used display
      // fullscreen
      const GLFWvidmode* vmode = glfwGetVideoMode(monitor);
      glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_FALSE);
      glfwSetWindowFocusCallback(m_window, NULL);
      glfwSetWindowAttrib(m_window, GLFW_FLOATING, GLFW_FALSE);
      glfwSetWindowMonitor(m_window, monitor, 0, 0, vmode->width, vmode->height, GLFW_DONT_CARE);
    } break;
    case GfxDisplayMode::Borderless: {
      // TODO - when transitioning from fullscreen to windowed, it will use the old primary display
      // which is to say, dragging the window to a different monitor won't update the used display
      // borderless fullscreen
      int x, y;
      glfwGetMonitorPos(monitor, &x, &y);
      const GLFWvidmode* vmode = glfwGetVideoMode(monitor);
      glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_FALSE);
      // glfwSetWindowAttrib(m_window, GLFW_FLOATING, GLFW_TRUE);
      // glfwSetWindowFocusCallback(m_window, FocusCallback);
#ifdef _WIN32
      glfwSetWindowMonitor(m_window, NULL, x, y, vmode->width, vmode->height + 1, GLFW_DONT_CARE);
#else
      glfwSetWindowMonitor(m_window, NULL, x, y, vmode->width, vmode->height, GLFW_DONT_CARE);
#endif
    } break;
    default: {
      break;
    }
  }
}

int VkDisplay::get_screen_vmode_count() {
  std::lock_guard<std::mutex> lk(m_lock);
  return m_display_state.num_vmodes;
}

void VkDisplay::get_screen_size(int vmode_idx, s32* w_out, s32* h_out) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (vmode_idx >= 0 && vmode_idx < MAX_VMODES) {
    if (w_out) {
      *w_out = m_display_state.vmodes[vmode_idx].width;
    }
    if (h_out) {
      *h_out = m_display_state.vmodes[vmode_idx].height;
    }
  } else if (fullscreen_mode() == Fullscreen) {
    if (w_out) {
      *w_out = m_display_state.largest_vmode_width;
    }
    if (h_out) {
      *h_out = m_display_state.largest_vmode_height;
    }
  } else {
    if (w_out) {
      *w_out = m_display_state.current_vmode.width;
    }
    if (h_out) {
      *h_out = m_display_state.current_vmode.height;
    }
  }
}

int VkDisplay::get_screen_rate(int vmode_idx) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (vmode_idx >= 0 && vmode_idx < MAX_VMODES) {
    return m_display_state.vmodes[vmode_idx].refresh_rate;
  } else if (fullscreen_mode() == GfxDisplayMode::Fullscreen) {
    return m_display_state.largest_vmode_refresh_rate;
  } else {
    return m_display_state.current_vmode.refresh_rate;
  }
}

GLFWmonitor* VkDisplay::get_monitor(int index) {
  if (index < 0 || index >= g_glfw_state.monitor_count) {
    // out of bounds, default to primary monitor
    index = 0;
  }

  return g_glfw_state.monitors[index];
}

std::tuple<double, double> VkDisplay::get_mouse_pos() {
  return {last_cursor_x_position, last_cursor_y_position};
}

int VkDisplay::get_monitor_count() {
  return g_glfw_state.monitor_count;
}

bool VkDisplay::minimized() {
  return m_minimized;
}

void VkDisplay::set_lock(bool lock) {
  glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, lock ? GLFW_TRUE : GLFW_FALSE);
}

bool VkDisplay::fullscreen_pending() {
  GLFWmonitor* monitor;
  {
    auto _ = scoped_prof("get_monitor");
    monitor = get_monitor(fullscreen_screen());
  }

  const GLFWvidmode* vmode;
  {
    auto _ = scoped_prof("get-video-mode");
    vmode = glfwGetVideoMode(monitor);
  }

  return GfxDisplay::fullscreen_pending() ||
         (vmode->width != m_last_video_mode.width || vmode->height != m_last_video_mode.height ||
          vmode->refreshRate != m_last_video_mode.refreshRate);
}

void VkDisplay::fullscreen_flush() {
  GfxDisplay::fullscreen_flush();

  GLFWmonitor* monitor = get_monitor(fullscreen_screen());
  auto vmode = glfwGetVideoMode(monitor);

  m_last_video_mode = *vmode;
}

void VkDisplay::VMode::set(const GLFWvidmode* vmode) {
  width = vmode->width;
  height = vmode->height;
  refresh_rate = vmode->refreshRate;
}

void VkDisplay::update_glfw() {
  auto p = scoped_prof("update_glfw");
  glfwPollEvents();
  auto& mapping_info = Gfx::get_button_mapping();
  Pad::update_gamepads(mapping_info);
  glfwGetFramebufferSize(m_window, &m_display_state_copy.window_size_width,
                         &m_display_state_copy.window_size_height);
  glfwGetWindowContentScale(m_window, &m_display_state_copy.window_scale_x,
                            &m_display_state_copy.window_scale_y);
  glfwGetWindowPos(m_window, &m_display_state_copy.window_pos_x,
                   &m_display_state_copy.window_pos_y);
  GLFWmonitor* monitor = get_monitor(fullscreen_screen());
  auto current_vmode = glfwGetVideoMode(monitor);
  if (current_vmode) {
    m_display_state_copy.current_vmode.set(current_vmode);
  }
  int count = 0;
  auto vmodes = glfwGetVideoModes(monitor, &count);
  if (count > MAX_VMODES) {
    fmt::print("got too many vmodes: {}\n", count);
    count = MAX_VMODES;
  }
  m_display_state_copy.num_vmodes = count;
  m_display_state_copy.largest_vmode_width = 1;
  m_display_state_copy.largest_vmode_refresh_rate = 1;
  for (int i = 0; i < count; i++) {
    if (vmodes[i].width > m_display_state_copy.largest_vmode_width) {
      m_display_state_copy.largest_vmode_height = vmodes[i].height;
      m_display_state_copy.largest_vmode_width = vmodes[i].width;
    }
    if (vmodes[i].refreshRate > m_display_state_copy.largest_vmode_refresh_rate) {
      m_display_state_copy.largest_vmode_refresh_rate = vmodes[i].refreshRate;
    }
    m_display_state_copy.vmodes[i].set(&vmodes[i]);
  }
  if (m_pending_size.pending) {
    glfwSetWindowSize(m_window, m_pending_size.width, m_pending_size.height);
    m_pending_size.pending = false;
  }
  std::lock_guard<std::mutex> lk(m_lock);
  m_display_state = m_display_state_copy;
}

/*!
 * Main function called to render graphics frames. This is called in a loop.
 */
void VkDisplay::render() {
  update_glfw();

  // imgui start of frame
  {
    auto p = scoped_prof("imgui-init");
    m_imgui_helper.InitializeNewFrame();
  }

  // framebuffer size
  int fbuf_w, fbuf_h;
  glfwGetFramebufferSize(m_window, &fbuf_w, &fbuf_h);
  bool windows_borderless_hacks = false;
#ifdef _WIN32
  if (last_fullscreen_mode() == GfxDisplayMode::Borderless) {
    windows_borderless_hacks = true;
  }
#endif

  // render game!
  if (Gfx::debug_gui.should_advance_frame()) {
    auto p = scoped_prof("game-render");
    int game_res_w = Gfx::g_global_settings.game_res_w;
    int game_res_h = Gfx::g_global_settings.game_res_h;
    if (game_res_w <= 0 || game_res_h <= 0) {
      // if the window size reports 0, the game will ask for a 0 sized window, and nothing likes
      // that.
      game_res_w = 640;
      game_res_h = 480;
    }
    vulkan_render_game_frame(game_res_w, game_res_h, fbuf_w, fbuf_h, Gfx::g_global_settings.lbox_w,
                             Gfx::g_global_settings.lbox_h, Gfx::g_global_settings.msaa_samples,
                             windows_borderless_hacks);
  }

  // render debug
  if (is_imgui_visible()) {
    auto p = scoped_prof("debug-gui");
    Gfx::debug_gui.draw(g_gfx_data->dma_copier.get_last_result().stats);
  }
  {
    auto p = scoped_prof("imgui-render");
    m_imgui_helper.Render(Gfx::g_global_settings.game_res_w, Gfx::g_global_settings.game_res_h,
                          g_gfx_data->vulkan_renderer.GetSwapChain());
  }

  // actual vsync
  Gfx::debug_gui.finish_frame();
  {
    //auto p = scoped_prof("swap-buffers");
    //glfwSwapBuffers(m_window);
  }
  if (Gfx::g_global_settings.framelimiter) {
    auto p = scoped_prof("frame-limiter");
    g_gfx_data->frame_limiter.run(
        Gfx::g_global_settings.target_fps, Gfx::g_global_settings.experimental_accurate_lag,
        Gfx::g_global_settings.sleep_in_frame_limiter, g_gfx_data->last_engine_time);
  }
  // actually wait for vsync
  if (Gfx::debug_gui.should_gl_finish()) {
    //glFinish();
  }

  // Start timing for the next frame.
  Gfx::debug_gui.start_frame();
  prof().instant_event("ROOT");
  Gfx::update_global_profiler();

  // toggle even odd and wake up engine waiting on vsync.
  // TODO: we could play with moving this earlier, right after the final bucket renderer.
  //       it breaks the VIF-interrupt profiling though.
  {
    prof().instant_event("engine-notify");
    std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
    g_gfx_data->frame_idx++;
    g_gfx_data->sync_cv.notify_all();
  }

  // reboot whole game, if requested
  if (Gfx::debug_gui.want_reboot_in_debug) {
    Gfx::debug_gui.want_reboot_in_debug = false;
    MasterExit = RuntimeExitStatus::RESTART_IN_DEBUG;
  }

  {
    auto p = scoped_prof("check-close-window");
    // exit if display window was closed
    if (glfwWindowShouldClose(m_window)) {
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

void vk_poll_events() {
  glfwPollEvents();
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
    vk_poll_events,         // poll_events
    vk_set_levels,          // set_levels
    vk_set_pmode_alp,       // set_pmode_alp
    GfxPipeline::Vulkan,    // pipeline
    "Vulkan 1.0"            // name
};
