#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/ocean/CommonOceanRenderer.h"

struct CommonOceanFragmentUniformShaderData {
  float color_mult;
  float alpha_mult;
  math::Vector4f fog_color;
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
                  ScopedProfilerNode& prof);

  void flush_mid(BaseSharedRenderState* render_state,
                 ScopedProfilerNode& prof);

  // Move to public since neither Vulkan or DX12 have a getUniformLocation API like OpenGL does.
  // They'll need to know the memory offset of each member in the shader structure
  struct Vertex {
    math::Vector<float, 3> xyz;
    math::Vector<u8, 4> rgba;
    math::Vector<float, 3> stq;
    u32 fog;
  };
  static_assert(sizeof(Vertex) == 32);

 protected:
  void InitializeVertexInputAttributes();
  void CreatePipelineLayout();
  void InitializeShaders();

  struct OceanVulkanGraphicsHelper {
    void Initialize(std::unique_ptr<GraphicsDeviceVulkan>& device,
                    u32 index_count,
                    std::unique_ptr<DescriptorLayout>& setLayout,
                    std::unique_ptr<DescriptorPool>& descriptor_pool,
                    VkDescriptorImageInfo* defaultImageInfo);

    std::unique_ptr<IndexBuffer> index_buffers[NUM_BUCKETS];

    std::unique_ptr<CommonOceanFragmentUniformBuffer>
        m_uniform_fragment_buffers[NUM_BUCKETS];
    std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writers[NUM_BUCKETS];
    
    VkDescriptorBufferInfo m_fragment_buffer_descriptor_infos[NUM_BUCKETS];
    VkDescriptorImageInfo m_descriptor_image_infos[NUM_BUCKETS];
    VkDescriptorSet m_descriptor_sets[NUM_BUCKETS];

    std::unique_ptr<GraphicsPipelineLayout> m_pipeline_layouts[NUM_BUCKETS];
    std::unique_ptr<VulkanSamplerHelper> m_ocean_samplers[NUM_BUCKETS];
  };

  OceanVulkanGraphicsHelper m_ocean_mid;
  OceanVulkanGraphicsHelper m_ocean_near;

  void FinalizeVulkanDraw(OceanVulkanGraphicsHelper& ocean_graphics,
                        VulkanTexture* texture,
                        uint32_t bucket);

  struct PushConstant{
    int bucket = 0;
    float scissor_adjust = 0;
  }m_push_constant;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<VertexBuffer> vertex_buffer;

  PipelineConfigInfo m_pipeline_config_info;
  VulkanInitializationInfo& m_vulkan_info;

  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;
};
