#pragma once

#include "common/goal_constants.h"

#ifdef USE_VULKAN
#include "third-party/glad/include/vulkan/vulkan.h"
#endif

#include "third-party/SDL/include/SDL.h"

namespace pipeline_common {
constexpr bool run_dma_copy = false;

constexpr PerGameVersion<int> fr3_level_count(jak1::LEVEL_TOTAL,
                                              jak2::LEVEL_TOTAL,
                                              jak3::LEVEL_TOTAL);
}  // namespace pipeline_common
