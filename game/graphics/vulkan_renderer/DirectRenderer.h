#pragma once

#include <vector>

#include "common/dma/gs.h"
#include "common/log/log.h"
#include "common/math/Vector.h"
#include "common/util/SmallVector.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/DirectRenderer.h"

struct alignas(float) DirectBasicTexturedFragmentUniformShaderData {
  math::Vector4f fog_color;
  math::Vector4f game_sizes;
  float alpha_reject;
  float color_mult;
  float alpha_mult;
  float alpha_sub;
  float ta0;
  int scissor_enable;
};

/*!
 * The direct renderer will handle rendering GIFtags directly.
 * It's named after the DIRECT VIFCode which sends data directly to the GS.
 *
 * It should mostly be used for debugging/text stuff as this rendering style does all the math on
 * the EE and just sends geometry directly to the GS without using the VUs.
 *
 * It can be used as a BucketRenderer, or as a subcomponent of another renderer.
 */
class DirectVulkanRenderer : public BaseDirectRenderer, public BucketVulkanRenderer {
 public:
  DirectVulkanRenderer(const std::string& name,
                 int my_id,
                 std::shared_ptr<GraphicsDeviceVulkan> device,
                 VulkanInitializationInfo& vulkan_info,
                 int batch_size);
  ~DirectVulkanRenderer();
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;

  /*!
   * If you don't use the render interface, call this at the very end.
   */
  void flush_pending(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void set_current_index(u32 imageIndex);

 protected:
  void InitializeInputVertexAttribute();
  void SetShaderModule(ShaderId shader);

  void create_pipeline_layout() override;
  void update_graphics_prim(BaseSharedRenderState* render_state) override;
  void update_graphics_blend() override;
  void update_graphics_test() override;
  void update_graphics_texture(BaseSharedRenderState* render_state, int unit) override;
  void render_and_draw_buffers(BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof) override;
  void handle_trxdir(u64 dir,
                     BaseSharedRenderState* render_state,
                     ScopedProfilerNode& prof) override;
  void allocate_new_descriptor_set();

  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    u32 vertex_buffer_bytes = 0;
    u32 vertex_buffer_max_verts = 0;
    float color_mult = 1.0;
    float alpha_mult = 1.0;
  } m_ogl;

  struct {
    float height_scale;
    float scissor_adjust;
    int offscreen_mode;
  } m_textured_pipeline_push_constant;

  struct RendererGraphicsHelper {
    std::unique_ptr<VulkanSamplerHelper> sampler;
    std::unique_ptr<GraphicsPipelineLayout> graphics_pipeline_layout;

  };

  std::array<VkVertexInputAttributeDescription, 1> debugRedAttributeDescriptions{};
  std::array<VkVertexInputAttributeDescription, 2> directBasicAttributeDescriptions{};
  std::array<VkVertexInputAttributeDescription, 6> directBasicTexturedAttributeDescriptions{};

  std::unique_ptr<DescriptorLayout> m_direct_basic_fragment_descriptor_layout;

  VkDescriptorImageInfo m_descriptor_image_info{};
  DirectBasicTexturedFragmentUniformShaderData m_direct_basic_fragment_push_constant;

  std::vector<VkDescriptorSet> m_descriptor_sets;
  std::unordered_map<u32, RendererGraphicsHelper> m_graphics_helper_map;

  GraphicsPipelineLayout m_debug_graphics_pipeline_layout{m_device};

  VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_textured_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_debug_red_pipeline_layout = VK_NULL_HANDLE;

  u32 currentImageIndex = 0;
  u32 totalImageCount = 0;
};

