#pragma once

#include "common/dma/gs.h"
#include "common/math/Vector.h"

#include "game/graphics/general_renderer/SpriteRenderer.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/SpriteCommon.h"

class SpriteVulkanRenderer : public BaseSpriteRenderer, public BucketVulkanRenderer {
 public:
  SpriteVulkanRenderer(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  static constexpr int SPRITES_PER_CHUNK = 48;

 private:
  void InitializeInputVertexAttribute();
  void render_2d_group0(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  void render_2d_group1(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  void graphics_sprite_frame_setup() override;
  void update_graphics_prim(BaseSharedRenderState* render_state) override;
  void update_graphics_texture(BaseSharedRenderState* render_state, int unit) override;
  void flush_sprites(BaseSharedRenderState* render_state, ScopedProfilerNode& prof);
  void update_graphics_blend(AdGifState& state) override;


  struct {
    std::unique_ptr<VertexBuffer> vertex_buffer;
  } m_ogl;

  std::unique_ptr<Sprite3dVertexUniformBuffer> m_sprite_3d_vertex_uniform_buffer;
  std::unique_ptr<Sprite3dFragmentUniformBuffer> m_sprite_3d_fragment_uniform_buffer;
};

