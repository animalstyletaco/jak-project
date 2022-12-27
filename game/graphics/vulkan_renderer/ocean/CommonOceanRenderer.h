#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/ocean/CommonOceanRenderer.h"

class CommonOceanVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  CommonOceanVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                 uint32_t instanceCount,
                                 VkDeviceSize minOffsetAlignment = 1);
};

struct CommonOceanFragmentUniformShaderData {
  float color_mult;
  float alpha_mult;
  math::Vector4f fog_color;
  int32_t bucket;
  uint32_t tex_T0;
};

class CommonOceanFragmentUniformBuffer : public UniformVulkanBuffer {
 public:
  CommonOceanFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                   uint32_t instanceCount,
                                   VkDeviceSize minOffsetAlignment = 1);
};

class CommonOceanVulkanRenderer : public BaseCommonOceanRenderer {
 public:
  CommonOceanVulkanRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info);
  ~CommonOceanVulkanRenderer();

  void flush_near(BaseSharedRenderState* render_state,
                  ScopedProfilerNode& prof,
                  std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
                  std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer);

  void flush_mid(BaseSharedRenderState* render_state,
                 ScopedProfilerNode& prof,
                 std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
                 std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer);

  // Move to public since neither Vulkan or DX12 have a getUniformLocation API like OpenGL does.
  // They'll need to know the memory offset of each member in the shader structure
  struct Vertex {
    math::Vector<float, 3> xyz;
    math::Vector<u8, 4> rgba;
    math::Vector<float, 3> stq;
    u8 fog;
    u8 pad[3];
  };
  static_assert(sizeof(Vertex) == 32);

 protected:
  void InitializeVertexInputAttributes();

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffers[NUM_BUCKETS];
  } m_ogl;

  PipelineConfigInfo m_pipeline_config_info;
  GraphicsPipelineLayout m_pipeline_layout;
  VulkanInitializationInfo& m_vulkan_info;
};
