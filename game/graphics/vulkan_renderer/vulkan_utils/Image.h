#pragma once

#include "GraphicsDeviceVulkan.h"

class TextureInfo {
 public:
  TextureInfo(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device){};
  void CreateImage(VkExtent3D extents,
                   uint32_t mip_levels,
                   VkImageType image_type,
                   VkSampleCountFlagBits num_samples,
                   VkFormat format,
                   VkImageTiling tiling,
                   uint32_t usage,
                   uint32_t properties);

  void CreateImageView(VkImageViewType image_view_type,
                       VkFormat format,
                       VkImageAspectFlags aspectFlags,
                       uint32_t mipLevels);

  VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void unmap();

  void writeToBuffer(void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

  void writeToIndex(void* data, int index);
  VkResult flushIndex(int index);
  VkDescriptorBufferInfo descriptorInfoForIndex(int index);
  VkResult invalidateIndex(int index);

  void* getMappedMemory() const { return mapped_memory; }

  template <class T>
  void UpdateTexture(VkDeviceSize memory_offset, T* data, uint64_t element_count);
  uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

  void DestroyTexture();
  void CreateTextureSampler();
  VkFormat findDepthFormat();
  VkImage GetImage() { return m_image; };
  VkImageView GetImageView() { return m_image_view; };

  ~TextureInfo() { DestroyTexture(); };

  private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkImage m_image = VK_NULL_HANDLE; //TODO: Should this be a vector since image views can support multiple images?
  VkImageView m_image_view = VK_NULL_HANDLE;
  VkDeviceMemory m_device_memory = VK_NULL_HANDLE;
  VkDeviceSize m_device_size = 0;
  VkSamplerCreateInfo m_sampler_info;
  VkSampler m_sampler = VK_NULL_HANDLE;
  void* mapped_memory = nullptr;
  VkExtent3D m_extents;
};
