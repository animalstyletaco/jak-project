#include "vulkan_utils.h"

#include <array>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FramebufferTexturePair::FramebufferTexturePair(int w,
                                               int h,
                                               VkFormat format,
                                               VkDevice& device, int num_levels)
    : m_w(w), m_h(h), m_device(device) {
  VkSamplerCreateInfo samplerInfo;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

  textures.resize(num_levels);
  texture_memories.resize(num_levels);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.depth = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.mipLevels = num_levels;

  for (int i = 0; i < num_levels; i++) {
    imageInfo.extent.width = m_w >> i;
    imageInfo.extent.height = m_h >> i;

    vulkan_utils::CreateImage(m_device, imageInfo.extent.width, imageInfo.extent.height,
                              num_levels, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textures[i],
                              texture_memories[i]);
  }

  for (int i = 0; i < num_levels; i++) {
    //m_textures[i];
    // TODO: Copy Image to Buffer
    // I don't know if we really need to do this. whatever uses this texture should figure it out.

    //glDrawBuffers(1, draw_buffers);
  }
}

FramebufferTexturePair::~FramebufferTexturePair() {
  for (auto& texture : textures) {
    vkDestroyImage(m_device, texture, nullptr);
  }

  for (auto& texture_memory : texture_memories) {
    vkFreeMemory(m_device, texture_memory, nullptr);
  }
}

FramebufferTexturePairContext::FramebufferTexturePairContext(FramebufferTexturePair& fb, int level)
    : m_fb(&fb) {
  glGetIntegerv(GL_VIEWPORT, m_old_viewport);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_old_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fb->m_framebuffers[level]);
  glViewport(0, 0, m_fb->m_w, m_fb->m_h);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_fb->m_texture, level);
}

void FramebufferTexturePairContext::switch_to(FramebufferTexturePair& fb) {
  if (&fb != m_fb) {
    m_fb = &fb;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fb->m_framebuffers[0]);
    glViewport(0, 0, m_fb->m_w, m_fb->m_h);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_fb->m_texture, 0);
  }
}

FramebufferTexturePairContext::~FramebufferTexturePairContext() {
  glViewport(m_old_viewport[0], m_old_viewport[1], m_old_viewport[2], m_old_viewport[3]);
  glBindFramebuffer(GL_FRAMEBUFFER, m_old_framebuffer);
}

FullScreenDraw::FullScreenDraw(VkDevice& device) : m_device(device) {

  m_uniform_buffer = UniformBuffer(device, sizeof(math::Vector4f));
  struct Vertex {
    float x, y;
  };

  std::array<Vertex, 4> vertices = {
      Vertex{-1, -1},
      Vertex{-1, 1},
      Vertex{1, -1},
      Vertex{1, 1},
  };

  VkDeviceSize device_size = sizeof(Vertex) * 4;
  VkBuffer m_vertex_buffer = vulkan_utils::CreateBuffer(m_device, m_device_memory, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, device_size);
  vulkan_utils::SetDataInVkDeviceMemory(m_device, m_device_memory, device_size, 0,
                                        vertices.data(), device_size);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,               // location 0 in the shader
                        2,               // 2 floats per vert
                        GL_FLOAT,        // floats
                        GL_TRUE,         // normalized, ignored,
                        sizeof(Vertex),  //
                        nullptr          //
  );
}

FullScreenDraw::~FullScreenDraw() {
  glDeleteBuffers(1, &m_vertex_buffer);
}

