#pragma once

#include "GraphicsDeviceVulkan.h"

class VulkanTexture {
 public:
  VulkanTexture(std::shared_ptr<GraphicsDeviceVulkan> device);
  VulkanTexture(const VulkanTexture& image);
  void createImage(VkExtent3D extents,
                   uint32_t mipLevels,
                   VkImageType image_type,
                   VkFormat format,
                   VkImageTiling tiling,
                   VkImageUsageFlags usage,
                   VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);

  void createImage(VkExtent3D extents,
                   uint32_t mipLevels,
                   VkImageType image_type,
                   VkSampleCountFlagBits numSamples,
                   VkFormat format,
                   VkImageTiling tiling,
                   VkImageUsageFlags usage,
                   VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);

  void createImageView(VkImageViewType image_view_type,
                       VkFormat format,
                       VkImageAspectFlags aspectFlags,
                       uint32_t mipLevels);

  void writeToImage(void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void getImageData(VkBuffer buffer,
                    uint32_t width,
                    uint32_t height,
                    double x_offset,
                    double y_offset);
  VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void transitionImageLayout(VkImageLayout imageLayout, unsigned baseMipLevel = 0, unsigned levelCount = 1);

  void destroyTexture();
  VkFormat findDepthFormat();
  VkImage getImage() const { return m_image; };
  VkDeviceSize getMemorySize() const { return m_device_size; };
  VkImageView getImageView() const { return m_image_view; };
  uint32_t getWidth() const { return m_image_create_info.extent.width; };
  uint32_t getHeight() const { return m_image_create_info.extent.height; };
  uint32_t getDepth() const { return m_image_create_info.extent.depth; };
  std::shared_ptr<GraphicsDeviceVulkan> getLogicalDevice() { return m_device; };
  bool isInitialized() { return m_initialized; };
  VkSampleCountFlagBits getMsaaCount() const { return m_device->getMsaaCount(); }
  void generateMipmaps(VkFormat imageFormat, unsigned mipLevels) {
    m_device->GenerateMipmaps(m_image, imageFormat, m_image_create_info.extent.width,
                              m_image_create_info.extent.height, mipLevels);
  }
  void SetImageLayout(VkImageLayout imageLayout) {
    m_image_create_info.initialLayout = imageLayout;
  }

  unsigned long GetTextureId() { return m_image_id; }

  ~VulkanTexture() { destroyTexture(); };

 private:
  void AllocateVulkanImageMemory();

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VkImage m_image = VK_NULL_HANDLE;
  VkImageView m_image_view = VK_NULL_HANDLE;
  VkDeviceMemory m_device_memory = VK_NULL_HANDLE;
  VkDeviceSize m_device_size = 0;

  VkImageCreateInfo m_image_create_info{};
  VkImageViewCreateInfo m_image_view_create_info{};

  bool m_initialized = false;
  uint32_t m_image_id = 0;
};
