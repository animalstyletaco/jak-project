#pragma once

#include "game/graphics/vulkan_renderer/loader/common.h"
#include "game/graphics/texture/VulkanTexturePool.h"

namespace vk_loader_stage {
std::vector<std::unique_ptr<LoaderStageVulkan>> make_loader_stages(
    std::unique_ptr<GraphicsDeviceVulkan>&);
void update_texture(VulkanTexturePool& pool, const tfrag3::Texture& tex, VulkanTexture& texture_info, bool is_common);
}  // namespace vk_loader_stage

class MercVulkanLoaderStage : public LoaderStageVulkan {
 public:
  MercVulkanLoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device);
  bool run(Timer& timer, LoaderInputVulkan& data) override;
  void reset() override;

 private:
  bool m_done = false;
  bool m_vulkan = false;
  bool m_vtx_uploaded = false;
  u32 m_idx = 0;
};
