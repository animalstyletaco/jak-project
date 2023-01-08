#include "game/graphics/vulkan_renderer/loader/Loader.h"

#include "common/util/FileUtil.h"
#include "common/util/Timer.h"
#include "common/util/compress.h"

#include "game/graphics/vulkan_renderer/loader/LoaderStages.h"

VulkanLoader::VulkanLoader(std::unique_ptr<GraphicsDeviceVulkan>& device, const fs::path& base_path, int max_levels)
    : BaseLoader(base_path, max_levels), m_device(device) {
  m_loader_thread = std::thread(&VulkanLoader::loader_thread, this);
  m_loader_stages = vk_loader_stage::make_loader_stages(m_device);
}

VulkanLoader::~VulkanLoader() {
  {
    std::lock_guard<std::mutex> lk(m_loader_mutex);
    m_want_shutdown = true;
    m_loader_cv.notify_all();
  }
  m_loader_thread.join();
}

/*!
 * Try to get a loaded level by name. It may fail and return nullptr.
 * Getting a level will reset the counter for the level and prevent it from being kicked out
 * for a little while.
 *
 * This is safe to call from the graphics thread
 */
LevelDataVulkan* VulkanLoader::get_tfrag3_level(const std::string& level_name) {
  std::unique_lock<std::mutex> lk(m_loader_mutex);
  const auto& existing = m_loaded_tfrag3_levels.find(level_name);
  if (existing == m_loaded_tfrag3_levels.end()) {
    return nullptr;
  } else {
    existing->second->frames_since_last_used = 0;
    return existing->second.get();
  }
}

/*!
 * The game calls this to give the loader a hint on which levels we want.
 * If the loader is not busy, it will begin loading the level.
 * This should be called on every frame.
 */
void VulkanLoader::set_want_levels(const std::vector<std::string>& levels) {
  std::unique_lock<std::mutex> lk(m_loader_mutex);
  m_desired_levels = levels;
  if (!m_level_to_load.empty()) {
    // can't do anything, we're loading a level right now
    return;
  }

  if (!m_initializing_tfrag3_levels.empty()) {
    // can't do anything, we're initializing a level right now
    return;
  }

  // loader isn't busy, try to load one of the requested levels.
  for (auto& lev : levels) {
    auto it = m_loaded_tfrag3_levels.find(lev);
    if (it == m_loaded_tfrag3_levels.end()) {
      // we haven't loaded it yet. Request this level to load and wake up the thread.
      m_level_to_load = lev;
      lk.unlock();
      m_loader_cv.notify_all();
      return;
    }
  }
}

/*!
 * Get all levels that are in memory and used very recently.
 */
std::vector<LevelDataVulkan*> VulkanLoader::get_in_use_levels() {
  std::vector<LevelDataVulkan*> result;
  std::unique_lock<std::mutex> lk(m_loader_mutex);

  for (auto& [level_name, level_data] : m_loaded_tfrag3_levels) {
    if (level_data->frames_since_last_used < 5) {
      result.push_back(level_data.get());
    }
  }
  return result;
}

/*!
 * Load a "common" FR3 file that has non-level textures.
 * This should be called during initialization, before any threaded loading goes on.
 */
void VulkanLoader::load_common(VulkanTexturePool& tex_pool, const std::string& name) {
  auto data = file_util::read_binary_file(m_base_path / fmt::format("{}.fr3", name));

  auto decomp_data = compression::decompress_zstd(data.data(), data.size());
  Serializer ser(decomp_data.data(), decomp_data.size());
  m_common_level.level = std::make_unique<tfrag3::Level>();
  m_common_level.level->serialize(ser);
  m_common_level.textures.resize(m_common_level.level->textures.size(), VulkanTexture{m_device});
  for (size_t i = 0; i < m_common_level.level->textures.size(); i++) {
    vk_loader_stage::update_texture(tex_pool, m_common_level.level->textures.at(i), m_common_level.textures[i], true);
  }

  Timer tim;
  MercVulkanLoaderStage mls{m_device};
  LoaderInputVulkan input;
  input.tex_pool = &tex_pool;
  input.mercs = &m_all_merc_models;
  input.lev_data = &m_common_level;
  bool done = false;
  while (!done) {
    done = mls.run(tim, input);
  }
}

