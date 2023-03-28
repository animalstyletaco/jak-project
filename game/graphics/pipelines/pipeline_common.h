#pragma once

#include "common/goal_constants.h"

#ifdef USE_VULKAN
#include "third-party/glad/include/vulkan/vulkan.h"
#else
#define GLFW_INCLUDE_NONE
#include "third-party/glad/include/glad/glad.h"
#endif

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

namespace pipeline_common {
constexpr bool run_dma_copy = false;

constexpr PerGameVersion<int> fr3_level_count(jak1::LEVEL_TOTAL, jak2::LEVEL_TOTAL);
}  // namespace pipeline_common
