#include "BucketRenderer.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

std::string BucketRenderer::name_and_id() const {
  return fmt::format("[{:2d}] {}", (int)m_my_id, m_name);
}

template <class T>
void createVertexBuffer(std::vector<T>& vertices_data) {
  VkDeviceSize bufferSize = sizeof(vertices_data[0]) * vertices_data.size();
  auto staging_data = vertices_data;

  VkBuffer staging_buffer = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         staging_data);

  createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

  //copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);
}

template <class T>
VkBuffer BucketRenderer::CreateBuffer(VkBufferUsageFlags usage_flag,
                                      const T* input_data,
                                      uint64_t element_count) {
  VkBuffer buffer = VK_NULL_HANDLE;

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  bufferInfo.size = element_count * sizeof(*input_data);
  bufferInfo.usage = usage_info;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate vertex buffer memory!");
  }

  vkBindBufferMemory(m_device, buffer, vertexBufferMemory, 0);

  void* data;
  vkMapMemory(m_device, vertexBufferMemory, 0, bufferInfo.size, 0, &data);
  memcpy(data, input_data.data(), (size_t)bufferInfo.size);
  vkUnmapMemory(m_device, vertexBufferMemory);

  return buffer;
}

template <class T>
VkBuffer BucketRenderer::CreateBuffer(VkBufferUsageFlags usage_flag,
                                      std::vector<T>& input_data) {
  return CreateBuffer(VkBufferUsageFlags usage_flag, input_data.data(), input_data.size());

uint32_t BucketRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(m_physical_device, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void BucketRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void BucketRenderer::CreateTextureSampler(VkFilter mag_filter, VkFilter min_filter, uint32_t mips_level) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = mag_filter;
  samplerInfo.minFilter = min_filter;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}

VkImageView BucketRenderer::CreateImageView(VkImage image,
                                            VkFormat format,
                                            VkImageViewType viewType,
                                            VkImageAspectFlags aspectFlags,
                                            uint32_t mipLevels) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = viewType;
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

void VulkanRenderer::CreateImage(uint32_t width,
                                 uint32_t height,
                                 VkImageType imageType,
                                 uint32_t mipLevels,
                                 VkSampleCountFlagBits numSamples,
                                 VkFormat format,
                                 VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkImage& image,
                                 VkDeviceMemory& imageMemory,
                                 VkDeviceSize& deviceSize) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = imageType;
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

  if (deviceSize < memRequirements.size) {
    deviceSize = memRequirements.size;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = deviceSize;
  allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}

EmptyBucketRenderer::EmptyBucketRenderer(const std::string& name, BucketId my_id)
    : BucketRenderer(name, my_id) {}

void EmptyBucketRenderer::render(DmaFollower& dma,
                                 SharedRenderState* render_state,
                                 ScopedProfilerNode& /*prof*/) {
  // an empty bucket should have 4 things:
  // a NEXT in the bucket buffer
  // a CALL that calls the default register buffer chain
  // a CNT then RET to get out of the default register buffer chain
  // a NEXT to get to the next bucket.

  // NEXT
  auto first_tag = dma.current_tag();
  dma.read_and_advance();
  ASSERT(first_tag.kind == DmaTag::Kind::NEXT && first_tag.qwc == 0);

  // CALL
  auto call_tag = dma.current_tag();
  dma.read_and_advance();
  if (!(call_tag.kind == DmaTag::Kind::CALL && call_tag.qwc == 0)) {
    fmt::print("Bucket renderer {} ({}) was supposed to be empty, but wasn't\n", m_my_id, m_name);
  }
  ASSERT(call_tag.kind == DmaTag::Kind::CALL && call_tag.qwc == 0);

  // in the default reg buffer:
  ASSERT(dma.current_tag_offset() == render_state->default_regs_buffer);
  dma.read_and_advance();
  ASSERT(dma.current_tag().kind == DmaTag::Kind::RET);
  dma.read_and_advance();

  // NEXT to next buffer
  auto to_next_buffer = dma.current_tag();
  ASSERT(to_next_buffer.kind == DmaTag::Kind::NEXT);
  ASSERT(to_next_buffer.qwc == 0);
  dma.read_and_advance();

  // and we should now be in the next bucket!
  ASSERT(dma.current_tag_offset() == render_state->next_bucket);
}

SkipRenderer::SkipRenderer(const std::string& name, BucketId my_id) : BucketRenderer(name, my_id) {}

void SkipRenderer::render(DmaFollower& dma,
                          SharedRenderState* render_state,
                          ScopedProfilerNode& /*prof*/) {
  while (dma.current_tag_offset() != render_state->next_bucket) {
    dma.read_and_advance();
  }
}

void SharedRenderState::reset() {
  has_pc_data = false;
  for (auto& x : occlusion_vis) {
    x.valid = false;
  }
  load_status_debug.clear();
}

RenderMux::RenderMux(const std::string& name,
                     BucketId my_id,
                     std::vector<std::unique_ptr<BucketRenderer>> renderers)
    : BucketRenderer(name, my_id), m_renderers(std::move(renderers)) {
  for (auto& r : m_renderers) {
    m_name_strs.push_back(r->name_and_id());
  }
  for (auto& n : m_name_strs) {
    m_name_str_ptrs.push_back(n.data());
  }
}

void RenderMux::render(DmaFollower& dma,
                       SharedRenderState* render_state,
                       ScopedProfilerNode& prof) {
  m_renderers[m_render_idx]->enabled() = m_enabled;
  m_renderers[m_render_idx]->render(dma, render_state, prof);
}

void RenderMux::draw_debug_window() {
  ImGui::ListBox("Pick", &m_render_idx, m_name_str_ptrs.data(), m_renderers.size());
  ImGui::Separator();
  m_renderers[m_render_idx]->draw_debug_window();
}

void RenderMux::init_textures(TexturePool& tp) {
  for (auto& rend : m_renderers) {
    rend->init_textures(tp);
  }
}

void RenderMux::init_shaders(ShaderLibrary& sl) {
  for (auto& rend : m_renderers) {
    rend->init_shaders(sl);
  }
}
