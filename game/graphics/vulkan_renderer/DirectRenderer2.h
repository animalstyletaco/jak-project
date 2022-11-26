#pragma once

#include <vector>

#include "common/common_types.h"
#include "common/dma/gs.h"

#include "game/graphics/general_renderer/DirectRenderer2.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class DirectVulkanRenderer2 : public BaseDirectRenderer2 {
 public:
  DirectVulkanRenderer2(std::unique_ptr<GraphicsDeviceVulkan>& device,
                        VulkanInitializationInfo& vulkan_info,
                        u32 max_verts,
                        u32 max_inds,
                        u32 max_draws,
                        const std::string& name,
                        bool use_ftoi_mod);
  void reset_state();
  void render_gif_data(const u8* data,
                       SharedVulkanRenderState* render_state,
                       ScopedProfilerNode& prof,
                       UniformVulkanBuffer& uniform_buffer);
  void flush_pending(SharedVulkanRenderState* render_state,
                     ScopedProfilerNode& prof,
                     UniformVulkanBuffer& uniform_buffer);
  ~DirectVulkanRenderer2();

 private:
  void InitializeInputVertexAttribute();
  void InitializeShaderModule();

  void reset_buffers();

  void draw_call_loop_simple(SharedVulkanRenderState* render_state,
                             ScopedProfilerNode& prof,
                             UniformVulkanBuffer& uniform_buffer);
  void draw_call_loop_grouped(SharedVulkanRenderState* render_state,
                              ScopedProfilerNode& prof,
                              UniformVulkanBuffer& uniform_buffer);

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
    GLuint alpha_reject, color_mult, fog_color;
  } m_ogl;

  void setup_vulkan_for_draw_mode(const Draw& draw,
                                  SharedVulkanRenderState* render_state,
                                  UniformBuffer& uniform_buffer);
  void setup_vulkan_tex(u16 unit,
                        u16 tbp,
                        bool filter,
                        bool clamp_s,
                        bool clamp_t,
                        SharedVulkanRenderState* render_state);

  void handle_xyzf2_packed(const u8* data,
                           SharedVulkanRenderState* render_state,
                           ScopedProfilerNode& prof,
                           UniformVulkanBuffer& uniform_buffer);

  bool m_use_ftoi_mod = false;
  void handle_xyzf2_mod_packed(const u8* data,
                               SharedVulkanRenderState* render_state,
                               ScopedProfilerNode& prof,
                               UniformVulkanBuffer& uniform_buffer);

  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;
  VulkanInitializationInfo& m_vulkan_info;
};
