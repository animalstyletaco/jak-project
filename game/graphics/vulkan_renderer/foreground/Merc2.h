#pragma once
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

struct MercCameraControlUniformBufferVertexData {
  math::Vector4f hvdf_offset;
  math::Vector4f perspective0;
  math::Vector4f perspective1;
  math::Vector4f perspective2;
  math::Vector4f perspective3;
  math::Vector4f fog_constants;
};

struct MercUniformBufferVertexData {
  MercLightControlUniformBufferVertexData light_system;    // binding = 0
  MercCameraControlUniformBufferVertexData camera_system;  // binding = 1
  math::Matrix4f perspective_matrix;                       // binding = 2
};

class MercVertexUniformBuffer : public UniformBuffer {
 public:
  MercVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                          VkDeviceSize instanceSize,
                          uint32_t instanceCount,
                          VkMemoryPropertyFlags memoryPropertyFlags,
                          VkDeviceSize minOffsetAlignment = 1);
};

struct MercUniformBufferFragmentData {
  math::Vector4f fog_color;
  int ignore_alpha;
  int decal_enable;
};

class MercFragmentUniformBuffer : public UniformBuffer {
 public:
  MercFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                            VkDeviceSize instanceSize,
                            uint32_t instanceCount,
                            VkMemoryPropertyFlags memoryPropertyFlags,
                            VkDeviceSize minOffsetAlignment = 1);
};

class Merc2 : public BucketRenderer {
 public:
  Merc2(const std::string& name,
        BucketId my_id,
        std::unique_ptr<GraphicsDeviceVulkan>& device,
        VulkanInitializationInfo& vulkan_info);
  ~Merc2();
  void draw_debug_window() override;
  void init_shaders(ShaderLibrary& shaders) override;
  void render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void handle_merc_chain(DmaFollower& dma,
                         SharedRenderState* render_state,
                         ScopedProfilerNode& prof);

 protected:
  void InitializeVertexBuffer(SharedRenderState* render_state);

 private:
  enum MercDataMemory {
    LOW_MEMORY = 0,
    BUFFER_BASE = 442,
    // this negative offset is what broke jak graphics in Dobiestation for a long time.
    BUFFER_OFFSET = -442
  };

  struct LowMemory {
    u8 tri_strip_tag[16];
    u8 ad_gif_tag[16];
    math::Vector4f hvdf_offset;
    math::Vector4f perspective[4];
    math::Vector4f fog;
  } m_low_memory;
  static_assert(sizeof(LowMemory) == 0x80);

  struct VuLights {
    math::Vector3f direction0;
    u32 w0;
    math::Vector3f direction1;
    u32 w1;
    math::Vector3f direction2;
    u32 w2;
    math::Vector3f color0;
    u32 w3;
    math::Vector3f color1;
    u32 w4;
    math::Vector3f color2;
    u32 w5;
    math::Vector3f ambient;
    u32 w6;
  };

  void init_for_frame(SharedRenderState* render_state);
  void init_pc_model(const DmaTransfer& setup, SharedRenderState* render_state);
  void handle_all_dma(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof);
  void handle_setup_dma(DmaFollower& dma, SharedRenderState* render_state);
  u32 alloc_lights(const VuLights& lights);
  void set_lights(const DmaTransfer& dma);
  void handle_matrix_dma(const DmaTransfer& dma);
  void flush_pending_model(SharedRenderState* render_state, ScopedProfilerNode& prof);

  u32 alloc_bones(int count);

  std::optional<MercRef> m_current_model = std::nullopt;
  u16 m_current_effect_enable_bits = 0;
  u16 m_current_ignore_alpha_bits = 0;

  struct MercMat {
    math::Vector4f tmat[4];
    math::Vector4f nmat[3];
  };

  struct ShaderMercMat {
    math::Vector4f tmat[4];
    math::Vector4f nmat[3];
    math::Vector4f pad;
  };

  static constexpr int MAX_SKEL_BONES = 128;
  static constexpr int BONE_VECTORS_PER_BONE = 7;
  static constexpr int MAX_SHADER_BONE_VECTORS = 1024 * 32;  // ??

  static constexpr int MAX_LEVELS = 3;
  static constexpr int MAX_DRAWS_PER_LEVEL = 1024;

  math::Vector4f m_shader_bone_vector_buffer[MAX_SHADER_BONE_VECTORS];
  ShaderMercMat m_skel_matrix_buffer[MAX_SKEL_BONES];

  struct UniformData {
    VuLights light_control;
    math::Vector4f hvdf_offset;
    math::Vector4f perspective[4];
    math::Vector4f fog;
    math::Matrix4f perspective_matrix;
    math::Vector4f fog_color;
    s32 ignore_alpha;
    s32 pad[3]; //TODO: Verify that this padding is necessary
  };

  struct Stats {
    int num_models = 0;
    int num_chains = 0;
    int num_effects = 0;
    int num_predicted_draws = 0;
    int num_predicted_tris = 0;
    int num_bones_uploaded = 0;
    int num_lights = 0;
    int num_draw_flush = 0;
  } m_stats;

  struct Draw {
    u32 first_index;
    u32 index_count;
    DrawMode mode;
    u32 texture;
    u32 num_triangles;
    u16 first_bone;
    u16 light_idx;
    u8 ignore_alpha;
  };

  struct LevelDrawBucket {
    LevelData* level = nullptr;
    std::vector<Draw> draws;
    u32 next_free_draw = 0;

    void reset() {
      level = nullptr;
      next_free_draw = 0;
    }
  };

  static constexpr int MAX_LIGHTS = 1024;
  VuLights m_lights_buffer[MAX_LIGHTS];
  u32 m_next_free_light = 0;
  VuLights m_current_lights;

  std::vector<LevelDrawBucket> m_level_draw_buckets;
  u32 m_next_free_level_bucket = 0;
  u32 m_next_free_bone_vector = 0;
  size_t m_vulkan_buffer_alignment = 0;

  void flush_draw_buckets(SharedRenderState* render_state, ScopedProfilerNode& prof);
  std::unique_ptr<MercVertexUniformBuffer> m_vertex_uniform_buffer;
  std::unique_ptr<MercFragmentUniformBuffer> m_fragment_uniform_buffer;
};
