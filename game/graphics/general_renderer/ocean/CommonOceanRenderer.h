#pragma once

#include "common/log/log.h"
#include "game/graphics/general_renderer/BucketRenderer.h"
#include <exception>

namespace ocean_common {
   static constexpr int OCEAN_TEX_TBP_JAK1 = 8160;  // todo
   static constexpr int OCEAN_TEX_TBP_JAK2 = 672;
}  // namespace ocean_common

class BaseCommonOceanRenderer {
 public:
  BaseCommonOceanRenderer();
  ~BaseCommonOceanRenderer();

  void init_for_near();
  void kick_from_near(const u8* data);

  void init_for_mid();
  void kick_from_mid(const u8* data);

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
  void reverse_indices(u32* indices, u32 count);

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
};

