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
  FramebufferVulkan(std::shared_ptr<GraphicsDeviceVulkan> device, VkFormat format);
  ~FramebufferVulkan();

  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;

  VulkanTexture m_multisample_texture;
  VulkanTexture m_color_texture;
  VulkanTexture m_depth_texture;

  VulkanSamplerHelper m_sampler_helper;
  VkExtent2D extents;

  void createFramebuffer();
  void initializeFramebufferAtLevel(VkSampleCountFlagBits samples, unsigned level);
  void beginRenderPass(VkCommandBuffer commandBuffer);
  void beginRenderPass(VkCommandBuffer commandBuffer,
                       std::vector<VkClearValue>&);
  void createRenderPass();
  VkSampleCountFlagBits m_current_msaa = VK_SAMPLE_COUNT_1_BIT;

 private:
  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VkFormat m_format;
};

class FramebufferVulkanHelper {
 public:
  FramebufferVulkanHelper(unsigned w,
                          unsigned h,
                          VkFormat format,
                          std::shared_ptr<GraphicsDeviceVulkan> device,
                          VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                          int num_levels = 1);

  void setViewportScissor(VkCommandBuffer command);
  VulkanTexture& ColorAttachmentTexture() { return m_framebuffer.m_color_texture; }
  VulkanTexture& DepthAttachmentTexture() { return m_framebuffer.m_depth_texture; }
  VulkanSamplerHelper& GetSamplerHelper() { return m_framebuffer.m_sampler_helper; }
  VkRenderPass GetRenderPass(unsigned index = 0) { return m_framebuffer.render_pass; }
  void beginRenderPass(VkCommandBuffer commandBuffer, unsigned mipmapLevel = 0);
  void beginRenderPass(VkCommandBuffer commandBuffer,
                       std::vector<VkClearValue>&,
                       unsigned mipmapLevel = 0);
  void GenerateMipmaps() { m_framebuffer.m_color_texture.generateMipmaps(m_format, m_mipmap_level); }
  void TransitionImageLayout(VkImageLayout imageLayout,
                             unsigned baseMipLevel = 0,
                             unsigned levelCount = 1) {
    m_framebuffer.m_color_texture.transitionImageLayout(imageLayout, baseMipLevel, levelCount);
  }
  VkSampleCountFlagBits GetCurrentSampleCount() { return m_framebuffer.m_current_msaa; }
  void initializeFramebufferAtLevel(VkSampleCountFlagBits samples, unsigned level) {
    m_framebuffer.initializeFramebufferAtLevel(samples, level);
  };

  FramebufferVulkanHelper(const FramebufferVulkanHelper&) = delete;
  FramebufferVulkanHelper& operator=(const FramebufferVulkanHelper&) = delete;

 private:
  VkExtent2D extents = {640, 480};
  VkOffset2D offsetExtents;

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  FramebufferVulkan m_framebuffer;

  uint32_t currentImageIndex = 0;
  bool isFrameStarted = false;
  uint32_t m_mipmap_level = 1;

  VkFormat m_format;
};

class FramebufferVulkanCopier {
 public:
  FramebufferVulkanCopier(std::shared_ptr<GraphicsDeviceVulkan> device, std::unique_ptr<SwapChain>& swapChain);
  ~FramebufferVulkanCopier();
  void copy_now(int render_fb_w,
                int render_fb_h,
                int render_fb_x,
                int render_fb_y,
                uint32_t swapChainImageIndex);
  VulkanTexture* Texture() { return &m_framebuffer_image; }
  VulkanSamplerHelper& Sampler() { return m_sampler_helper; }

  private:
  void createFramebufferImage();

  int m_fbo_width = 640, m_fbo_height = 480;
  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VulkanTexture m_framebuffer_image;
  VulkanSamplerHelper m_sampler_helper;
  std::unique_ptr<SwapChain>& m_swap_chain;
};
