#pragma once
#include "game/graphics/general_renderer/foreground/Merc2.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

struct MercLightControlUniformBufferVertexData {
  math::Vector3f light_dir0;
  math::Vector3f light_dir1;
  math::Vector3f light_dir2;
  math::Vector3f light_col0;
  math::Vector3f light_col1;
  math::Vector3f light_col2;
  math::Vector3f light_ambient;
};

struct MercUniformBufferVertexData {
  math::Vector4f hvdf_offset;
  math::Vector4f fog_constants;
};

struct EmercUniformBufferVertexData : MercUniformBufferVertexData {
  math::Vector4f fade;
};

class MercLightControlVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  MercLightControlVertexUniformBuffer(std::shared_ptr<GraphicsDeviceVulkan> device,
                                      uint32_t instanceCount,
                                      VkDeviceSize minOffsetAlignment = 1);
};

class MercVertexBoneUniformBuffer : public UniformVulkanBuffer {
  MercVertexBoneUniformBuffer(std::shared_ptr<GraphicsDeviceVulkan> device,
                              uint32_t instanceCount,
                              VkDeviceSize minOffsetAlignment = 1);
};

class MercVulkan2 : public BaseMerc2 {
 public:
  MercVulkan2(std::shared_ptr<GraphicsDeviceVulkan> device,
              VulkanInitializationInfo& vulkan_info,
              std::vector<VulkanTexture*>* slot_anim_array = nullptr);
  ~MercVulkan2();
  void init_shaders();
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof,
              BaseMercDebugStats* debug_stats);
  void reset_draw_count();

 protected:
  void handle_pc_model(const DmaTransfer& setup,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof,
                       BaseMercDebugStats*) override;
  void flush_draw_buckets(BaseSharedRenderState* render_state,
                          ScopedProfilerNode& prof,
                          BaseMercDebugStats*) override;
  void set_merc_uniform_buffer_data(const DmaTransfer& dma) override;

  struct alignas(float) TieVertexPushConstant {
    math::Matrix4f perspective_matrix;
    MercUniformBufferVertexData camera_control;
    float height_scale = 0;
    float scissor_adjust;
  } m_merc_vertex_push_constant;

  struct alignas(float) VertexEmercPushConstant {
    math::Matrix4f perspective_matrix;
    EmercUniformBufferVertexData etie_data;
    float height_scale = 0;
    float scissor_adjust;
  } m_emerc_vertex_push_constant;

  struct VulkanDraw : Draw {
    VulkanBuffer* mod_vtx_buffer = NULL;
  };

  struct alignas(float) MercUniformBufferFragmentData {
    int ignore_alpha;
    int settings;  // Stores hack variable and decal enable so it can fix within 128 byte push
                   // constant.
    math::Vector4f fog_color;
  };

  struct LevelDrawBucketVulkan {
    LevelDataVulkan* level = nullptr;
    std::vector<VulkanDraw> draws;
    std::vector<VulkanDraw> envmap_draws;
    std::vector<VulkanSamplerHelper> samplers;
    std::vector<VulkanSamplerHelper> emerc_samplers;
    std::vector<VkDescriptorImageInfo> descriptor_image_infos;
    u32 next_free_draw = 0;
    u32 next_free_envmap_draw = 0;

    void reset() {
      level = nullptr;
      next_free_draw = 0;
      next_free_envmap_draw = 0;
    }
  };

 private:
  void create_pipeline_layout();
  void draw_merc2(LevelDrawBucketVulkan& level_bucket, ScopedProfilerNode& prof);
  void draw_emercs(LevelDrawBucketVulkan& level_bucket, ScopedProfilerNode& prof);
  void InitializeInputAttributes();
  VulkanTexture* get_texture_at_draw_id(const LevelDataVulkan* level,
                                        const VulkanDraw& draw);

  VulkanDraw* alloc_normal_draw(const tfrag3::MercDraw& mdraw,
                                LevelDrawBucketVulkan* lev_bucket, const ModSettings&);
  VulkanDraw* try_alloc_envmap_draw(const tfrag3::MercDraw& mdraw,
                                    const DrawMode& envmap_mode,
                                    u32 envmap_texture,
                                    LevelDrawBucketVulkan* lev_bucket,
                                    const u8* fade, const ModSettings&);

  class MercBoneVertexUniformBuffer : public UniformVulkanBuffer {
   public:
    MercBoneVertexUniformBuffer(std::shared_ptr<GraphicsDeviceVulkan> device,
                                VkDeviceSize minOffsetAlignment = 1);
  };

  void do_mod_draws(
      const tfrag3::MercEffect& effect,
      LevelDrawBucketVulkan* lev_bucket,
      u8* fade_buffer,
      uint32_t index,
      ModSettings& settings);

  void model_mod_draws(int num_effects,
                     const tfrag3::MercModel* model,
                     const LevelDataVulkan* lev,
                     const u8* input_data,
                     const DmaTransfer& setup,
                     BaseMercDebugStats* stats);

  void model_mod_blerc_draws(int num_effects,
                             const tfrag3::MercModel* model,
                             const LevelDataVulkan* lev,
                             const float* blerc_weights,
                             BaseMercDebugStats* stats);

  void FinalizeVulkanDraw(uint32_t drawIndex,
                          LevelDrawBucketVulkan& lev_bucket,
                          VulkanTexture* texture,
                          VkDescriptorSet descriptor_sets,
                          bool isMercDraw);

  void alloc_mod_vtx_buffer(const LevelDataVulkan* level);

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VulkanInitializationInfo& m_vulkan_info;

  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;

  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;

  PipelineConfigInfo m_pipeline_config_info{};
  GraphicsPipelineLayout m_merc_graphics_pipeline_layout;

  MercUniformBufferFragmentData m_fragment_push_constant;
  std::vector<LevelDrawBucketVulkan> m_level_draw_buckets;

  VkDescriptorSet m_merc_vertex_descriptor_set = nullptr;
  std::vector<VkDescriptorSet> m_merc_fragment_descriptor_sets;

  std::unique_ptr<MercLightControlVertexUniformBuffer> m_light_control_vertex_uniform_buffer;
  std::unique_ptr<MercBoneVertexUniformBuffer> m_bone_vertex_uniform_buffer;

  VkDescriptorImageInfo m_placeholder_descriptor_image_info;
  VkDescriptorBufferInfo m_light_control_vertex_buffer_descriptor_info;
  VkDescriptorBufferInfo m_bone_vertex_buffer_descriptor_info;
  VkPipelineLayout m_merc_pipeline_layout = nullptr;

  // Emerc
  GraphicsPipelineLayout m_emerc_graphics_pipeline_layout;

  VkDescriptorSet m_emerc_vertex_descriptor_set = nullptr;
  std::vector<VkDescriptorSet> m_emerc_fragment_descriptor_sets;
  std::unique_ptr<DescriptorLayout> m_emerc_vertex_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_emerc_vertex_descriptor_writer;

  VkPipelineLayout m_emerc_pipeline_layout = nullptr;

  std::vector<VkPipelineShaderStageCreateInfo> m_merc_vertex_shader_stage_info;
  std::vector<VkPipelineShaderStageCreateInfo> m_emerc_vertex_shader_stage_info;

  EyeVulkanRenderer* m_eye_renderer = nullptr;
  std::unordered_map<u32, VertexBuffer*> m_mod_vtx_buffers;
  const std::vector<VulkanTexture*>* m_anim_slot_array = nullptr;
  
  u32 merc_draw_call_count = 0;
  u32 emerc_draw_call_count = 0;
};

class MercVulkan2Jak1 : public MercVulkan2 {
 public:
  MercVulkan2Jak1(std::shared_ptr<GraphicsDeviceVulkan> device,
                  VulkanInitializationInfo& vulkan_info)
      : MercVulkan2(device, vulkan_info) {
    m_merc_vertex_push_constant.height_scale = m_merc_vertex_push_constant.height_scale = 1;
    m_merc_vertex_push_constant.scissor_adjust = m_merc_vertex_push_constant.scissor_adjust =
        (-512 / 448.f);
  };
};
