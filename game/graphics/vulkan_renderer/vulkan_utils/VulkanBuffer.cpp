#include "VulkanBuffer.h"
#include <cassert>

/**
 * Returns the minimum instance size required to be compatible with devices minOffsetAlignment
 *
 * @param instanceSize The size of an instance
 * @param minOffsetAlignment The minimum required alignment, in bytes, for the offset member (eg
 * minUniformBufferOffsetAlignment)
 *
 * @return VkResult of the buffer mapping call
 */
VkDeviceSize VulkanBuffer::getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
  if (minOffsetAlignment > 0) {
    return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
  }
  return instanceSize;
}

VulkanBuffer::VulkanBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VkDeviceSize instanceSize,
                           uint32_t instanceCount,
                           VkBufferUsageFlags usageFlags,
                           VkMemoryPropertyFlags memoryPropertyFlags,
                           VkDeviceSize minOffsetAlignment)
    : m_device{device},
      instanceSize{instanceSize},
      instanceCount{instanceCount},
      minOffsetAlignment {minOffsetAlignment},
      usageFlags{usageFlags},
      memoryPropertyFlags{memoryPropertyFlags} {
  alignmentSize = getAlignment(instanceSize, minOffsetAlignment);
  bufferSize = alignmentSize * instanceCount;
  createBuffer(bufferSize, usageFlags, memoryPropertyFlags, buffer, memory);
}

VulkanBuffer::VulkanBuffer(const VulkanBuffer& original_buffer)
    : m_device{original_buffer.m_device},
      instanceSize{original_buffer.instanceSize},
      instanceCount{original_buffer.instanceCount},
      minOffsetAlignment{original_buffer.minOffsetAlignment},
      usageFlags{original_buffer.usageFlags},
      memoryPropertyFlags{original_buffer.memoryPropertyFlags} {
  alignmentSize = getAlignment(instanceSize, minOffsetAlignment);
  bufferSize = alignmentSize * instanceCount;
  createBuffer(bufferSize, usageFlags, memoryPropertyFlags, buffer, memory);

  StagingBuffer stagingBuffer(m_device, instanceSize, instanceCount,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT, alignmentSize);
  m_device->copyBuffer(original_buffer.buffer, stagingBuffer.getBuffer(), instanceCount * instanceSize);
  stagingBuffer.map();
  writeToGpuBuffer(stagingBuffer.getMappedMemory(), instanceCount * instanceSize, 0);
  stagingBuffer.unmap();
}

void VulkanBuffer::createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer& buffer,
                          VkDeviceMemory& bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device->getLogicalDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(m_device->getLogicalDevice(), buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device->findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(m_device->getLogicalDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate vertex buffer memory!");
  }

  vkBindBufferMemory(m_device->getLogicalDevice(), buffer, bufferMemory, 0);
}

VulkanBuffer::~VulkanBuffer() {
  unmap();
  vkDestroyBuffer(m_device->getLogicalDevice(), buffer, nullptr);
  vkFreeMemory(m_device->getLogicalDevice(), memory, nullptr);
}

/**
 * Map a memory range of this buffer. If successful, mapped points to the specified buffer range.
 *
 * @param size (Optional) Size of the memory range to map. Pass VK_WHOLE_SIZE to map the complete
 * buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the buffer mapping call
 */
VkResult VulkanBuffer::map(VkDeviceSize size, VkDeviceSize offset) {
  assert(buffer && memory && "Called map on buffer before create");
  return vkMapMemory(m_device->getLogicalDevice(), memory, offset, size, 0, &mapped);
}

/**
 * Unmap a mapped memory range
 *
 * @note Does not return a result as vkUnmapMemory can't fail
 */
