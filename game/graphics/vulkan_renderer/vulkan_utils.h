#pragma once

#include "common/math/Vector.h"
#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/vulkan_utils/Buffer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/pipelines/vulkan_pipeline.h"

struct SharedRenderState;
class ScopedProfilerNode;

/*!
 * This is a wrapper around a framebuffer and texture to make it easier to render to a texture.
 */
class FramebufferTexturePair {
 public:
  FramebufferTexturePair(int w, int h, VkFormat format, std::unique_ptr<GraphicsDeviceVulkan>& device, int num_levels = 1);
  ~FramebufferTexturePair();

  TextureInfo* texture() const { return (TextureInfo*)textures.data(); }

  FramebufferTexturePair(const FramebufferTexturePair&) = delete;
  FramebufferTexturePair& operator=(const FramebufferTexturePair&) = delete;

 private:
  friend class FramebufferTexturePairContext;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::vector<TextureInfo> textures;

  int m_w, m_h;

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
  VkViewport m_old_viewport;
  GLint m_old_framebuffer;
};

// draw over the full screen.
// you must set alpha/ztest/etc.
class FullScreenDraw {
 public:
  FullScreenDraw(std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~FullScreenDraw();
  FullScreenDraw(const FullScreenDraw&) = delete;
  FullScreenDraw& operator=(const FullScreenDraw&) = delete;
  void draw(const math::Vector4f& color, SharedRenderState* render_state, ScopedProfilerNode& prof);

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<UniformBuffer> m_fragment_uniform_buffer;
};
