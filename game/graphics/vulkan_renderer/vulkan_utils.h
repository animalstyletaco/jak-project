#pragma once

#include "common/math/Vector.h"
#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"

struct SharedVulkanRenderState;
class ScopedProfilerNode;


/*!
 * This is a wrapper around a framebuffer and texture to make it easier to render to a texture.
 */
class FramebufferVulkanTexturePair {
 public:
  FramebufferVulkanTexturePair(unsigned w,
                               unsigned h,
                               VkFormat format,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               int num_levels = 1);
  ~FramebufferVulkanTexturePair();

  VulkanTexture& Texture(int level) { return m_textures[level]; }

  FramebufferVulkanTexturePair(const FramebufferVulkanTexturePair&) = delete;
  FramebufferVulkanTexturePair& operator=(const FramebufferVulkanTexturePair&) = delete;

 private:
  friend class FramebufferVulkanTexturePairContext;

  VkExtent2D extents = {640, 480};
  VkOffset2D offsetExtents;

  std::unique_ptr<SwapChain> m_swap_chain;
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;

  VkSampler m_sampler = VK_NULL_HANDLE;
  std::vector<VulkanTexture> m_textures;
};

class FramebufferVulkanTexturePairContext {
 public:
  FramebufferVulkanTexturePairContext(FramebufferVulkanTexturePair& fb, int level = 0);
  ~FramebufferVulkanTexturePairContext();

  void switch_to(FramebufferVulkanTexturePair& fb);

  FramebufferVulkanTexturePairContext(const FramebufferVulkanTexturePairContext&) = delete;
  FramebufferVulkanTexturePairContext& operator=(const FramebufferVulkanTexturePairContext&) = delete;

 private:
  FramebufferVulkanTexturePair* m_fb;
  VkViewport m_old_viewport;
  VkFramebuffer m_old_framebuffer;
};

// draw over the full screen.
// you must set alpha/ztest/etc.
class FullScreenDrawVulkan {
 public:
  FullScreenDrawVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~FullScreenDrawVulkan();
  FullScreenDrawVulkan(const FullScreenDrawVulkan&) = delete;
  FullScreenDrawVulkan& operator=(const FullScreenDrawVulkan&) = delete;
  void draw(const math::Vector4f& color,
            SharedVulkanRenderState* render_state,
            ScopedProfilerNode& prof);

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<UniformVulkanBuffer> m_fragment_uniform_buffer;
  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;
};
