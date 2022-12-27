#pragma once

#include <string>

struct RenderOptions {
  bool draw_render_debug_window = false;
  bool draw_profiler_window = false;
  bool draw_small_profiler_window = false;
  bool draw_subtitle_editor_window = false;

  // internal rendering settings - The OpenGLRenderer will internally use this resolution/format.
  int msaa_samples = 4;
  int game_res_w = 640;
  int game_res_h = 480;

  // size of the window's framebuffer (framebuffer 0)
  // The renderer needs to know this to do an optimization to render directly to the window's
  // framebuffer when possible.
  int window_framebuffer_height = 0;
  int window_framebuffer_width = 0;

  // the part of the window that we should draw to. The rest is black. This value is determined by
  // logic inside of the game - it needs to know the desired aspect ratio.
  int draw_region_height = 0;
  int draw_region_width = 0;

  // windows-specific tweaks to the size of the drawing area in borderless.
  bool borderless_windows_hacks = false;

  bool save_screenshot = false;
  std::string screenshot_path;

  float pmode_alp_register = 0.f;

  // when enabled, does a `glFinish()` after each major rendering pass. This blocks until the GPU
  // is done working, making it easier to profile GPU utilization.
  bool gpu_sync = false;
};
