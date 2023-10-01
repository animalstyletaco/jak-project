#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "common/custom_data/Tfrag3Data.h"
#include "common/util/FileUtil.h"
#include "common/util/Timer.h"

#include "game/graphics/vulkan_renderer/loader/common.h"
#include "game/graphics/general_renderer/loader/Loader.h"
#include "game/graphics/texture/VulkanTexturePool.h"

class VulkanLoader : public BaseLoader {
 public:
  VulkanLoader(std::shared_ptr<GraphicsDeviceVulkan> device, const fs::path& base_path, int max_levels);
  ~VulkanLoader();
  void update(VulkanTexturePool& tex_pool);
  void update_blocking(VulkanTexturePool& tex_pool);
  void load_common(VulkanTexturePool& tex_pool, const std::string& name);
  void set_want_levels(const std::vector<std::string>& levels);
  LevelDataVulkan* get_tfrag3_level(const std::string& level_name);
  void draw_debug_window();
  std::optional<MercRefVulkan> get_merc_model(const char* model_name);
  std::vector<LevelDataVulkan*> get_in_use_levels();
  bool upload_textures(Timer& timer,
                       LevelDataVulkan& data,
                       VulkanTexturePool& texture_pool);

 private:
  void loader_thread();

  const std::string* get_most_unloadable_level();
  // used only by game thread
  std::unordered_map<std::string, std::unique_ptr<LevelDataVulkan>> m_loaded_tfrag3_levels;
  std::unordered_map<std::string, std::unique_ptr<LevelDataVulkan>> m_initializing_tfrag3_levels;

  std::unordered_map<std::string, std::vector<MercRefVulkan>> m_all_merc_models;
  std::vector<std::unique_ptr<LoaderStageVulkan>> m_loader_stages;

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  LevelDataVulkan m_common_level;
};
