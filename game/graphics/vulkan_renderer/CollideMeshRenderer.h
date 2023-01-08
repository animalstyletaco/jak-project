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
  s32 wireframe;
  s32 mode;

  u32 collision_mode_mask[(collision::PAT_MOD_COUNT + 31) / 32];
  u32 collision_event_mask[(collision::PAT_EVT_COUNT + 31) / 32];
  u32 collision_material_mask[(collision::PAT_MAT_COUNT + 31) / 32];
  u32 collision_skip_mask;
};

class CollisionMeshVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  CollisionMeshVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                   VkDeviceSize instanceSize,
                                   uint32_t instanceCount,
                                   VkDeviceSize minOffsetAlignment = 1);
};

class CollideMeshVulkanRenderer {
 public:
  CollideMeshVulkanRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info);
  void render(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);
  ~CollideMeshVulkanRenderer();

 private:
  void InitializeInputVertexAttribute();
  void init_shaders();
  void create_pipeline_layout();

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  UniformVulkanBuffer m_collision_mesh_vertex_uniform_buffer;

  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;
  VulkanInitializationInfo& m_vulkan_info;

  VkDescriptorBufferInfo m_vertex_buffer_descriptor_info;

  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::vector<VkDescriptorSet> m_descriptor_sets;
};
