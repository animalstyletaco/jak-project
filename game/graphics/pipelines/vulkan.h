#pragma once

/*!
 * @file vulkan.h
 * Vulkan includes.
 */


#include "game/graphics/display.h"
#include "game/graphics/gfx.h"

#include "third-party/glad/include/vulkan/vulkan.h"

#define GLFW_INCLUDE_VULKAN
#include "third-party/glfw/include/GLFW/glfw3.h"

enum GlfwKeyAction {
  Release = GLFW_RELEASE,  // falling edge of key press
  Press = GLFW_PRESS,      // rising edge of key press
  Repeat = GLFW_REPEAT     // repeated input on hold e.g. when typing something
};

enum GlfwKeyCustomAxis {
  CURSOR_X_AXIS = GLFW_GAMEPAD_AXIS_LAST + 1,
  CURSOR_Y_AXIS = GLFW_GAMEPAD_AXIS_LAST + 2
};

class VkDisplay : public GfxDisplay {
 public:
  VkDisplay(GLFWwindow* window, bool is_main);
  virtual ~VkDisplay();

  void* get_window() const override { return m_window; }
  void get_position(int* x, int* y) override;
  void get_size(int* w, int* h) override;
  void get_scale(float* x, float* y) override;
  void get_screen_size(int vmode_idx, s32* w, s32* h) override;
  int get_screen_rate(int vmode_idx) override;
  int get_screen_vmode_count() override;
  int get_monitor_count() override;
  void set_size(int w, int h) override;
  void update_fullscreen(GfxDisplayMode mode, int screen) override;
  void render() override;
  bool minimized() override;
  bool fullscreen_pending() override;
  void fullscreen_flush() override;
  void set_lock(bool lock) override;
  void on_key(GLFWwindow* window, int key, int scancode, int action, int mods);
  void on_window_pos(GLFWwindow* window, int xpos, int ypos);
  void on_window_size(GLFWwindow* window, int width, int height);
  void on_iconify(GLFWwindow* window, int iconified);

 private:
  GLFWwindow* m_window;
  bool m_minimized = false;
  GLFWvidmode m_last_video_mode = {0, 0, 0, 0, 0, 0};

  GLFWmonitor* get_monitor(int index);
};

