#pragma once

#include <vector>

#include "common/common_types.h"
#include "common/dma/gs.h"

#include "game/graphics/general_renderer/DirectRenderer2.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class DirectVulkanRenderer2 : public BaseDirectRenderer2 {
 public:
  DirectVulkanRenderer2(std::shared_ptr<GraphicsDeviceVulkan> device,
                        VulkanInitializationInfo& vulkan_info,
                        u32 max_verts,
                        u32 max_inds,
                        u32 max_draws,
                        const std::string& name,
                        bool use_ftoi_mod);
  void reset_state();
  void render_gif_data(const u8* data,
                       SharedVulkanRenderState* render_state,
                       ScopedProfilerNode& prof, VkCommandBuffer command_buffer);
  void flush_pending(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);
  ~DirectVulkanRenderer2();

 private:
  void InitializeInputVertexAttribute();
  void InitializeShaderModule();

  void reset_buffers();

  void draw_call_loop_simple(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);
  void draw_call_loop_grouped(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
  } m_ogl;

  void CreatePipelineLayout();
  void setup_vulkan_for_draw_mode(const Draw& draw, SharedVulkanRenderState* render_state);
  void setup_vulkan_tex(u16 unit,
                        u16 tbp,
                        bool filter,
                        bool clamp_s,
                        bool clamp_t,
                        SharedVulkanRenderState* render_state);

  void handle_xyzf2_packed(const u8* data,
                           SharedVulkanRenderState* render_state,
                           ScopedProfilerNode& prof);

  bool m_use_ftoi_mod = false;
  void handle_xyzf2_mod_packed(const u8* data,
                               SharedVulkanRenderState* render_state,
                               ScopedProfilerNode& prof);

  std::shared_ptr<GraphicsDeviceVulkan> m_device;

  PipelineConfigInfo m_pipeline_config_info;
  VulkanInitializationInfo& m_vulkan_info;

  GraphicsPipelineLayout m_graphics_pipeline_layout{m_device};
  VulkanSamplerHelper m_sampler_helper{m_device};

  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;
  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;

  VkDescriptorImageInfo m_descriptor_image_info{};

  struct PushConstant {
    math::Vector4f fog_colors;
    float alpha_reject;
    float color_mult;
  } m_push_constant;

  VkCommandBuffer m_command_buffer = VK_NULL_HANDLE;
};
