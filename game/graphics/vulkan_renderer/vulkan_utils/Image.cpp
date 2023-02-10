#include "Image.h"
#include "VulkanBuffer.h"

#include <cassert>
#include <stdexcept>

namespace vulkan_texture {
static unsigned long image_id = 0;
}

VulkanTexture::VulkanTexture(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device) {
  m_image_id = vulkan_texture::image_id++; 
}

VulkanTexture::VulkanTexture(const VulkanTexture& texture) : m_device(texture.m_device) {
  m_image_id = texture.m_image_id;
  m_image_create_info = texture.m_image_create_info;
  m_image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (texture.m_image) {
    AllocateVulkanImageMemory();
  }
  if (texture.m_image_create_info.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
    transitionImageLayout(texture.m_image_create_info.initialLayout);
  }

  m_image_view_create_info = texture.m_image_view_create_info;
  if (m_image_view) {
    if (vkCreateImageView(m_device->getLogicalDevice(), &m_image_view_create_info, nullptr,
                          &m_image_view) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture image view!");
    }
  }

  m_device_size = texture.m_device_size;
}


void VulkanTexture::AllocateVulkanImageMemory() {
  if (vkCreateImage(m_device->getLogicalDevice(), &m_image_create_info, nullptr, &m_image) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_device->getLogicalDevice(), m_image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      m_device->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_device->getLogicalDevice(), &allocInfo, nullptr, &m_device_memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(m_device->getLogicalDevice(), m_image, m_device_memory, 0);
  m_initialized = true;
}

void VulkanTexture::createImage(VkExtent3D extents,
                                uint32_t mipLevels,
                                VkImageType image_type,
                                VkFormat format,
                                VkImageTiling tiling,
                                VkImageUsageFlags usage,
                                VkImageLayout layout) {
  createImage(extents, mipLevels, image_type, m_device->getMsaaCount(), format, tiling, usage,
              layout);
}

void VulkanTexture::createImage(VkExtent3D extents,
                              uint32_t mipLevels,
                              VkImageType image_type,
                              VkSampleCountFlagBits numSamples,
                              VkFormat format,
                              VkImageTiling tiling,
                              VkImageUsageFlags usage,
                              VkImageLayout layout) {
  m_image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  m_image_create_info.imageType = image_type;
  m_image_create_info.extent = extents;
  m_image_create_info.mipLevels = mipLevels;
  m_image_create_info.arrayLayers = 1;
  m_image_create_info.format = format;
  m_image_create_info.tiling = tiling;
  m_image_create_info.initialLayout = layout;
  m_image_create_info.usage = usage;
  m_image_create_info.samples = (numSamples > m_device->GetMaxUsableSampleCount()) ? m_device->GetMaxUsableSampleCount() : numSamples;
  m_image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  AllocateVulkanImageMemory();
}

void VulkanTexture::createImageView(VkImageViewType image_view_type,
                                  VkFormat format,
                                  VkImageAspectFlags aspectFlags,
                                  uint32_t mipLevels) {
  m_image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  m_image_view_create_info.image = m_image;
  m_image_view_create_info.viewType = image_view_type;
  m_image_view_create_info.format = format;
  m_image_view_create_info.subresourceRange.aspectMask = aspectFlags;
  m_image_view_create_info.subresourceRange.baseMipLevel = 0;
  m_image_view_create_info.subresourceRange.levelCount = mipLevels;
  m_image_view_create_info.subresourceRange.baseArrayLayer = 0;
  m_image_view_create_info.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device->getLogicalDevice(), &m_image_view_create_info, nullptr,
                        &m_image_view) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }
}

void VulkanTexture::destroyTexture() {
  if (m_image_view) {
    vkDestroyImageView(m_device->getLogicalDevice(), m_image_view, nullptr);
    m_image_view = VK_NULL_HANDLE;
  }
  if (m_image) {
    vkDestroyImage(m_device->getLogicalDevice(), m_image, nullptr);
    m_image_view = VK_NULL_HANDLE;
  }
  if (m_device_memory) {
    vkFreeMemory(m_device->getLogicalDevice(), m_device_memory, nullptr);
    m_device_memory = VK_NULL_HANDLE;
  }
};

void VulkanTexture::transitionImageLayout(VkImageLayout imageLayout) {
  m_device->transitionImageLayout(m_image, m_image_create_info.format,
                                  m_image_create_info.initialLayout,
                                  imageLayout);
  m_image_create_info.initialLayout = imageLayout; //TODO: Should there be a separate variable to keep track of image layout is
}

/**
 * Copies the specified data to the mapped buffer. Default value writes whole buffer range
 *
 * @param data Pointer to the data to copy
 * @param size (Optional) Size of the data to copy. Pass VK_WHOLE_SIZE to flush the complete buffer
 * range.
 * @param offset (Optional) Byte offset from beginning of mapped region
 *
 */
void VulkanTexture::writeToImage(void* data, VkDeviceSize size, VkDeviceSize offset) {
  transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  StagingBuffer stagingBuffer(m_device,
                              m_image_create_info.extent.width * m_image_create_info.extent.height *
                                  m_image_create_info.extent.depth * 4, 1,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

  stagingBuffer.map();
  stagingBuffer.writeToCpuBuffer(data);

  m_device->copyBufferToImage(stagingBuffer.getBuffer(), m_image, m_image_create_info.extent.width, m_image_create_info.extent.height,
                              0, 0, 1);
  stagingBuffer.unmap();
  transitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

/**
 * Copies the specified data to the mapped buffer. Default value writes whole buffer range
 *
 * @param data Pointer to the data to copy
 * @param size (Optional) Size of the data to copy. Pass VK_WHOLE_SIZE to flush the complete buffer
 * range.
 * @param offset (Optional) Byte offset from beginning of mapped region
 *
 */
void VulkanTexture::getImageData(VkBuffer buffer, uint32_t width, uint32_t height, double x_offset, double y_offset) {
  m_device->copyImageToBuffer(m_image, width, height, x_offset, y_offset, 1, buffer);
}

/**
 * Flush a memory range of the buffer to make it visible to the device
 *
 * @note Only required for non-coherent memory
 *
 * @param size (Optional) Size of the memory range to flush. Pass VK_WHOLE_SIZE to flush the
 * complete buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the flush call
 */
VkResult VulkanTexture::flush(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = m_device_memory;
  mappedRange.offset = offset;
  mappedRange.size = size;
  return vkFlushMappedMemoryRanges(m_device->getLogicalDevice(), 1, &mappedRange);
}

/**
 * Invalidate a memory range of the buffer to make it visible to the host
 *
 * @note Only required for non-coherent memory
 *
 * @param size (Optional) Size of the memory range to invalidate. Pass VK_WHOLE_SIZE to invalidate
 * the complete buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the invalidate call
 */
VkResult VulkanTexture::invalidate(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = m_device_memory;
  mappedRange.offset = offset;
  mappedRange.size = size;
  return vkInvalidateMappedMemoryRanges(m_device->getLogicalDevice(), 1, &mappedRange);
}

VkFormat VulkanTexture::findDepthFormat() {
  return m_device->findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool hasStencilComponent(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


