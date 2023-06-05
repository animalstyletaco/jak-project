
#pragma once

#include <map>

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/vulkan_renderer/FramebufferHelper.h"
#include "game/graphics/general_renderer/sprite/Sprite3.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"
#include "game/graphics/vulkan_renderer/sprite/GlowRenderer.h"

class Sprite3dVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  Sprite3dVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                              VkDeviceSize minOffsetAlignment);
};

class SpriteVulkan3 : public BaseSprite3, public BucketVulkanRenderer {
 public:
  SpriteVulkan3(const std::string& name,
          int my_id,
          std::unique_ptr<GraphicsDeviceVulkan>& device,
          VulkanInitializationInfo& vulkan_info);
  ~SpriteVulkan3();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void SetupShader(ShaderId shaderId) override;

 protected:
  void setup_graphics_for_2d_group_0_render() override;

 private:
  void graphics_setup() override;
  void graphics_setup_normal() override;
  void graphics_setup_distort() override;
  void create_pipeline_layout() override;

  void glow_renderer_cancel_sprite() override;
  SpriteGlowOutput* glow_renderer_alloc_sprite() override;
  void glow_renderer_flush(BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) override;

  void distort_draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void distort_draw_instanced(BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) override;
  void distort_draw_common(BaseSharedRenderState* render_state,
                           ScopedProfilerNode& prof) override;
  void distort_setup_framebuffer_dims(BaseSharedRenderState* render_state) override;

  void flush_sprites(BaseSharedRenderState* render_state,
                     ScopedProfilerNode& prof,
                     bool double_draw) override;

  struct VulkanDistortOgl : BaseSprite3::GraphicsDistortOgl {
    std::unique_ptr<VertexBuffer>
        vertex_buffer;  // contains vertex data for each possible sprite resolution (3-11)
    std::unique_ptr<IndexBuffer>
        index_buffer;  // contains all instance specific data for each sprite per frame

    std::unique_ptr<FramebufferVulkanHelper> fbo;
  };
  VulkanDistortOgl m_distort_ogl;

  struct VulkanDistortInstancedOgl {
    std::unique_ptr<VertexBuffer>
        vertex_buffer;  // contains vertex data for each possible sprite resolution (3-11)
    std::unique_ptr<VertexBuffer>
        instance_buffer;  // contains all instance specific data for each sprite per frame

  } m_vulkan_distort_instanced_ogl;

  void direct_renderer_reset_state() override;
  void direct_renderer_render_vif(u32 vif0,
                                  u32 vif1,
                                  const u8* data,
                                  u32 size,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof) override;
  void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& prof) override;
  void SetSprite3UniformVertexFourFloatVector(const char* name,
                                              u32 numberOfFloats,
                                              float* data,
                                              u32 flags = 0) override;
  void SetSprite3UniformMatrixFourFloatVector(const char* name,
                                              u32 numberOfFloats,
                                              bool isTransponsedMatrix,
                                              float* data,
                                              u32 flags = 0) override;
  void SetSprite3UniformVertexUserHvdfVector(const char* name,
                                             u32 totalBytes,
                                             float* data,
                                             u32 flags = 0) override;

  void EnableSprite3GraphicsBlending() override;

  DirectVulkanRenderer m_direct;
  GlowVulkanRenderer m_glow_renderer;

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
  } m_ogl;

  struct FragmentPushConstant {
    float alpha_min;
    float alpha_max;
  }m_sprite_fragment_push_constant;

  struct DistortPushConstant {
    math::Vector4f colors;
    float height_scale;
  } m_sprite_distort_push_constant;

  struct Sprite3GraphicsSettings {
    Sprite3GraphicsSettings(std::unique_ptr<DescriptorPool>& descriptorPool,
                            VkDescriptorSetLayout descriptorSetLayout,
                            u32 bucketSize) : m_descriptor_pool(descriptorPool){
      Reinitialize(descriptorSetLayout, bucketSize);
    }

    void Reinitialize(VkDescriptorSetLayout descriptorSetLayout, u32 bucket_size) {
      sampler_helpers.clear();
      pipeline_layouts.clear();
      descriptor_image_infos.clear();
      if (!fragment_descriptor_sets.empty()) {
        m_descriptor_pool->freeDescriptors(fragment_descriptor_sets);
      }

      sampler_helpers.resize(bucket_size, m_descriptor_pool->device());
      pipeline_layouts.resize(bucket_size, m_descriptor_pool->device());
      descriptor_image_infos.resize(bucket_size);

      fragment_descriptor_sets.resize(bucket_size);
      std::vector<VkDescriptorSetLayout> descriptorSetLayouts{bucket_size, descriptorSetLayout};
      if (!descriptorSetLayouts.empty()) {
        m_descriptor_pool->allocateDescriptor(descriptorSetLayouts.data(),
                                              fragment_descriptor_sets.data(),
                                              fragment_descriptor_sets.size());
      }
    }

    ~Sprite3GraphicsSettings() {
      if (!fragment_descriptor_sets.empty()) {
        m_descriptor_pool->freeDescriptors(fragment_descriptor_sets);
      }
    }

    std::vector<VulkanSamplerHelper> sampler_helpers;
    std::vector<GraphicsPipelineLayout> pipeline_layouts;
    std::vector<VkDescriptorImageInfo> descriptor_image_infos;
    std::vector<VkDescriptorSet> fragment_descriptor_sets;

    private:
    std::unique_ptr<DescriptorPool>& m_descriptor_pool;
  };

  GraphicsPipelineLayout m_distorted_pipeline_layout;
  GraphicsPipelineLayout m_distorted_instance_pipeline_layout;

  std::unique_ptr<Sprite3dVertexUniformBuffer> m_sprite_3d_vertex_uniform_buffer;

  std::unique_ptr<DescriptorLayout> m_sprite_distort_vertex_descriptor_layout;
  std::unique_ptr<DescriptorLayout> m_sprite_distort_fragment_descriptor_layout;

  std::unique_ptr<DescriptorWriter> m_sprite_distort_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_sprite_distort_fragment_descriptor_writer;

  std::unordered_map<uint32_t, Sprite3GraphicsSettings> m_sprite_graphics_settings_map;
  uint32_t m_flush_sprite_call_count = 0;

  VulkanSamplerHelper m_distort_sampler_helper;

  VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_sprite_distort_pipeline_layout = VK_NULL_HANDLE;

  std::vector<VkVertexInputBindingDescription> m_sprite_input_binding_descriptions;
  std::vector<VkVertexInputBindingDescription> m_sprite_distort_input_binding_descriptions;
  std::vector<VkVertexInputBindingDescription> m_sprite_distort_instanced_input_binding_descriptions;

  std::vector<VkVertexInputAttributeDescription> m_sprite_attribute_descriptions;
  std::vector<VkVertexInputAttributeDescription> m_sprite_distort_attribute_descriptions;
  std::vector<VkVertexInputAttributeDescription> m_sprite_distort_instanced_attribute_descriptions;

  VkDescriptorSet m_vertex_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_sprite_distort_fragment_descriptor_set = VK_NULL_HANDLE;

  VkDescriptorImageInfo m_sprite_distort_descriptor_image_info;

  void AllocateNewDescriptorMapElement();

  uint32_t m_direct_renderer_call_count = 0;
};