bool VulkanLoader::upload_textures(Timer& timer, LevelDataVulkan& data, VulkanTexturePool& texture_pool) {
  // try to move level from initializing to initialized:

  constexpr int MAX_TEX_BYTES_PER_FRAME = 1024 * 128;

  int bytes_this_run = 0;
  int tex_this_run = 0;
  if (data.textures.size() < data.level->textures.size()) {
    std::unique_lock<std::mutex> tpool_lock(texture_pool.mutex());
    while (data.textures.size() < data.level->textures.size()) {
      auto& level_texture = data.level->textures[data.textures.size()];
      VulkanTexture texture_to_be_loaded{m_device};
      vk_loader_stage::update_texture(texture_pool, level_texture, texture_to_be_loaded, false);

      data.textures.push_back(texture_to_be_loaded);
      bytes_this_run += level_texture.w * level_texture.h * 4;
      tex_this_run++;
      if (tex_this_run > 20) {
        break;
      }
      if (bytes_this_run > MAX_TEX_BYTES_PER_FRAME || timer.getMs() > SHARED_TEXTURE_LOAD_BUDGET) {
        break;
      }
    }
  }
  return data.textures.size() == data.level->textures.size();
}

void VulkanLoader::update_blocking(VulkanTexturePool& tex_pool) {
  fmt::print("NOTE: coming out of blackout on next frame, doing all loads now...\n");

  bool missing_levels = true;
  while (missing_levels) {
    bool needs_run = true;

    while (needs_run) {
      needs_run = false;
      {
        std::unique_lock<std::mutex> lk(m_loader_mutex);
        if (!m_level_to_load.empty()) {
          m_file_load_done_cv.wait(lk, [&]() { return m_level_to_load.empty(); });
        }
      }
    }

    needs_run = true;

    while (needs_run) {
      needs_run = false;
      {
        std::unique_lock<std::mutex> lk(m_loader_mutex);
        if (!m_initializing_tfrag3_levels.empty()) {
          needs_run = true;
        }
      }

      if (needs_run) {
        update(tex_pool);
      }
    }

    {
      std::unique_lock<std::mutex> lk(m_loader_mutex);
      missing_levels = false;
      for (auto& des : m_desired_levels) {
        if (m_loaded_tfrag3_levels.find(des) == m_loaded_tfrag3_levels.end()) {
          fmt::print("blackout loader doing additional level {}...\n", des);
          missing_levels = true;
        }
      }
    }

    if (missing_levels) {
      set_want_levels(m_desired_levels);
    }
  }

  fmt::print("Blackout loads done. Current status:");
  std::unique_lock<std::mutex> lk(m_loader_mutex);
  for (auto& ld : m_loaded_tfrag3_levels) {
    fmt::print("  {} is loaded.\n", ld.first);
  }
}

/*!
 * Loader function that runs in a completely separate thread.
 * This is used for file I/O and unpacking.
 */
void VulkanLoader::loader_thread() {
  while (!m_want_shutdown) {
    std::unique_lock<std::mutex> lk(m_loader_mutex);

    // this will keep us asleep until we've got a level to load.
    m_loader_cv.wait(lk, [&] { return !m_level_to_load.empty() || m_want_shutdown; });
    if (m_want_shutdown) {
      return;
    }
    std::string lev = m_level_to_load;
    // don't hold the lock while reading the file.
    lk.unlock();

    // simulate slower hard drive (so that the loader thread can lose to the game loads)
    // std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // load the fr3 file
    Timer disk_timer;
    auto data =
        file_util::read_binary_file(m_base_path / fmt::format("{}.fr3", uppercase_string(lev)));
    double disk_load_time = disk_timer.getSeconds();

    // the FR3 files are compressed
    Timer decomp_timer;
    auto decomp_data = compression::decompress_zstd(data.data(), data.size());
    double decomp_time = decomp_timer.getSeconds();

    // Read back into the tfrag3::Level structure
    Timer import_timer;
    auto result = std::make_unique<tfrag3::Level>();
    Serializer ser(decomp_data.data(), decomp_data.size());
    result->serialize(ser);
    double import_time = import_timer.getSeconds();

    // and finally "unpack", which creates the vertex data we'll upload to the GPU
    Timer unpack_timer;
    for (auto& tie_tree : result->tie_trees) {
      for (auto& tree : tie_tree) {
        tree.unpack();
      }
    }
    for (auto& t_tree : result->tfrag_trees) {
      for (auto& tree : t_tree) {
        tree.unpack();
      }
    }

    for (auto& shrub_tree : result->shrub_trees) {
      shrub_tree.unpack();
    }
    fmt::print(
        "------------> Load from file: {:.3f}s, import {:.3f}s, decomp {:.3f}s unpack {:.3f}s\n",
        disk_load_time, import_time, decomp_time, unpack_timer.getSeconds());

    // grab the lock again
    lk.lock();
    // move this level to "initializing" state.
    m_initializing_tfrag3_levels[lev] = std::make_unique<LevelDataVulkan>();  // reset load state
    m_initializing_tfrag3_levels[lev]->level = std::move(result);
    m_level_to_load = "";
    m_file_load_done_cv.notify_all();
  }
}


