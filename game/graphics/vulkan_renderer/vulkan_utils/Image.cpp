#include "Image.h"
#include <cassert>
#include <stdexcept>

void TextureInfo::CreateImage(VkExtent3D extents,
                              uint32_t mipLevels,
                              VkImageType image_type,
                              VkSampleCountFlagBits numSamples,
                              VkFormat format,
                              VkImageTiling tiling,
                              uint32_t usage,
                              uint32_t properties) {
  m_extents = extents;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = image_type;
  imageInfo.extent = extents;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = numSamples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_device->getLogicalDevice(), &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_device->getLogicalDevice(), m_image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device->findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(m_device->getLogicalDevice(), &allocInfo, nullptr, &m_device_memory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(m_device->getLogicalDevice(), m_image, m_device_memory, 0);
  m_initialized = true;
}

void TextureInfo::CreateImageView(VkImageViewType image_view_type,
                                  VkFormat format,
                                  VkImageAspectFlags aspectFlags,
                                  uint32_t mipLevels) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_image;
  viewInfo.viewType = image_view_type;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device->getLogicalDevice(), &viewInfo, nullptr, &m_image_view) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }
}

void TextureInfo::DestroyTexture() {
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

/**
 * Map a memory range of this buffer. If successful, mapped points to the specified buffer range.
 *
 * @param size (Optional) Size of the memory range to map. Pass VK_WHOLE_SIZE to map the complete
 * buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the buffer mapping call
 */
VkResult TextureInfo::map(VkDeviceSize size, VkDeviceSize offset) {
  assert(m_image && m_device_memory && "Called map on buffer before create");
  return vkMapMemory(m_device->getLogicalDevice(), m_device_memory, offset, size, 0, &mapped_memory);
}

/**
 * Unmap a mapped memory range
 *
 * @note Does not return a result as vkUnmapMemory can't fail
 */
void TextureInfo::unmap() {
  if (mapped_memory) {
    vkUnmapMemory(m_device->getLogicalDevice(), m_device_memory);
    mapped_memory = nullptr;
  }
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
void TextureInfo::writeToBuffer(void* data, VkDeviceSize size, VkDeviceSize offset) {
  assert(mapped_memory && "Cannot copy to unmapped buffer");

  VkDeviceSize image_size = m_extents.width * m_extents.height * m_extents.depth * sizeof(u32);

  if (size == VK_WHOLE_SIZE) {
    memcpy(mapped_memory, data, image_size);
  } else {
    char* memOffset = (char*)mapped_memory;
    memOffset += offset;
    memcpy(memOffset, data, size);
  }
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
VkResult TextureInfo::flush(VkDeviceSize size, VkDeviceSize offset) {
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
VkResult TextureInfo::invalidate(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = m_device_memory;
  mappedRange.offset = offset;
  mappedRange.size = size;
  return vkInvalidateMappedMemoryRanges(m_device->getLogicalDevice(), 1, &mappedRange);
}

VkFormat TextureInfo::findDepthFormat() {
  return m_device->findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool hasStencilComponent(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void TextureInfo::CreateTextureSampler() {
  if (m_sampler) {
    vkDestroySampler(m_device->getLogicalDevice(), m_sampler, nullptr);
    m_sampler = VK_NULL_HANDLE;
  }

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(m_device->getPhysicalDevice(), &properties);

  m_sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  m_sampler_info.anisotropyEnable = VK_TRUE;
  m_sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  m_sampler_info.unnormalizedCoordinates = VK_FALSE;
  m_sampler_info.minLod = 0.0f;
  // m_sampler_info.maxLod = static_cast<float>(mipLevels);
  m_sampler_info.mipLodBias = 0.0f;

  m_sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

  if (vkCreateSampler(m_device->getLogicalDevice(), &m_sampler_info, nullptr, &m_sampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}

/**
 * Create a buffer info descriptor
 *
 * @param size (Optional) Size of the memory range of the descriptor
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkDescriptorBufferInfo of specified offset and range
 */
VkDescriptorImageInfo TextureInfo::descriptorInfo(VkImageLayout image_layout) {
  return VkDescriptorImageInfo{
      m_sampler,
      m_image_view,
      image_layout
  };
}
