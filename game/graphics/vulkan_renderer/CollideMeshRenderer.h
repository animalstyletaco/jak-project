#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

namespace collision {
const int PAT_MOD_COUNT = 3;
const int PAT_EVT_COUNT = 7;
const int PAT_MAT_COUNT = 23;
}  // namespace

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

class CollisionMeshVertexUniformBuffer : public UniformBuffer {
 public:
  CollisionMeshVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                   VkDeviceSize instanceSize,
                                   uint32_t instanceCount,
                                   VkMemoryPropertyFlags memoryPropertyFlags,
                                   VkDeviceSize minOffsetAlignment = 1);
};

class CollideMeshRenderer {
 public:
  CollideMeshRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device);
  void render(SharedRenderState* render_state, ScopedProfilerNode& prof);
  ~CollideMeshRenderer();

 private:
  void InitializeInputVertexAttribute();

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  UniformBuffer m_collision_mesh_vertex_uniform_buffer;
};
