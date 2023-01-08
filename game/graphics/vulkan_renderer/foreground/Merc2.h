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

struct MercUniformBufferVertexData {
  MercLightControlUniformBufferVertexData light_system;    // binding = 0
  MercCameraControlUniformBufferVertexData camera_system;  // binding = 1
  math::Matrix4f perspective_matrix;                       // binding = 2
};

class MercVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  MercVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
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
  void handle_merc_chain(DmaFollower& dma,
                         SharedVulkanRenderState* render_state,
                         ScopedProfilerNode& prof);

  protected:
    void init_for_frame(BaseSharedRenderState* render_state) override;
    void init_pc_model(const DmaTransfer& setup,
                       BaseSharedRenderState* render_state) override;
    void flush_draw_buckets(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
    void flush_pending_model(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
    void set_merc_uniform_buffer_data(const DmaTransfer& dma) override;

  private:
    void create_pipeline_layout() override;
    void InitializeInputAttributes();

  struct LevelDrawBucketVulkan {
    const LevelDataVulkan* level = nullptr;
    std::vector<Draw> draws;
    u32 next_free_draw = 0;

    void reset() {
      level = nullptr;
      next_free_draw = 0;
    }
  };

  class MercBoneVertexUniformBuffer : public UniformVulkanBuffer {
   public:
    MercBoneVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                VkDeviceSize minOffsetAlignment = 1);
  };

  std::optional<MercRefVulkan> m_current_model = std::nullopt;

  std::vector<LevelDrawBucketVulkan> m_level_draw_buckets;

  std::unique_ptr<MercVertexUniformBuffer> m_vertex_uniform_buffer;
  std::unique_ptr<MercBoneVertexUniformBuffer> m_bone_vertex_uniform_buffer;
  std::unique_ptr<MercFragmentUniformBuffer> m_fragment_uniform_buffer;
};

