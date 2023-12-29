#pragma once

#include "game/graphics/general_renderer/Profiler.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/DescriptorLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SamplerHelper.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"

struct SharedVulkanRenderState;
struct VulkanInitializationInfo;

// draw over the full screen.
// you must set alpha/ztest/etc.
class FullScreenDrawVulkan {
 public:
  FullScreenDrawVulkan(std::shared_ptr<GraphicsDeviceVulkan> device,
                       VulkanInitializationInfo& vulkan_info);
  ~FullScreenDrawVulkan();
  FullScreenDrawVulkan(const FullScreenDrawVulkan&) = delete;
  FullScreenDrawVulkan& operator=(const FullScreenDrawVulkan&) = delete;
  void draw(const math::Vector4f& color,
            SharedVulkanRenderState* render_state,
            ScopedProfilerNode& prof);

 private:
  struct Vertex {
    float x, y;
  };

  void create_command_buffers();
  void init_shaders();
  void initialize_input_binding_descriptions();
  void create_pipeline_layout();

  uint32_t currentImageIndex = 0;

  std::vector<VkCommandBuffer> commandBuffers;
  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;

  VulkanInitializationInfo& m_vulkan_info;
  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;

  VkDescriptorBufferInfo m_fragment_buffer_descriptor_info;
};
