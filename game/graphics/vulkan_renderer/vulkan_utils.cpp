#include "vulkan_utils.h"

#include <array>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FramebufferTexturePair::FramebufferTexturePair(int w,
                                               int h,
                                               VkFormat format,
                                               std::unique_ptr<GraphicsDeviceVulkan>& device, int num_levels)
    : m_device(device), m_w(w), m_h(h) {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  textures.resize(num_levels, TextureInfo{device});
  for (int i = 0; i < num_levels; i++) {
    VkExtent3D extents{m_w >> i, m_h >> i, 1};

    textures[i].CreateImage(extents, 1, VK_IMAGE_TYPE_2D, device->getMsaaCount(), format, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    textures[i].CreateImageView(VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }

  for (int i = 0; i < num_levels; i++) {
    //m_textures[i];
    // TODO: Copy Image to Buffer
    // I don't know if we really need to do this. whatever uses this texture should figure it out.

    //glDrawBuffers(1, draw_buffers);
  }
}

FramebufferTexturePair::~FramebufferTexturePair() {
}

FramebufferTexturePairContext::FramebufferTexturePairContext(FramebufferTexturePair& fb, int level)
    : m_fb(&fb) {
  //TODO: store previous viewport settings
  //glViewport(0, 0, m_fb->m_w, m_fb->m_h);
  //swapChainExtent.width = m_fb->m_w;
  //swapChainExtent.height = m_fb->m_h;
  //recreateSwapChain();
  //TODO: CopyImageToBuffer
}

void FramebufferTexturePairContext::switch_to(FramebufferTexturePair& fb) {
  if (&fb != m_fb) {
    m_fb = &fb;
    // swapChainExtent.width = m_fb->m_w;
    // swapChainExtent.height = m_fb->m_h;
    // recreateSwapChain();
    //glViewport(0, 0, m_fb->m_w, m_fb->m_h);
  }
}

FramebufferTexturePairContext::~FramebufferTexturePairContext() {
  //m_old_viewport;
  //m_fb->m_device->recreateSwapChains(m_old_viewport);
}

FullScreenDraw::FullScreenDraw(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device), m_pipeline_layout(device) {
  m_fragment_uniform_buffer = std::make_unique<UniformBuffer>(
      m_device, sizeof(math::Vector4f), 1,
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

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
  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, device_size, 1, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);
  m_vertex_buffer->map();
  m_vertex_buffer->writeToBuffer(vertices.data());
  m_vertex_buffer->unmap();

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

FullScreenDraw::~FullScreenDraw() {
}

void FullScreenDraw::draw(const math::Vector4f& color,
                          SharedRenderState* render_state,
                          ScopedProfilerNode& prof) {
  m_fragment_uniform_buffer->SetUniform4f("fragment_color", color[0], color[1], color[2],
              color[3]);

  prof.add_tri(2);
  prof.add_draw_call();
  //glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