void VulkanBuffer::unmap() {
  if (mapped) {
    vkUnmapMemory(m_device->getLogicalDevice(), memory);
    mapped = nullptr;
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
void VulkanBuffer::writeToCpuBuffer(void* data, VkDeviceSize size, VkDeviceSize offset) {
  assert(mapped && "Cannot copy to unmapped buffer");

  if (size == VK_WHOLE_SIZE) {
    memcpy(mapped, data, bufferSize);
  } else {
    char* memOffset = (char*)mapped;
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
VkResult VulkanBuffer::flush(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = memory;
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
VkResult VulkanBuffer::invalidate(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = memory;
  mappedRange.offset = offset;
  mappedRange.size = size;
  return vkInvalidateMappedMemoryRanges(m_device->getLogicalDevice(), 1, &mappedRange);
}

/**
 * Create a buffer info descriptor
 *
 * @param size (Optional) Size of the memory range of the descriptor
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkDescriptorBufferInfo of specified offset and range
 */
VkDescriptorBufferInfo VulkanBuffer::descriptorInfo(VkDeviceSize size, VkDeviceSize offset) {
  return VkDescriptorBufferInfo{
      buffer,
      offset,
      (size > bufferSize - offset) ? bufferSize - offset : size,
  };
}

/**
 * Copies "instanceSize" bytes of data to the mapped buffer at an offset of index * alignmentSize
 *
 * @param data Pointer to the data to copy
 * @param index Used in offset calculation
 *
 */
void VulkanBuffer::writeToIndex(void* data, int index) {
  writeToCpuBuffer(data, instanceSize, index * alignmentSize);
}

/**
 *  Flush the memory range at index * alignmentSize of the buffer to make it visible to the device
 *
 * @param index Used in offset calculation
 *
 */
VkResult VulkanBuffer::flushIndex(int index) {
  return flush(alignmentSize, index * alignmentSize);
}

/**
 * Create a buffer info descriptor
 *
 * @param index Specifies the region given by index * alignmentSize
 *
 * @return VkDescriptorBufferInfo for instance at index
 */
VkDescriptorBufferInfo VulkanBuffer::descriptorInfoForIndex(int index) {
  return descriptorInfo(alignmentSize, index * alignmentSize);
}

/**
 * Invalidate a memory range of the buffer to make it visible to the host
 *
 * @note Only required for non-coherent memory
 *
 * @param index Specifies the region to invalidate: index * alignmentSize
 *
 * @return VkResult of the invalidate call
 */
VkResult VulkanBuffer::invalidateIndex(int index) {
  return invalidate(alignmentSize, index * alignmentSize);
}

/**
 * Copies the specified data to the staging buffer then copies to GPU VRAM. Default value writes whole buffer range
 *
 * @param data Pointer to the data to copy
 * @param size (Optional) Size of the data to copy. Pass VK_WHOLE_SIZE to flush the complete buffer
 * range.
 * @param offset (Optional) Byte offset from beginning of mapped region
 *
 */
void VulkanBuffer::writeToGpuBuffer(void* data, VkDeviceSize size, VkDeviceSize offset) {
  StagingBuffer stagingBuffer(m_device, instanceSize, instanceCount,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT, alignmentSize);

  stagingBuffer.map();
  if (size < instanceSize) {
    stagingBuffer.writeToCpuBuffer(data, size, offset);
    m_device->copyBuffer(stagingBuffer.getBuffer(), buffer, size);
  } else {
    stagingBuffer.writeToCpuBuffer(data, instanceSize, 0);
    m_device->copyBuffer(stagingBuffer.getBuffer(), buffer, instanceSize);
  }
}

StagingBuffer::StagingBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VkDeviceSize instanceSize,
                           uint32_t instanceCount,
                           VkBufferUsageFlagBits properties,
                           VkDeviceSize minOffsetAlignment)
    : VulkanBuffer(device,
                   instanceSize,
                   instanceCount,
                   properties,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   minOffsetAlignment) {}

VertexBuffer::VertexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VkDeviceSize instanceSize,
                           uint32_t instanceCount,
                           VkDeviceSize minOffsetAlignment)
    : VulkanBuffer(device,
                   instanceSize,
                   instanceCount,
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   minOffsetAlignment) {
}

IndexBuffer::IndexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                         VkDeviceSize instanceSize,
                         uint32_t instanceCount,
                         VkDeviceSize minOffsetAlignment)
    : VulkanBuffer(device,
                   instanceSize,
                   instanceCount,
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   minOffsetAlignment) {
}

UniformVulkanBuffer::UniformVulkanBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                         VkDeviceSize instanceSize,
                                         uint32_t instanceCount,
                                         VkDeviceSize minOffsetAlignment)
    : UniformBuffer(instanceSize), VulkanBuffer(device, instanceSize, instanceCount,
                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                   minOffsetAlignment) {
}


void UniformVulkanBuffer::SetDataInVkDeviceMemory(uint32_t memory_offset,
                                                  uint8_t* value,
                                                  uint32_t value_size,
                                                  uint32_t flags) {
  map(value_size, memory_offset);
  writeToCpuBuffer(value, value_size, memory_offset);
  //flush(value_size, memory_offset);
  unmap();
}

uint32_t UniformVulkanBuffer::GetDeviceMemoryOffset(const char* name) {
  std::string key(name);
  if (section_name_to_memory_offset_map.find(key) != section_name_to_memory_offset_map.end()) {
    return section_name_to_memory_offset_map[name];
  }
  return 0;
}

MultiDrawVulkanBuffer::MultiDrawVulkanBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                         uint32_t instanceCount,
                                         VkDeviceSize minOffsetAlignment)
    : VulkanBuffer(device,
                   sizeof(VkDrawIndexedIndirectCommand),
                   instanceCount,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   minOffsetAlignment) {}

VkDrawIndexedIndirectCommand MultiDrawVulkanBuffer::GetDrawIndexIndirectCommandAtInstanceIndex(
    unsigned index) {
  VkDrawIndexedIndirectCommand command;
  StagingBuffer stagingBuffer(m_device, instanceSize, 1,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT, alignmentSize);

  m_device->copyBuffer(buffer, stagingBuffer.getBuffer(), instanceSize, index * instanceCount, 0);

  stagingBuffer.map();
  void* stagingMappedMemory = stagingBuffer.getMappedMemory();
  ::memcpy(&command, stagingMappedMemory, sizeof(command));
  stagingBuffer.unmap();

  return command;
}

void MultiDrawVulkanBuffer::SetDrawIndexIndirectCommandAtInstanceIndex(unsigned index, VkDrawIndexedIndirectCommand command) {
  StagingBuffer stagingBuffer(m_device, instanceSize, 1,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT, alignmentSize);
  stagingBuffer.map();
  void* stagingMappedMemory = stagingBuffer.getMappedMemory();
  ::memcpy(stagingMappedMemory, &command, sizeof(command));
  stagingBuffer.unmap();

  m_device->copyBuffer(stagingBuffer.getBuffer(), buffer, instanceSize, 0, instanceSize * index);
}
