#pragma once

#include "game/graphics/general_renderer/foreground/Generic2.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

struct GenericCommonVertexUniformShaderData {
  math::Vector4f scale;
  math::Vector4f hvdf_offset;
  math::Vector3f fog_constants;
  float mat_23;
  float mat_32;
  float mat_33;
};

struct GenericCommonFragmentPushConstantData {
  float alpha_reject;
  float color_mult;
  int hack_no_tex = 0;
  math::Vector4f fog_color;
};

class GenericVulkan2 : public BucketVulkanRenderer, public BaseGeneric2 {
 public:
  GenericVulkan2(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& vulkan_info,
                 u32 num_verts = 200000,
                 u32 num_frags = 2000,
                 u32 num_adgif = 6000,
                 u32 num_buckets = 800);
  ~GenericVulkan2();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void do_hud_draws(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void do_draws(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void do_draws_for_alpha(BaseSharedRenderState* render_state,
                          ScopedProfilerNode& prof,
                          DrawMode::AlphaBlend alpha,
                          bool hud);
  void init_shaders(VulkanShaderLibrary& shaders) override;

  struct Vertex {
    math::Vector<float, 3> xyz;
    math::Vector<u8, 4> rgba;
    math::Vector<float, 2> st;  // 16
    u8 tex_unit;
    u8 flags;
    u8 adc;
    u8 pad0;
    u32 pad1;
  };
  static_assert(sizeof(Vertex) == 32);

 private:
  void InitializeInputAttributes();
  void create_pipeline_layout() override;
  void graphics_setup() override;
  void graphics_cleanup() override;
  void graphics_bind_and_setup_proj(BaseSharedRenderState* render_state) override;
  void setup_graphics_for_draw_mode(const DrawMode& draw_mode,
                                  u8 fix,
                                  BaseSharedRenderState* render_state, uint32_t bucket);

  void setup_graphics_tex(u16 unit,
                          u16 tbp,
                          bool filter,
                          bool clamp_s,
                          bool clamp_t,
                          BaseSharedRenderState* render_state,
                          u32 bucketId);

 private:
  void FinalizeVulkanDraws(u32 bucket, u32 indexCount, u32 firstIndex);

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
  } m_ogl;

  struct alignas(float) VertexPushConstant : GenericCommonVertexUniformShaderData {
    float height_scale;
    float scissor_adjust;
    uint32_t warp_sample_mode;
  }m_vertex_push_constant;

  std::vector<VkDescriptorImageInfo> m_descriptor_image_infos;
  std::vector<VulkanSamplerHelper> m_samplers;

  VkDescriptorSet m_vertex_descriptor_set = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> m_fragment_descriptor_sets;

  GenericCommonFragmentPushConstantData m_fragment_push_constant;
};