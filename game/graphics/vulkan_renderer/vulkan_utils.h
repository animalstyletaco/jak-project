#pragma once

#include "common/math/Vector.h"

#include "game/graphics/pipelines/vulkan.h"

struct SharedRenderState;
class ScopedProfilerNode;

namespace vulkan_utils { //TODO: Should the rest of this file be encapsulated in vulkan_utils namespace?
VkBuffer CreateBuffer(VkDevice& device,
                      VkDeviceMemory& device_memory,
                      VkBufferUsageFlags usage_flag,
                      VkDeviceSize& element_count);

template <class T>
void GetDataInVkDeviceMemory(VkDevice& device,
                             VkDeviceMemory& device_memory,
                             VkDeviceSize& device_size,
                             uint32_t memory_offset,
                             T& value,
                             uint32_t value_size,
                             uint32_t flags);
template <class T>
void GetDataInVkDeviceMemory(VkDevice& device,
                             VkDeviceMemory& device_memory,
                             VkDeviceSize& device_size,
                             uint32_t memory_offset,
                             T* value,
                             uint32_t value_size,
                             uint32_t flags);

template <class T>
void SetDataInVkDeviceMemory(VkDevice& device,
                             VkDeviceMemory& device_memory,
                             VkDeviceSize& device_size,
                             uint32_t memory_offset,
                             T& value,
                             uint32_t value_size,
                             uint32_t flags);
template <class T>
void SetDataInVkDeviceMemory(VkDevice& device,
                             VkDeviceMemory& device_memory,
                             VkDeviceSize& device_size,
                             uint32_t memory_offset,
                             T* value,
                             uint32_t value_size,
                             uint32_t flags);

void CreateImage(VkDevice& device,
                 uint32_t width,
                 uint32_t height,
                 uint32_t mipLevels,
                 VkSampleCountFlagBits numSamples,
                 VkFormat format,
                 VkImageTiling tiling,
                 VkImageUsageFlags usage,
                 VkMemoryPropertyFlags properties,
                 VkImage& image,
                 VkDeviceMemory& imageMemory);

VkImageView CreateImageView(VkDevice device,
                            VkImage image,
                            VkFormat format,
                            VkImageAspectFlags aspectFlags,
                            uint32_t mipLevels);

}  // namespace vulkan_utils

class UniformBuffer {
  // Ideally the rest of the APIs can be used in a base class to make it easy to add support for
  // other graphics APIs in the future.
 public:
  UniformBuffer() = default;
  UniformBuffer(VkDevice device, VkDeviceSize device_size);
  ~UniformBuffer();

  template <class T>
  void SetUniformBufferData(T& buffer_data);
  VkBuffer GetUniformBuffer() { return m_uniform_buffer; };

  uint32_t GetDeviceMemoryOffset(const char* name);

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

 private:
  VkDevice m_device = VK_NULL_HANDLE;
  VkDeviceMemory m_device_memory = VK_NULL_HANDLE;
  VkBuffer m_uniform_buffer = VK_NULL_HANDLE;
  VkDeviceSize m_device_size = 0;
};

/*!
 * This is a wrapper around a framebuffer and texture to make it easier to render to a texture.
 */
class FramebufferTexturePair {
 public:
  FramebufferTexturePair(int w, int h, VkFormat format, VkDevice& device, int num_levels = 1);
  ~FramebufferTexturePair();

  VkImage texture() const { return textures[0]; }
  VkDeviceMemory texture_memory() const { return texture_memories[0]; }

  FramebufferTexturePair(const FramebufferTexturePair&) = delete;
  FramebufferTexturePair& operator=(const FramebufferTexturePair&) = delete;

 private:
  void CreateImage(VkImageCreateInfo& imageInfo,
                   VkImage& image,
                   VkDeviceMemory& imageMemory);
  friend class FramebufferTexturePairContext;

  std::vector<VkImage> textures;
  std::vector<VkDeviceMemory> texture_memories;

  int m_w, m_h;
  VkDevice m_device = VK_NULL_HANDLE;
};

class FramebufferTexturePairContext {
 public:
  FramebufferTexturePairContext(FramebufferTexturePair& fb, int level = 0);
  ~FramebufferTexturePairContext();

  void switch_to(FramebufferTexturePair& fb);

  FramebufferTexturePairContext(const FramebufferTexturePairContext&) = delete;
  FramebufferTexturePairContext& operator=(const FramebufferTexturePairContext&) = delete;

 private:
  FramebufferTexturePair* m_fb;
  GLint m_old_viewport[4];
  GLint m_old_framebuffer;
};

// draw over the full screen.
// you must set alpha/ztest/etc.
class FullScreenDraw {
 public:
  FullScreenDraw(VkDevice& device);
  ~FullScreenDraw();
  FullScreenDraw(const FullScreenDraw&) = delete;
  FullScreenDraw& operator=(const FullScreenDraw&) = delete;
  void draw(const math::Vector4f& color, SharedRenderState* render_state, ScopedProfilerNode& prof);

 private:
  UniformBuffer m_uniform_buffer;
  VkBuffer m_vertex_buffer = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VkDeviceMemory m_device_memory = VK_NULL_HANDLE;
};
