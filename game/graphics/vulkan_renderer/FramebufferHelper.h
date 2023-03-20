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

//TODO: Come up with better framebuffer name
class FramebufferVulkan {
 public:
  FramebufferVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VkFormat format);
  ~FramebufferVulkan();

  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFramebuffer frame_buffer = VK_NULL_HANDLE;

  VulkanTexture color_texture;
  VulkanTexture depth_texture;
  VulkanTexture mipmap_texture;
  
  VulkanSamplerHelper sampler_helper;

  VkExtent2D extents;
  void setViewportScissor(VkCommandBuffer);
  VkFormat GetSupportedDepthFormat();

  void createRenderPass();
  void createFramebuffer();
  void initializeFramebufferAtLevel(int level = 0);

  VkSampleCountFlags m_current_msaa = VK_SAMPLE_COUNT_1_BIT;
  uint32_t GetMipmapLevel(int level);

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkFormat m_format;
};

class FramebufferVulkanHelper {
 public:
  FramebufferVulkanHelper(unsigned w,
                          unsigned h,
                          VkFormat format,
                          std::unique_ptr<GraphicsDeviceVulkan>& device,
                          int num_levels = 1);

  void setViewportScissor(VkCommandBuffer command, int level = 0) {
    m_framebuffers[level].setViewportScissor(command);
  }
  VulkanTexture& Texture(int level = 0) { return m_framebuffers[level].color_texture; }
  VulkanSamplerHelper& GetSamplerHelper(int level = 0) {
    return m_framebuffers[level].sampler_helper;
  }
  VkRenderPass GetRenderPass(int level = 0) { return m_framebuffers[level].render_pass; }

  FramebufferVulkanHelper(const FramebufferVulkanHelper&) = delete;
  FramebufferVulkanHelper& operator=(const FramebufferVulkanHelper&) = delete;

  void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, int level = 0);

 private:
  VkExtent2D extents = {640, 480};
  VkOffset2D offsetExtents;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::vector<FramebufferVulkan> m_framebuffers;

  uint32_t currentImageIndex = 0;
  bool isFrameStarted = false;

  VkFormat m_format;
};


