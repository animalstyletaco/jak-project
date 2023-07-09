/*!
 * @file gfx.cpp
 * Graphics component for the runtime. Abstraction layer for the main graphics routines.
 */

#include "gfx.h"

#include <cstdio>
#include <functional>
#include <utility>

#include "display.h"

#include "common/global_profiler/GlobalProfiler.h"
#include "common/log/log.h"
#include "common/symbols.h"
#include "common/util/FileUtil.h"
#include "common/util/json_util.h"
#include "common/global_profiler/GlobalProfiler.h"

#include "game/common/file_paths.h"
#include "game/kernel/common/kmachine.h"
#include "game/kernel/common/kscheme.h"
#include "game/runtime.h"


#include "pipelines/vulkan_pipeline.h"
extern const GfxRendererModule gRendererVulkan;

#include "pipelines/opengl.h"
extern const GfxRendererModule gRendererOpenGL;

namespace Gfx {

std::function<void()> vsync_callback;
GfxGlobalSettings g_global_settings;

game_settings::DebugSettings g_debug_settings;

const GfxRendererModule* GetRenderer(GfxPipeline pipeline) {
  switch (pipeline) {
    case GfxPipeline::Invalid:
      lg::error("Requested invalid renderer", fmt::underlying(pipeline));
      return NULL;
    case GfxPipeline::OpenGL:
      return &gRendererOpenGL;
    case GfxPipeline::Vulkan:
      return &gRendererVulkan;
    default:
      lg::error("Requested unknown renderer {}", fmt::underlying(pipeline));
      return NULL;
  }
}

void SetRenderer(GfxPipeline pipeline) {
  g_global_settings.renderer = GetRenderer(pipeline);
}

const GfxRendererModule* GetCurrentRenderer() {
  return g_global_settings.renderer;
}

u32 Init(GameVersion version) {
  lg::info("GFX Init");
  profiler::prof().instant_event("ROOT");

  g_debug_settings = game_settings::DebugSettings();
  {
    auto p = profiler::scoped_prof("startup::gfx::get_renderer");
    g_global_settings.renderer = GetRenderer(GfxPipeline::OpenGL);
  }

  {
    auto p = profiler::scoped_prof("startup::gfx::init_current_renderer");
    if (GetCurrentRenderer()->init(g_global_settings)) {
      lg::error("Gfx::Init error");
      return 1;
    }
  }

  if (g_main_thread_id != std::this_thread::get_id()) {
    lg::error("Ran Gfx::Init outside main thread. Init display elsewhere?");
  } else {
    {
      auto p = profiler::scoped_prof("startup::gfx::init_main_display");
      std::string title = "OpenGOAL";
      if (g_game_version == GameVersion::Jak2) {
        title += " - Work in Progress";
      }
      title += fmt::format(" - {} - {}", version_to_game_name_external(g_game_version),
                           build_revision());
      Display::InitMainDisplay(640, 480, title.c_str(), g_global_settings, version);
    }
  }

  return 0;
}

void Loop(std::function<bool()> f) {
  lg::info("GFX Loop");
  while (f()) {
    auto p = profiler::scoped_prof("gfx loop");
    // check if we have a display
    if (Display::GetMainDisplay()) {
      Display::GetMainDisplay()->render();
    }
  }
}

u32 Exit() {
  lg::info("GFX Exit");
  Display::KillMainDisplay();
  GetCurrentRenderer()->exit();
  g_debug_settings.save_settings();
  return 0;
}

void register_vsync_callback(std::function<void()> f) {
  vsync_callback = std::move(f);
}

void clear_vsync_callback() {
  vsync_callback = nullptr;
}

u32 vsync() {
  if (GetCurrentRenderer()) {
    // Inform the IOP kernel that we're vsyncing so it can run the vblank handler
    if (vsync_callback != nullptr)
      vsync_callback();
    return GetCurrentRenderer()->vsync();
  }
  return 0;
}

u32 sync_path() {
  if (GetCurrentRenderer()) {
    return GetCurrentRenderer()->sync_path();
  }
  return 0;
}

bool CollisionRendererGetMask(GfxGlobalSettings::CollisionRendererMode mode, int mask_id) {
  int arr_idx = mask_id / 32;
  int arr_ofs = mask_id % 32;

  switch (mode) {
    case GfxGlobalSettings::CollisionRendererMode::Mode:
      return (g_global_settings.collision_mode_mask[arr_idx] >> arr_ofs) & 1;
    case GfxGlobalSettings::CollisionRendererMode::Event:
      return (g_global_settings.collision_event_mask[arr_idx] >> arr_ofs) & 1;
    case GfxGlobalSettings::CollisionRendererMode::Material:
      return (g_global_settings.collision_material_mask[arr_idx] >> arr_ofs) & 1;
    case GfxGlobalSettings::CollisionRendererMode::Skip:
      ASSERT(arr_idx == 0);
      return (g_global_settings.collision_skip_mask >> arr_ofs) & 1;
    default:
      lg::error("{} invalid params {} {}", __PRETTY_FUNCTION__, fmt::underlying(mode), mask_id);
      return false;
  }
}

void CollisionRendererSetMask(GfxGlobalSettings::CollisionRendererMode mode, int mask_id) {
  int arr_idx = mask_id / 32;
  int arr_ofs = mask_id % 32;

  switch (mode) {
    case GfxGlobalSettings::CollisionRendererMode::Mode:
      g_global_settings.collision_mode_mask[arr_idx] |= 1 << arr_ofs;
      break;
    case GfxGlobalSettings::CollisionRendererMode::Event:
      g_global_settings.collision_event_mask[arr_idx] |= 1 << arr_ofs;
      break;
    case GfxGlobalSettings::CollisionRendererMode::Material:
      g_global_settings.collision_material_mask[arr_idx] |= 1 << arr_ofs;
      break;
    case GfxGlobalSettings::CollisionRendererMode::Skip:
      ASSERT(arr_idx == 0);
      g_global_settings.collision_skip_mask |= 1 << arr_ofs;
      break;
    default:
      lg::error("{} invalid params {} {}", __PRETTY_FUNCTION__, fmt::underlying(mode), mask_id);
      break;
  }
}

void CollisionRendererClearMask(GfxGlobalSettings::CollisionRendererMode mode, int mask_id) {
  int arr_idx = mask_id / 32;
  int arr_ofs = mask_id % 32;

  switch (mode) {
    case GfxGlobalSettings::CollisionRendererMode::Mode:
      g_global_settings.collision_mode_mask[arr_idx] &= ~(1 << arr_ofs);
      break;
    case GfxGlobalSettings::CollisionRendererMode::Event:
      g_global_settings.collision_event_mask[arr_idx] &= ~(1 << arr_ofs);
      break;
    case GfxGlobalSettings::CollisionRendererMode::Material:
      g_global_settings.collision_material_mask[arr_idx] &= ~(1 << arr_ofs);
      break;
    case GfxGlobalSettings::CollisionRendererMode::Skip:
      ASSERT(arr_idx == 0);
      g_global_settings.collision_skip_mask &= ~(1 << arr_ofs);
      break;
    default:
      lg::error("{} invalid params {} {}", __PRETTY_FUNCTION__, fmt::underlying(mode), mask_id);
      break;
  }
}

void CollisionRendererSetMode(GfxGlobalSettings::CollisionRendererMode mode) {
  g_global_settings.collision_mode = mode;
}

}  // namespace Gfx


void Gfx::update_global_profiler() {
  if (debug_gui.dump_events) {
    profiler::prof().set_enable(false);
    debug_gui.dump_events = false;

    auto dir_path = file_util::get_jak_project_dir() / "profile_data";
    fs::create_directories(dir_path);

    if (fs::exists(dir_path / "prof.json")) {
      int file_index = 1;
      auto file_path = dir_path / fmt::format("prof{}.json", file_index);
      while (!fs::exists(file_path)) {
        file_path = dir_path / fmt::format("prof{}.json", ++file_index);
      }
      profiler::prof().dump_to_json(file_path.string());
    } else {
      profiler::prof().dump_to_json((dir_path / "prof.json").string());
    }
  }
  profiler::prof().set_enable(debug_gui.record_events);
}