void VulkanLoader::update(VulkanTexturePool& texture_pool) {
  Timer loader_timer;

  // only main thread can touch this.
  for (auto& lev : m_loaded_tfrag3_levels) {
    lev.second->frames_since_last_used++;
  }

  bool did_gpu_stuff = false;

  // work on moving initializing to initialized.
  {
    // accessing initializing, should lock
    std::unique_lock<std::mutex> lk(m_loader_mutex);
    // grab the first initializing level:
    const auto& it = m_initializing_tfrag3_levels.begin();
    if (it != m_initializing_tfrag3_levels.end()) {
      did_gpu_stuff = true;
      std::string name = it->first;
      auto& lev = it->second;
      if (it->second->load_id == UINT64_MAX) {
        it->second->load_id = m_id++;
      }

      // we're the only place that erases, so it's okay to unlock and hold a reference
      lk.unlock();
      bool done = true;
      LoaderInputVulkan loader_input;
      loader_input.lev_data = lev.get();
      loader_input.mercs = &m_all_merc_models;
      loader_input.tex_pool = &texture_pool;

      for (auto& stage : m_loader_stages) {
        Timer stage_timer;
        done = stage->run(loader_timer, loader_input);
        if (stage_timer.getMs() > 5.f) {
          fmt::print("stage {} took {:.2f} ms\n", stage->name(), stage_timer.getMs());
        }
        if (!done) {
          break;
        }
      }

      if (done) {
        lk.lock();
        m_loaded_tfrag3_levels[name] = std::move(lev);
        m_initializing_tfrag3_levels.erase(it);

        for (auto& stage : m_loader_stages) {
          stage->reset();
        }
      }
    }
  }

  if (!did_gpu_stuff) {
    // try to remove levels.
    Timer unload_timer;
    if (m_loaded_tfrag3_levels.size() > MAX_LEVELS_LOADED) {
      for (auto& [level_name, level_data] : m_loaded_tfrag3_levels) {
        if (level_data->frames_since_last_used > 180) {
          std::unique_lock<std::mutex> lk(texture_pool.mutex());
          fmt::print("------------------------- PC unloading {}\n", level_name);
          for (size_t i = 0; i < level_data->level->textures.size(); i++) {
            auto& tex = level_data->level->textures[i];
            if (tex.load_to_pool) {
              texture_pool.unload_texture(PcTextureId::from_combo_id(tex.combo_id),
                                          level_data->textures.at(i).GetTextureId());
            }
          }
          lk.unlock();
          for (auto& tex : level_data->textures) {
            tex.destroyTexture();
          }

          for (auto& tie_geo : level_data->tie_data) {
            for (auto& tie_tree : tie_geo) {
              //glDeleteBuffers(1, &tie_tree.vertex_buffer);
              if (tie_tree.has_wind) {
                //glDeleteBuffers(1, &tie_tree.wind_indices);
              }
            }
          }

          for (auto& tfrag_geo : level_data->tfrag_vertex_data) {
            for (auto& tfrag_buff : tfrag_geo) {
              //glDeleteBuffers(1, &tfrag_buff);
            }
          }

          //glDeleteBuffers(1, &lev.second->collide_vertices);
          //glDeleteBuffers(1, &lev.second->merc_vertices);
          //glDeleteBuffers(1, &lev.second->merc_indices);

          for (auto& model : level_data->level->merc_data.models) {
            auto& mercs = m_all_merc_models.at(model.name);
            MercRefVulkan ref{&model, level_data->load_id, NULL};
            auto it = std::find(mercs.begin(), mercs.end(), ref);
            ASSERT_MSG(it != mercs.end(), fmt::format("missing merc: {}\n", model.name));
            mercs.erase(it);
          }

          m_loaded_tfrag3_levels.erase(level_name);
          break;
        }
      }
    }

    if (unload_timer.getMs() > 5.f) {
      fmt::print("Unload took {:.2f}\n", unload_timer.getMs());
    }
  }

  if (loader_timer.getMs() > 5) {
    fmt::print("VulkanLoader::update slow setup: {:.1f}ms\n", loader_timer.getMs());
  }
}

std::optional<MercRefVulkan> VulkanLoader::get_merc_model(const char* model_name) {
  // don't think we need to lock here...
  const auto& it = m_all_merc_models.find(model_name);
  if (it != m_all_merc_models.end() && !it->second.empty()) {
    // it->second.front().parent_level->frames_since_last_used = 0;
    return it->second.front();
  } else {
    return std::nullopt;
  }
}
