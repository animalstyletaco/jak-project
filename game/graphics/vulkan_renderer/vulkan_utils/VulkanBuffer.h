#pragma once

#include <unordered_map>

#include "common/math/geometry.h"
#include "game/graphics/general_renderer/renderer_utils/Buffer.h"
#include "GraphicsDeviceVulkan.h"

class VulkanBuffer {
 public:
  VulkanBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
               VkDeviceSize instanceSize,
               uint32_t instanceCount,
               VkBufferUsageFlags usageFlags,
               VkMemoryPropertyFlags memoryPropertyFlags,
               VkDeviceSize minOffsetAlignment = 1);
  virtual ~VulkanBuffer();

  VulkanBuffer(const VulkanBuffer&);
  VulkanBuffer& operator=(const VulkanBuffer&);

  void createBuffer(VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer& buffer,
                    VkDeviceMemory& bufferMemory);

  VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void unmap();

  void writeToCpuBuffer(void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void writeToGpuBuffer(void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
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

  VkDeviceSize bufferSize = 0;
  VkDeviceSize instanceSize = 0;
  uint32_t instanceCount = 0;
  VkDeviceSize alignmentSize = 0;
  VkDeviceSize minOffsetAlignment = 0;
  VkBufferUsageFlags usageFlags;
  VkMemoryPropertyFlags memoryPropertyFlags;
};

class VertexBuffer : public VulkanBuffer {
 public:
  VertexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
               VkDeviceSize instanceSize,
               uint32_t instanceCount,
               VkDeviceSize minOffsetAlignment = 1);
};

class IndexBuffer : public VulkanBuffer {
 public:
  IndexBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
              VkDeviceSize instanceSize,
              uint32_t instanceCount,
              VkDeviceSize minOffsetAlignment = 1);
};

class StagingBuffer : public VulkanBuffer {
 public:
  StagingBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                VkDeviceSize instanceSize,
                uint32_t instanceCount,
                VkBufferUsageFlagBits properties,
                VkDeviceSize minOffsetAlignment = 1);
};

class UniformVulkanBuffer : public UniformBuffer, public VulkanBuffer {
 public:
  UniformVulkanBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                      VkDeviceSize instanceSize,
                      uint32_t instanceCount,
                      VkDeviceSize minOffsetAlignment = 1);

  void SetDataInVkDeviceMemory(uint32_t memory_offset,
                               uint8_t* value,
                               uint32_t value_size,
                               uint32_t flags) override;

  uint32_t GetDeviceMemoryOffset(const char* name) override;

 protected:
  std::unordered_map<std::string, uint32_t> section_name_to_memory_offset_map;
};

class MultiDrawVulkanBuffer : public UniformBuffer, public VulkanBuffer {
 public:
  MultiDrawVulkanBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                  uint32_t instanceCount,
                  VkDeviceSize minOffsetAlignment = 1);

  VkDrawIndexedIndirectCommand GetDrawIndexIndirectCommandAtInstanceIndex(unsigned);
  void SetDrawIndexIndirectCommandAtInstanceIndex(unsigned, VkDrawIndexedIndirectCommand);
};
