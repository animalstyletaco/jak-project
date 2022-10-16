#include "Buffer.h"
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
VkDeviceSize Buffer::getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
  if (minOffsetAlignment > 0) {
    return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
  }
  return instanceSize;
}

Buffer::Buffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
               VkDeviceSize instanceSize,
               uint32_t instanceCount,
               VkBufferUsageFlags usageFlags,
               VkMemoryPropertyFlags memoryPropertyFlags,
               VkDeviceSize minOffsetAlignment)
    : m_device{device},
      instanceSize{instanceSize},
      instanceCount{instanceCount},
      usageFlags{usageFlags},
      memoryPropertyFlags{memoryPropertyFlags} {
  alignmentSize = getAlignment(instanceSize, minOffsetAlignment);
  bufferSize = alignmentSize * instanceCount;
  createBuffer(bufferSize, usageFlags, memoryPropertyFlags, buffer, memory);
}

void Buffer::createBuffer(VkDeviceSize size,
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

Buffer::~Buffer() {
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
VkResult Buffer::map(VkDeviceSize size, VkDeviceSize offset) {
  assert(buffer && memory && "Called map on buffer before create");
  return vkMapMemory(m_device->getLogicalDevice(), memory, offset, size, 0, &mapped);
}

/**
 * Unmap a mapped memory range
 *
 * @note Does not return a result as vkUnmapMemory can't fail
 */
void Buffer::unmap() {
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
void Buffer::writeToBuffer(void* data, VkDeviceSize size, VkDeviceSize offset) {
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
VkResult Buffer::flush(VkDeviceSize size, VkDeviceSize offset) {
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
VkResult Buffer::invalidate(VkDeviceSize size, VkDeviceSize offset) {
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
VkDescriptorBufferInfo Buffer::descriptorInfo(VkDeviceSize size, VkDeviceSize offset) {
  return VkDescriptorBufferInfo{
      buffer,
      offset,
      size,
  };
}

/**
 * Copies "instanceSize" bytes of data to the mapped buffer at an offset of index * alignmentSize
 *
 * @param data Pointer to the data to copy
 * @param index Used in offset calculation
 *
 */
void Buffer::writeToIndex(void* data, int index) {
  writeToBuffer(data, instanceSize, index * alignmentSize);
}

/**
 *  Flush the memory range at index * alignmentSize of the buffer to make it visible to the device
 *
 * @param index Used in offset calculation
 *
 */
VkResult Buffer::flushIndex(int index) {
  return flush(alignmentSize, index * alignmentSize);
}

/**
 * Create a buffer info descriptor
 *
 * @param index Specifies the region given by index * alignmentSize
 *
 * @return VkDescriptorBufferInfo for instance at index
 */
VkDescriptorBufferInfo Buffer::descriptorInfoForIndex(int index) {
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
VkResult Buffer::invalidateIndex(int index) {
  return invalidate(alignmentSize, index * alignmentSize);
}

VertexBuffer::VertexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VkDeviceSize instanceSize,
                           uint32_t instanceCount,
                           VkMemoryPropertyFlags memoryPropertyFlags,
                           VkDeviceSize minOffsetAlignment)
    : Buffer(device,
             instanceSize,
             instanceCount,
             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
             memoryPropertyFlags,
             minOffsetAlignment) {
}

IndexBuffer::IndexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                         VkDeviceSize instanceSize,
                         uint32_t instanceCount,
                         VkMemoryPropertyFlags memoryPropertyFlags,
                         VkDeviceSize minOffsetAlignment)
    : Buffer(device,
             instanceSize,
             instanceCount,
             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
             memoryPropertyFlags,
             minOffsetAlignment) {
}

UniformBuffer::UniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                             VkDeviceSize instanceSize,
                             uint32_t instanceCount,
                             VkMemoryPropertyFlags memoryPropertyFlags,
                             VkDeviceSize minOffsetAlignment)
    : Buffer(device, instanceSize, instanceCount,
             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
             memoryPropertyFlags,
             minOffsetAlignment) {
}

template <class T>
void UniformBuffer::SetDataInVkDeviceMemory(uint32_t memory_offset,
                                            T& value,
                                            uint32_t value_size,
                                            uint32_t flags) {
  map(value_size, memory_offset);
  writeToBuffer(&value, value_size, memory_offset);
  unmap();
}

template <class T>
void UniformBuffer::SetDataInVkDeviceMemory(uint32_t memory_offset,
                                            T* value,
                                            uint32_t value_size,
                                            uint32_t flags) {
  map(value_size, memory_offset);
  writeToBuffer(value, value_size, memory_offset);
  unmap();
}

template <class T>
void UniformBuffer::SetUniformBufferData(T& buffer_data) {
  vulkan_utils::SetDataInVkDeviceMemory(m_device, m_device_memory, m_device_size, 0, buffer_data,
                                        0);
}

template <class T>
void UniformBuffer::SetMatrixDataInVkDeviceMemory(uint32_t memory_offset,
                                                  uint32_t row_count,
                                                  uint32_t col_count,
                                                  uint32_t matrix_count,
                                                  bool want_transpose_matrix,
                                                  bool is_row_col_order,
                                                  uint32_t flags,
                                                  T* matrix_data) {
  uint32_t element_count = row_count * col_count;
  uint32_t memory_size = row_count * col_count * sizeof(*matrix_data) * matrix_count;
  map(memory_size, memory_offset);
  for (uint32_t matrix_id = 0; matrix_id < matrix_count; matrix_id++)
    if (want_transpose_matrix) {
      // TODO: Verify logic
      T transpose_matrix[row_count * col_count * matrix_count];
      if (is_row_col_order) {
        for (uint32_t i = 0; i < matrix_count; i++) {
          for (uint32_t j = 0; j < row_count; j++) {
            for (uint32_t k = 0; k < col_count; k++) {
              transpose_matrix[i * element_count + (k * col_count + j)] =
                  matrix_data[i * element_count + (j * row_count + k)];
            }
          }
        }
      } else {
        for (uint32_t i = 0; i < matrix_count; i++) {
          for (uint32_t j = 0; j < col_count; j++) {
            for (uint32_t k = 0; k < row_count; k++) {
              transpose_matrix[i * element_count + (k * col_count + j)] =
                  matrix_data[i * element_count + (j * row_count + k)];
            }
          }
        }
      }

      ::memcpy(mapped, &transpose_matrix, memory_size);
    } else {
      ::memcpy(mapped, matrix_data, memory_size);
    }
  unmap();
}

void UniformBuffer::Set4x4MatrixDataInVkDeviceMemory(const char* section_name,
                                                     uint32_t matrix_count,
                                                     bool want_transpose_matrix,
                                                     float* matrix_data,
                                                     bool is_row_col_order) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetMatrixDataInVkDeviceMemory(memory_offset, 4, 4, matrix_count, want_transpose_matrix,
                                is_row_col_order, 0, matrix_data);
}

void UniformBuffer::SetUniformVectorUnsigned(const char* section_name,
                                             uint32_t size,
                                             uint32_t* value,
                                             uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(unsigned) * size, flags);
}

void UniformBuffer::SetUniformVectorFourFloat(const char* section_name,
                                              uint32_t vector_count,
                                              float* value,
                                              uint32_t flags) {
  uint32_t size = vector_count * 4 * sizeof(float);

  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, size, flags);
}

void UniformBuffer::SetUniform1ui(const char* section_name, uint32_t value, uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void UniformBuffer::SetUniform1i(const char* section_name, int32_t value, uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void UniformBuffer::SetUniform1f(const char* section_name, float value, uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void UniformBuffer::SetUniformMathVector3f(const char* section_name,
                                           math::Vector3f& value,
                                           uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void UniformBuffer::SetUniformMathVector4f(const char* section_name,
                                           math::Vector4f& value,
                                           uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void UniformBuffer::SetUniform3f(const char* section_name,
                                 float value1,
                                 float value2,
                                 float value3,
                                 uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  map(VK_WHOLE_SIZE, memory_offset);
  float* memory_mapped_data = (float*)mapped;
  memory_mapped_data[0] = value1;
  memory_mapped_data[1] = value2;
  memory_mapped_data[2] = value3;
  unmap();
}

void UniformBuffer::SetUniform4f(const char* section_name,
                                 float value1,
                                 float value2,
                                 float value3,
                                 float value4,
                                 uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  map(VK_WHOLE_SIZE, memory_offset);
  float* memory_mapped_data = (float*)mapped;
  memory_mapped_data[0] = value1;
  memory_mapped_data[1] = value2;
  memory_mapped_data[2] = value3;
  memory_mapped_data[2] = value4;
  unmap();
}

uint32_t UniformBuffer::GetDeviceMemoryOffset(const char* name) {
  std::string key(name);
  if (section_name_to_memory_offset_map.find(key) != section_name_to_memory_offset_map.end()) {
    return section_name_to_memory_offset_map[name];
  }
  return 0;
}