void FullScreenDraw::draw(const math::Vector4f& color,
                          SharedRenderState* render_state,
                          ScopedProfilerNode& prof) {
  m_uniform_buffer.SetUniform4f("fragment_color", color[0], color[1], color[2],
              color[3]);

  prof.add_tri(2);
  prof.add_draw_call();
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

template <class T>
void vulkan_utils::GetDataInVkDeviceMemory(VkDevice& device,
                                           VkDeviceMemory& device_memory,
                                           VkDeviceSize& device_size,
                                           uint32_t memory_offset,
                                           T& value,
                                           uint32_t value_size,
                                           uint32_t flags) {
  void* data = NULL;
  vkMapMemory(device, device_memory, memory_offset, device_size, flags, &data);
  ::memcpy(&value, data, value_size);
  vkUnmapMemory(device, device_memory);
}

template <class T>
void vulkan_utils::SetDataInVkDeviceMemory(VkDevice& device,
                                           VkDeviceMemory& device_memory,
                                           VkDeviceSize& device_size,
                                           uint32_t memory_offset,
                                           T* value,
                                           uint32_t value_size,
                                           uint32_t flags) {
  void* data = NULL;
  vkMapMemory(device, device_memory, memory_offset, device_size, flags, &data);
  ::memcpy(value, data, value_size);
  vkUnmapMemory(device, device_memory);
}

template <class T>
void vulkan_utils::SetDataInVkDeviceMemory(VkDevice& device,
                                           VkDeviceMemory& device_memory,
                                           VkDeviceSize& device_size,
                                           uint32_t memory_offset,
                                           T& value,
                                           uint32_t value_size,
                                           uint32_t flags) {
  void* data = NULL;
  vkMapMemory(device, device_memory, memory_offset, device_size, flags, &data);
  ::memcpy(data, &value, value_size);
  vkUnmapMemory(device, device_memory);
}

template <class T>
void vulkan_utils::SetDataInVkDeviceMemory(VkDevice& device,
                                           VkDeviceMemory& device_memory,
                                           VkDeviceSize& device_size,
                                           uint32_t memory_offset,
                                           T* value,
                                           uint32_t value_size,
                                           uint32_t flags) {
  void* data = NULL;
  vkMapMemory(device, device_memory, memory_offset, device_size, flags, &data);
  ::memcpy(data, value, value_size);
  vkUnmapMemory(device, device_memory);
}


VkBuffer vulkan_utils::CreateBuffer(VkDevice& device,
                                    VkDeviceMemory& device_memory,
                                    VkBufferUsageFlags usage_flag,
                                    VkDeviceSize& element_count) {
  VkBuffer buffer = VK_NULL_HANDLE;

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  bufferInfo.size = element_count * sizeof(*input_data);
  bufferInfo.usage = usage_flag;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &device_memory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate vertex buffer memory!");
  }

  vkBindBufferMemory(device, buffer, device_memory, 0);
  return buffer;
}

void vulkan_utils::CreateImage(VkDevice& device,
                               uint32_t width,
                               uint32_t height,
                               uint32_t mipLevels,
                               VkSampleCountFlagBits numSamples,
                               VkFormat format,
                               VkImageTiling tiling,
                               VkImageUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkImage& image,
                               VkDeviceMemory& imageMemory) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = numSamples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}


VkImageView vulkan_utils::CreateImageView(VkDevice device,
                                          VkImage image,
                                          VkFormat format,
                                          VkImageAspectFlags aspectFlags,
                                          uint32_t mipLevels) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }

  return imageView;
}

UniformBuffer::UniformBuffer(VkDevice device, VkDeviceSize device_size)
    : m_device(device), m_device_size(device_size) {
  m_uniform_buffer = vulkan_utils::CreateBuffer(m_device, m_device_memory,
                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_device_size);

}

template <class T>
void UniformBuffer::SetUniformBufferData(T& buffer_data) {
  vulkan_utils::SetDataInVkDeviceMemory(m_device, m_device_memory, m_device_size, 0, buffer_data, 0);
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
  void* data = NULL;

  uint32_t element_count = row_count * col_count;
  uint32_t memory_size = row_count * col_count * sizeof(*matrix_data) * matrix_count;
  vkMapMemory(m_device, m_device_memory, memory_offset, memory_size, flags, &data);
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

      ::memcpy(data, &transpose_matrix, memory_size);
    } else {
      ::memcpy(data, matrix_data, memory_size);
    }
  vkUnmapMemory(m_device, m_device_memory);
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

  void* data = NULL;
  vkMapMemory(m_device, m_device_memory, memory_offset, sizeof(float) * 4, flags, &data);
  float* memory_mapped_data = (float*)data;
  memory_mapped_data[0] = value1;
  memory_mapped_data[1] = value2;
  memory_mapped_data[2] = value3;
  vkUnmapMemory(m_device, m_device_memory);
}

void UniformBuffer::SetUniform4f(const char* section_name,
                          float value1,
                          float value2,
                          float value3,
                          float value4,
                          uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  void* data = NULL;
  vkMapMemory(m_device, m_device_memory, memory_offset, sizeof(float) * 4, flags, &data);
  float* memory_mapped_data = (float*)data;
  memory_mapped_data[0] = value1;
  memory_mapped_data[1] = value2;
  memory_mapped_data[2] = value3;
  memory_mapped_data[3] = value4;
  vkUnmapMemory(m_device, m_device_memory);
}

UniformBuffer::~UniformBuffer() {
  if (m_uniform_buffer) {
    vkDestroyBuffer(m_device, m_uniform_buffer, nullptr);
  }

  if (m_device_memory) {
    vkFreeMemory(m_device, m_device_memory, nullptr);
  }
}
