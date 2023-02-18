#pragma once

#include "common/math/Vector.h"
#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SamplerHelper.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/DescriptorLayout.h"

class ScopedProfilerNode;

/*!
 * This is a wrapper around a framebuffer and texture to make it easier to render to a texture.
 */
class FramebufferVulkanTexturePair {
 public:
  FramebufferVulkanTexturePair(unsigned w,
                               unsigned h,
                               VkFormat format,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               int num_levels = 1);

  VulkanTexture& Texture(int level) { return m_textures[level]; }
  VulkanSamplerHelper& GetSamplerHelper() { return m_sampler_helper; }


  FramebufferVulkanTexturePair(const FramebufferVulkanTexturePair&) = delete;
  FramebufferVulkanTexturePair& operator=(const FramebufferVulkanTexturePair&) = delete;

 private:
  VkExtent2D extents = {640, 480};
  VkOffset2D offsetExtents;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;

  VulkanSamplerHelper m_sampler_helper;

  // This is ok since it's only set once. Only need to avoid std::vector<VulkanTexture> if the container is
  // expected to add or remove elements
  std::vector<VulkanTexture> m_textures;

  uint32_t currentImageIndex = 0;
  bool isFrameStarted = false;
};


