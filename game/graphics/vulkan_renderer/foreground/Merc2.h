#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/foreground/Merc2.h"

struct MercLightControlUniformBufferVertexData {
  math::Vector3f light_dir0;
  math::Vector3f light_dir1;
  math::Vector3f light_dir2;
  math::Vector3f light_col0;
  math::Vector3f light_col1;
  math::Vector3f light_col2;
  math::Vector3f light_ambient;
};

struct MercCameraControlUniformBufferVertexData {
  math::Vector4f hvdf_offset;
  math::Vector4f perspective0;
  math::Vector4f perspective1;
  math::Vector4f perspective2;
  math::Vector4f perspective3;
  math::Vector4f fog_constants;
};

struct MercPerspectiveMatrixUniformBufferVertexData {
  math::Matrix4f perspective_matrix;                       // binding = 2
};

struct EmercUniformBufferVertexData {
  math::Vector4f hvdf_offset;
  math::Vector4f fog_constants;
  math::Vector4f fade;
  math::Matrix4f perspective_matrix;
};

class MercLightControlVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  MercLightControlVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                          uint32_t instanceCount,
                          VkDeviceSize minOffsetAlignment = 1);
};

class MercCameraControlVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  MercCameraControlVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                       uint32_t instanceCount,
                                       VkDeviceSize minOffsetAlignment = 1);
};

class MercPerspectiveMatrixVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  MercPerspectiveMatrixVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                           uint32_t instanceCount,
                                           VkDeviceSize minOffsetAlignment = 1);
};

class EmercVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  EmercVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                           uint32_t instanceCount,
                           VkDeviceSize minOffsetAlignment = 1);
};

struct MercUniformBufferFragmentData {
  math::Vector4f fog_color;
  int ignore_alpha;
  int decal_enable;
  int gfx_hack_no_tex;
};

class MercFragmentUniformBuffer : public UniformVulkanBuffer {
 public:
  MercFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                            uint32_t instanceCount,
                            VkDeviceSize minOffsetAlignment = 1);
};

class MercVertexBoneUniformBuffer : public UniformVulkanBuffer {
  MercVertexBoneUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                              uint32_t instanceCount,
                              VkDeviceSize minOffsetAlignment = 1);
};

class MercVulkan2 : public BaseMerc2, public BucketVulkanRenderer {
 public:
  MercVulkan2(const std::string& name,
        int my_id,
        std::unique_ptr<GraphicsDeviceVulkan>& device,
        VulkanInitializationInfo& vulkan_info);
  ~MercVulkan2();
  void init_shaders();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

  protected:
    void handle_pc_model(const DmaTransfer& setup,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof) override;
    void flush_draw_buckets(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
    void set_merc_uniform_buffer_data(const DmaTransfer& dma) override;

    std::unique_ptr<VertexBuffer> vertex;

    struct PushConstant {
      float height_scale = 0;
    } m_push_constant;

    struct VulkanDraw : Draw {
      std::unique_ptr<VulkanBuffer> mod_vtx_buffer;
    };

    struct LevelDrawBucketVulkan {
      LevelDataVulkan* level = nullptr;
      std::vector<VulkanDraw> draws;
      std::vector<VulkanDraw> envmap_draws;
      std::vector<VulkanSamplerHelper> samplers;
      std::vector<VkDescriptorImageInfo> descriptor_image_infos;
      std::vector<GraphicsPipelineLayout> pipeline_layouts;
      u32 next_free_draw = 0;
      u32 next_free_envmap_draw = 0;

      void reset() {
        level = nullptr;
        next_free_draw = 0;
      }
    };

  private:
    void create_pipeline_layout() override;
    void draw_merc2(LevelDrawBucketVulkan& level_bucket, ScopedProfilerNode& prof);
    void draw_emercs(LevelDrawBucketVulkan& level_bucket, ScopedProfilerNode& prof);
    void InitializeInputAttributes();
   
    VulkanDraw* alloc_normal_draw(const tfrag3::MercDraw& mdraw,
                                  bool ignore_alpha,
                                  LevelDrawBucketVulkan* lev_bucket,
                                  u32 first_bone,
                                  u32 lights);
    VulkanDraw* try_alloc_envmap_draw(const tfrag3::MercDraw& mdraw,
                                      const DrawMode& envmap_mode,
                                      u32 envmap_texture,
                                      LevelDrawBucketVulkan* lev_bucket,
                                      const u8* fade,
                                      u32 first_bone,
                                      u32 lights);

  class MercBoneVertexUniformBuffer : public UniformVulkanBuffer {
   public:
    MercBoneVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                VkDeviceSize minOffsetAlignment = 1);
  };

  struct ModSettings {
    int num_effects = 0;                 // sanity check
    int current_ignore_alpha_bits = 0;   // shader settings
    int current_effect_enable_bits = 0;  // mask for game to disable an effect
    bool model_uses_mod = false;         // if we should update vertices from game.
  };

void MercVulkan2::do_mod_draws(const tfrag3::MercEffect& effect,
                               ModSettings mod_settings,
                               LevelDrawBucketVulkan* lev_bucket,
                               u8* fade_buffer,
                               uint32_t index,
                               uint32_t first_bone,
                               uint32_t lights,
      std::unordered_map<uint32_t, std::unique_ptr<VertexBuffer>>& mod_graphics_buffers);

  std::vector<LevelDrawBucketVulkan> m_level_draw_buckets;

  std::unique_ptr<MercLightControlVertexUniformBuffer> m_light_control_vertex_uniform_buffer;
  std::unique_ptr<MercCameraControlVertexUniformBuffer> m_camera_control_vertex_uniform_buffer;
  std::unique_ptr<MercPerspectiveMatrixVertexUniformBuffer> m_perspective_matrix_vertex_uniform_buffer;
  std::unique_ptr<MercBoneVertexUniformBuffer> m_bone_vertex_uniform_buffer;
  std::unique_ptr<MercFragmentUniformBuffer> m_fragment_uniform_buffer;

  VkDescriptorImageInfo m_placeholder_descriptor_image_info;
  VkDescriptorBufferInfo m_light_control_vertex_buffer_descriptor_info;
  VkDescriptorBufferInfo m_camera_control_vertex_buffer_descriptor_info;
  VkDescriptorBufferInfo m_perspective_matrix_vertex_buffer_descriptor_info;
  VkDescriptorBufferInfo m_bone_vertex_buffer_descriptor_info;

  // Emerc
  std::vector<VkDescriptorSet> m_emerc_descriptor_sets;
  std::unique_ptr<DescriptorLayout> m_emerc_vertex_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_emerc_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_emerc_fragment_descriptor_writer;
  std::unique_ptr<EmercVertexUniformBuffer> m_emerc_vertex_uniform_buffer;

  std::vector<GraphicsPipelineLayout> m_emerc_pipeline_layouts;
  PipelineConfigInfo m_emerc_pipeline_config_info;

  VkDescriptorBufferInfo m_emerc_vertex_buffer_descriptor_info{};
};

