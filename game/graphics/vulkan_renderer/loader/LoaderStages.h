#pragma once

#include "game/graphics/vulkan_renderer/loader/common.h"

namespace vk_loader_stage {
std::vector<std::unique_ptr<LoaderStage>> make_loader_stages(
    std::unique_ptr<GraphicsDeviceVulkan>&);
void update_texture(TexturePool& pool, const tfrag3::Texture& tex, TextureInfo& texture_info, bool is_common);
}  // namespace vk_loader_stage

class MercLoaderStage : public LoaderStage {
 public:
  MercLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device);
  bool run(Timer& timer, LoaderInput& data) override;
  void reset() override;

 private:
  bool m_done = false;
  bool m_vulkan = false;
  bool m_vtx_uploaded = false;
  u32 m_idx = 0;
};