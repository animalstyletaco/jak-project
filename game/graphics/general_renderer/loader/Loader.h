#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "common/custom_data/Tfrag3Data.h"
#include "common/util/FileUtil.h"
#include "common/util/Timer.h"

#include "game/graphics/general_renderer/loader/common.h"

class BaseLoader {
 public:
  static constexpr float TIE_LOAD_BUDGET = 1.5f;
  static constexpr float SHARED_TEXTURE_LOAD_BUDGET = 3.f;
  static constexpr unsigned MAX_LEVELS_LOADED = 3;
  BaseLoader(const fs::path& base_path, int max_levels) : m_base_path(base_path), m_max_levels(max_levels){};
  virtual ~BaseLoader() = default;
  virtual void set_want_levels(const std::vector<std::string>& levels) = 0;
  virtual void set_active_levels(const std::vector<std::string>& levels) = 0;

  std::string uppercase_string(const std::string& s) {
    std::string result;
    for (auto c : s) {
      result.push_back(toupper(c));
    }
    return result;
  }

 protected:
  // used by game and loader thread
  std::string m_level_to_load;

  std::thread m_loader_thread;
  std::mutex m_loader_mutex;
  std::condition_variable m_loader_cv;
  std::condition_variable m_file_load_done_cv;
  bool m_want_shutdown = false;
  uint64_t m_id = 0;

  // used only by game thread
  std::vector<std::string> m_desired_levels;

  fs::path m_base_path;
  int m_max_levels = 0;
};
