#pragma once

#include "game/graphics/general_renderer/collision_common.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

struct CollisionMeshVertexUniformShaderData {
  math::Vector4f hvdf_offset;
  math::Matrix4f camera;
  math::Vector4f camera_position;
  float fog_constant;
  float fog_min;
  float fog_max;
};

struct PatColors {
  math::Vector4f pat_mode_colors[0x8];
  math::Vector4f pat_material_colors[0x40];
  math::Vector4f pat_event_colors[0x40];
};

class CollisionMeshVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  CollisionMeshVertexUniformBuffer(std::shared_ptr<GraphicsDeviceVulkan> device,
                                   uint32_t instanceCount,
                                   VkDeviceSize minOffsetAlignment = 1);
};

class CollisionMeshVertexPatternUniformBuffer : public UniformVulkanBuffer {
 public:
  CollisionMeshVertexPatternUniformBuffer(std::shared_ptr<GraphicsDeviceVulkan> device,
                                   uint32_t instanceCount,
                                   VkDeviceSize minOffsetAlignment = 1);
};

class CollideMeshVulkanRenderer {
 public:
  CollideMeshVulkanRenderer(std::shared_ptr<GraphicsDeviceVulkan> device, VulkanInitializationInfo& vulkan_info);
  void render(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);
  ~CollideMeshVulkanRenderer();

 private:
  void init_pat_colors(GameVersion version);
  void InitializeInputVertexAttribute();
  void init_shaders();
  void create_pipeline_layout();

  struct PushConstant {
    float scissor_adjust;

    s32 wireframe;
    s32 mode;

    u32 collision_mode_mask[(collision::PAT_MOD_COUNT + 31) / 32];
    u32 collision_event_mask[(collision::PAT_EVT_COUNT + 31) / 32];
    u32 collision_material_mask[(collision::PAT_MAT_COUNT + 31) / 32];
    u32 collision_skip_mask;
  } m_push_constant;

  PatColors m_colors;

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  CollisionMeshVertexUniformBuffer m_collision_mesh_vertex_uniform_buffer;

  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;
  VulkanInitializationInfo& m_vulkan_info;

  VkDescriptorBufferInfo m_vertex_buffer_descriptor_info;

  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::vector<VkDescriptorSet> m_descriptor_sets;
};
