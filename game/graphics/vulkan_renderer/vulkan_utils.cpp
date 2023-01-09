#include "vulkan_utils.h"

#include <array>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FramebufferVulkanTexturePair::FramebufferVulkanTexturePair(unsigned w,
                                                           unsigned h,
                                                           VkFormat format,
                                                           std::unique_ptr<GraphicsDeviceVulkan>& device, int num_levels)
    : m_device(device) {
  extents = {w, h};
  m_swap_chain = std::make_unique<SwapChain>(m_device, extents);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(m_device->getLogicalDevice(), &samplerInfo, nullptr, &m_sampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Frambuffer helper sampler\n");
  }

  m_textures.resize(num_levels, m_device);
  for (uint32_t i = 0; i < num_levels; i++) {
    VkExtent3D textureExtents = {extents.width >> i, extents.height >> i, 1};
    //Check needed to avoid validation error. See https://vulkan-tutorial.com/Generating_Mipmaps#page_Image-creation for more info
    uint32_t maxMinmapLevels =
        static_cast<uint32_t>(std::floor(std::log2(std::max(textureExtents.width, textureExtents.height)))) + 1;
    uint32_t minmapLevel = (i + 1 > maxMinmapLevels) ? maxMinmapLevels : i + 1;
    m_textures[i].createImage(textureExtents, minmapLevel, VK_IMAGE_TYPE_2D, device->getMsaaCount(), format,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  
    m_textures[i].createImageView(VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}

FramebufferVulkanTexturePair::~FramebufferVulkanTexturePair() {
  vkDestroySampler(m_device->getLogicalDevice(), m_sampler, nullptr);
}

FramebufferVulkanTexturePairContext::FramebufferVulkanTexturePairContext(FramebufferVulkanTexturePair& fb, int level)
    : m_fb(&fb) {
  //TODO: store previous viewport settings
  //recreateSwapChain();
  //TODO: CopyImageToBuffer
}

void FramebufferVulkanTexturePairContext::switch_to(FramebufferVulkanTexturePair& fb) {
  if (&fb != m_fb) {
    m_fb = &fb;
    // swapChainExtent.width = m_fb->m_w;
    // swapChainExtent.height = m_fb->m_h;
    // recreateSwapChain();
    //glViewport(0, 0, m_fb->m_w, m_fb->m_h);
  }
}

FramebufferVulkanTexturePairContext::~FramebufferVulkanTexturePairContext() {
}

FullScreenDrawVulkan::FullScreenDrawVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device), m_pipeline_layout(device) {
  m_fragment_uniform_buffer = std::make_unique<UniformVulkanBuffer>(
      m_device, sizeof(math::Vector4f), 1, 1);

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
  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, device_size, 1, 1);
  m_vertex_buffer->writeToGpuBuffer(vertices.data(), device_size, 0);

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributeDescription{};
  // TODO: This value needs to be normalized
  attributeDescription.binding = 0;
  attributeDescription.location = 0;
  attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescription.offset = 0;

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount = sizeof(attributeDescription);
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;
}

FullScreenDrawVulkan::~FullScreenDrawVulkan() {
}

void FullScreenDrawVulkan::draw(const math::Vector4f& color,
                          SharedVulkanRenderState* render_state,
                          ScopedProfilerNode& prof) {
  m_fragment_uniform_buffer->SetUniform4f("fragment_color", color[0], color[1], color[2],
              color[3]);

  prof.add_tri(2);
  prof.add_draw_call();
  //glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

