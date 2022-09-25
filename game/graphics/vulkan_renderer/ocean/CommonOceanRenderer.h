#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"


class CommonOceanVertexUniformBuffer : public UniformBuffer {
 public:
  CommonOceanVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                 VkDeviceSize instanceSize,
                                 uint32_t instanceCount,
                                 VkMemoryPropertyFlags memoryPropertyFlags,
                                 VkDeviceSize minOffsetAlignment = 1);
};

struct CommonOceanFragmentUniformShaderData {
  float color_mult;
  float alpha_mult;
  math::Vector4f fog_color;
  int32_t bucket;
  uint32_t tex_T0;
};

class CommonOceanFragmentUniformBuffer : public UniformBuffer {
 public:
  CommonOceanFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                   VkDeviceSize instanceSize,
                                   uint32_t instanceCount,
                                   VkMemoryPropertyFlags memoryPropertyFlags,
                                   VkDeviceSize minOffsetAlignment = 1);
};

class CommonOceanRenderer {
 public:
  CommonOceanRenderer();
  ~CommonOceanRenderer();

  void init_for_near();
  void kick_from_near(const u8* data);
  void flush_near(SharedRenderState* render_state,
                  ScopedProfilerNode& prof,
                  std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
                  std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer);

  void init_for_mid();
  void kick_from_mid(const u8* data);
  void flush_mid(SharedRenderState* render_state,
                 ScopedProfilerNode& prof,
                 std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
                 std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer);

  //Move to public since neither Vulkan or DX12 have a getUniformLocation API like OpenGL does.
  //They'll need to know the memory offset of each member in the shader structure
  struct Vertex {
    math::Vector<float, 3> xyz;
    math::Vector<u8, 4> rgba;
    math::Vector<float, 3> stq;
    u8 fog;
    u8 pad[3];
  };
  static_assert(sizeof(Vertex) == 32);

protected:
  void SetShaders(SharedRenderState* render_state);
  void InitializeVertexInputAttributes();

 private:
  void handle_near_vertex_gif_data_fan(const u8* data, u32 offset, u32 loop);
  void handle_near_vertex_gif_data_strip(const u8* data, u32 offset, u32 loop);

  void handle_near_adgif(const u8* data, u32 offset, u32 count);

  void handle_mid_adgif(const u8* data, u32 offset);

  enum VertexBucket {
    RGB_TEXTURE = 0,
    ALPHA = 1,
    ENV_MAP = 2,
  };
  u32 m_current_bucket = VertexBucket::RGB_TEXTURE;

  static constexpr int NUM_BUCKETS = 3;

  std::vector<Vertex> m_vertices;
  u32 m_next_free_vertex = 0;

  std::vector<u32> m_indices[NUM_BUCKETS];
  u32 m_next_free_index[NUM_BUCKETS] = {0};

  u32 m_envmap_tex = 0;

  struct {
    GLuint vertex_buffer, index_buffer[NUM_BUCKETS], vao;
  } m_ogl;
};

