#pragma once

#include "common/math/geometry.h"
#include "GraphicsDeviceVulkan.h"

class Buffer {
 public:
  Buffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
         VkDeviceSize instanceSize,
         uint32_t instanceCount,
         VkBufferUsageFlags usageFlags,
         VkMemoryPropertyFlags memoryPropertyFlags,
         VkDeviceSize minOffsetAlignment = 1);
  virtual ~Buffer();

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  void createBuffer(VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer& buffer,
                    VkDeviceMemory& bufferMemory);

  VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void unmap();

  void writeToBuffer(void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkDescriptorBufferInfo descriptorInfo(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

  void writeToIndex(void* data, int index);
  VkResult flushIndex(int index);
  VkDescriptorBufferInfo descriptorInfoForIndex(int index);
  VkResult invalidateIndex(int index);

  VkBuffer getBuffer() const { return buffer; }
  void* getMappedMemory() const { return mapped; }
  uint32_t getInstanceCount() const { return instanceCount; }
  VkDeviceSize getInstanceSize() const { return instanceSize; }
  VkDeviceSize getAlignmentSize() const { return instanceSize; }
  VkBufferUsageFlags getUsageFlags() const { return usageFlags; }
  VkMemoryPropertyFlags getMemoryPropertyFlags() const { return memoryPropertyFlags; }
  VkDeviceSize getBufferSize() const { return bufferSize; }
  std::unique_ptr<GraphicsDeviceVulkan>& getDevice() const { return m_device; }

 protected:
  static VkDeviceSize getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  void* mapped = nullptr;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;

  VkDeviceSize bufferSize;
  uint32_t instanceCount;
  VkDeviceSize instanceSize;
  VkDeviceSize alignmentSize;
  VkBufferUsageFlags usageFlags;
  VkMemoryPropertyFlags memoryPropertyFlags;
};

class VertexBuffer : public Buffer {
 public:
  VertexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
               VkDeviceSize instanceSize,
               uint32_t instanceCount,
               VkMemoryPropertyFlags memoryPropertyFlags,
               VkDeviceSize minOffsetAlignment = 1);
};

class IndexBuffer : public Buffer {
 public:
  IndexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
              VkDeviceSize instanceSize,
              uint32_t instanceCount,
              VkMemoryPropertyFlags memoryPropertyFlags,
              VkDeviceSize minOffsetAlignment = 1);
};

class UniformBuffer : public Buffer {
  // Ideally the rest of the APIs can be used in a base class to make it easy to add support for
  // other graphics APIs in the future. TODO: Separate from vulkan dependencies
 public:
  UniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                VkDeviceSize instanceSize,
                uint32_t instanceCount,
                VkMemoryPropertyFlags memoryPropertyFlags,
                VkDeviceSize minOffsetAlignment = 1);

  template <class T>
  void SetUniformBufferData(T& buffer_data);
  VkBuffer GetUniformBuffer() { return buffer; };

  template <class T>
  void SetDataInVkDeviceMemory(uint32_t memory_offset,
                               T& value,
                               uint32_t value_size,
                               uint32_t flags);
  template <class T>
  void SetDataInVkDeviceMemory(uint32_t memory_offset,
                               T* value,
                               uint32_t value_size,
                               uint32_t flags);

  uint32_t GetDeviceMemoryOffset(const char* section_name);
  void SetUniform1ui(const char* section_name, uint32_t value, uint32_t flags = 0);
  void SetUniform1i(const char* section_name, int32_t value, uint32_t flags = 0);
  void SetUniform1f(const char* section_name, float value, uint32_t flags = 0);
  void SetUniform3f(const char* section_name,
                    float value1,
                    float value2,
                    float value3,
                    uint32_t flags = 0);
  void SetUniform4f(const char* section_name,
                    float value1,
                    float value2,
                    float value3,
                    float value4,
                    uint32_t flags = 0);

  void SetUniformVectorUnsigned(const char* section_name,
                                uint32_t size,
                                uint32_t* value,
                                uint32_t flags = 0);

  void SetUniformVectorFourFloat(const char* section_name,
                                 uint32_t vector_count,
                                 float* value,
                                 uint32_t flags = 0);

  void SetUniformMathVector3f(const char* section_name, math::Vector3f& value, uint32_t flags = 0);

  void SetUniformMathVector4f(const char* section_name, math::Vector4f& value, uint32_t flags = 0);

  template <class T>
  void SetMatrixDataInVkDeviceMemory(uint32_t memory_offset,
                                     uint32_t row_count,
                                     uint32_t col_count,
                                     uint32_t matrix_count,
                                     bool want_transpose_matrix,
                                     bool is_row_col_order,
                                     uint32_t flags,
                                     T* matrix_data);

  void Set4x4MatrixDataInVkDeviceMemory(const char* section_name,
                                        uint32_t matrix_count,
                                        bool want_transpose_matrix,
                                        float* matrix_data,
                                        bool is_row_col_order = true);

 protected:
  std::unordered_map<std::string, uint32_t> section_name_to_memory_offset_map;
};
