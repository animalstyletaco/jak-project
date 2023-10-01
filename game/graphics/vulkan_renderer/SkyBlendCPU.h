#pragma once

#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/SkyBlendCommon.h"
#include "game/graphics/general_renderer/SkyBlendCPU.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class SkyBlendCPU : public BaseSkyBlendCPU {
 public:
  SkyBlendCPU(std::shared_ptr<GraphicsDeviceVulkan> device, VulkanInitializationInfo& vulkan_info);
  ~SkyBlendCPU();

  void init_textures(VulkanTexturePool& tex_pool);

 private:
  void setup_gpu_texture(u32, bool, u32, u32, int, SkyBlendStats&) override;
  static constexpr int m_sizes[2] = {32, 64};

  struct TexInfo {
    std::unique_ptr<VulkanTexture> texture;
    u32 tbp;
    VulkanGpuTextureMap* tex;
  } m_textures[2];

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VulkanInitializationInfo& m_vulkan_info;
};
