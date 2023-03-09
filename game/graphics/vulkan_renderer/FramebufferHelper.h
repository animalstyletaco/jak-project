#pragma once

#include "common/math/Vector.h"
#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SamplerHelper.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/DescriptorLayout.h"

/*!
 * This is a wrapper around a framebuffer and texture to make it easier to render to a texture.
 */
class FramebufferVulkanHelper {
 public:
  FramebufferVulkanHelper(unsigned w,
                          unsigned h,
                          VkFormat format,
                          std::unique_ptr<GraphicsDeviceVulkan>& device,
                          int num_levels = 1);

  VulkanTexture& Texture(int level) { return m_color_textures[level]; }
  VulkanSamplerHelper& GetSamplerHelper() { return m_sampler_helper; }

  FramebufferVulkanHelper(const FramebufferVulkanHelper&) = delete;
  FramebufferVulkanHelper& operator=(const FramebufferVulkanHelper&) = delete;

  void createRenderPass(VkFormat);
  void createFramebuffer();

 private:
  VkFormat GetSupportedDepthFormat();

  VkExtent2D extents = {640, 480};
  VkOffset2D offsetExtents;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkRenderPass m_render_pass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> m_frame_buffers;

  VulkanSamplerHelper m_sampler_helper;

  // This is ok since it's only set once. Only need to avoid std::vector<VulkanTexture> if the container is
  // expected to add or remove elements
  std::vector<VulkanTexture> m_color_textures;
  std::vector<VulkanTexture> m_depth_textures;

  uint32_t currentImageIndex = 0;
  bool isFrameStarted = false;
};


