#pragma once
#include "game/graphics/general_renderer/ocean/CommonOceanRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class CommonOceanVulkanRenderer : public BaseCommonOceanRenderer {
 public:
  CommonOceanVulkanRenderer(std::shared_ptr<GraphicsDeviceVulkan> device,
                            VulkanInitializationInfo& vulkan_info);
  virtual ~CommonOceanVulkanRenderer();

  void flush_near(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

  void flush_mid(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

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

  virtual u32 GetOceanTextureId() = 0;

  struct OceanVulkanGraphicsHelper {
    OceanVulkanGraphicsHelper(std::shared_ptr<GraphicsDeviceVulkan> device,
                              u32 index_count,
                              std::unique_ptr<DescriptorLayout>& setLayout,
                              std::unique_ptr<DescriptorPool>& descriptor_pool,
                              VkDescriptorImageInfo* defaultImageInfo);

    std::unique_ptr<IndexBuffer> index_buffers[NUM_BUCKETS];
    std::unique_ptr<DescriptorWriter> fragment_descriptor_writers[NUM_BUCKETS];

    VkDescriptorImageInfo descriptor_image_infos[NUM_BUCKETS];
    VkDescriptorSet descriptor_sets[NUM_BUCKETS];

    std::unique_ptr<VulkanSamplerHelper> ocean_samplers[NUM_BUCKETS];

    ~OceanVulkanGraphicsHelper();

   private:
    std::unique_ptr<DescriptorPool>& m_descriptor_pool;
  };

  std::unique_ptr<OceanVulkanGraphicsHelper> m_ocean_mid;
  std::unique_ptr<OceanVulkanGraphicsHelper> m_ocean_near;

  void FinalizeVulkanDraw(std::unique_ptr<OceanVulkanGraphicsHelper>& ocean_graphics,
                          VulkanTexture* texture,
                          uint32_t bucket);

  struct alignas(float) VertexPushConstant {
    float height_scale;
    float scissor_adjust;
    int bucket;
  } m_vertex_push_constant;

  struct alignas(float) FragmentPushConstant {
    int bucket;
    math::Vector4f fog_color;
    float color_mult;
    float alpha_mult;
  } m_fragment_push_constant;

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  std::unique_ptr<VertexBuffer> vertex_buffer;

  PipelineConfigInfo m_pipeline_config_info;
  VulkanInitializationInfo& m_vulkan_info;

  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;
  GraphicsPipelineLayout m_graphics_pipeline_layout{m_device};
};

class CommonOceanVulkanRendererJak1 : public CommonOceanVulkanRenderer {
 public:
  CommonOceanVulkanRendererJak1(std::shared_ptr<GraphicsDeviceVulkan> device,
                                VulkanInitializationInfo& vulkan_info)
      : CommonOceanVulkanRenderer(device, vulkan_info) {
    m_vertex_push_constant.height_scale = 1;
    m_vertex_push_constant.scissor_adjust = -512 / 448.0;
  }
  u32 GetOceanTextureId() override { return ocean_common::OCEAN_TEX_TBP_JAK1; };
};

class CommonOceanVulkanRendererJak2 : public CommonOceanVulkanRenderer {
 public:
  CommonOceanVulkanRendererJak2(std::shared_ptr<GraphicsDeviceVulkan> device,
                                VulkanInitializationInfo& vulkan_info)
      : CommonOceanVulkanRenderer(device, vulkan_info){};
  u32 GetOceanTextureId() override { return ocean_common::OCEAN_TEX_TBP_JAK2; };
};
