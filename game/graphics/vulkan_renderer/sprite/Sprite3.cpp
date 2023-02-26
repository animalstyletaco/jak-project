

#include "Sprite3.h"

#include "game/graphics/vulkan_renderer/background/background_common.h"
#include "game/graphics/general_renderer/dma_helpers.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

SpriteVulkan3::SpriteVulkan3(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& graphics_info)
    : BaseSprite3(name, my_id), BucketVulkanRenderer(device, graphics_info), m_direct(name, my_id, device, graphics_info, 1024),
      m_glow_renderer(device, graphics_info),
      m_distorted_pipeline_layout(device),
      m_distorted_instance_pipeline_layout(device),
      m_sampler_helper(device), m_distort_sampler_helper(device) {
  m_sprite_3d_vertex_uniform_buffer = std::make_unique<Sprite3dVertexUniformBuffer>(
      m_device, 1);

  m_sprite_3d_fragment_uniform_buffer = std::make_unique<Sprite3dFragmentUniformBuffer>(
    m_device, 1);

  m_sprite_3d_instanced_fragment_uniform_buffer =
      std::make_unique<SpriteDistortInstancedFragmentUniformBuffer>(m_device, 1);

  graphics_setup();
}

void SpriteVulkan3::graphics_setup() {
  // Set up OpenGL for 'normal' sprites
  graphics_setup_normal();

  // Set up OpenGL for distort sprites
  graphics_setup_distort();
}

void SpriteVulkan3::SetupShader(ShaderId shaderId) {
  auto& shader = m_vulkan_info.shaders[shaderId];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "main";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

 void SpriteVulkan3::render(DmaFollower& dma,
                     SharedVulkanRenderState* render_state,
                     ScopedProfilerNode& prof) {
  BaseSprite3::render(dma, render_state, prof);
 }


void SpriteVulkan3::graphics_setup_normal() {
  auto verts = SPRITE_RENDERER_MAX_SPRITES * 4;
  auto bytes = verts * sizeof(SpriteVertex3D);

  VkDeviceSize index_device_size = SPRITE_RENDERER_MAX_SPRITES * 5 * sizeof(u32);
  
  m_vertices_3d.resize(verts);
  m_index_buffer_data.resize(index_device_size);

  m_ogl.index_buffer = std::make_unique<IndexBuffer>(m_device, index_device_size, 1);

  m_vertices_3d.resize(verts);

  VkDeviceSize vertex_device_size = bytes;
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(m_device, vertex_device_size, 1);

  m_default_mode.disable_depth_write();
  m_default_mode.set_depth_test(GsTest::ZTest::GEQUAL);
  m_default_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_SRC_DST);
  m_default_mode.set_aref(38);
  m_default_mode.set_alpha_test(DrawMode::AlphaTest::GEQUAL);
  m_default_mode.set_alpha_fail(GsTest::AlphaFail::FB_ONLY);
  m_default_mode.set_at(true);
  m_default_mode.set_zt(true);
  m_default_mode.set_ab(true);

  m_current_mode = m_default_mode;

  m_distort_ogl.fbo = std::make_unique<VulkanTexture>(m_device);
  m_distort_ogl.fbo_texture = std::make_unique<VulkanTexture>(m_device);
}

void SpriteVulkan3::setup_graphics_for_2d_group_0_render() {
  // opengl sprite frame setup
  // auto shid = render_state->shaders[ShaderId::SPRITE3].id();
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("hvdf_offset", 1,
                                                               m_3d_matrix_data.hvdf_offset.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("pfog0", m_frame_data.pfog0);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("min_scale", m_frame_data.min_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("max_scale", m_frame_data.max_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_min", m_frame_data.fog_min);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_max", m_frame_data.fog_max);
  // glUniform1f(glGetUniformLocation(shid, "bonus"), m_frame_data.bonus);
  // glUniform4fv(glGetUniformLocation(shid, "hmge_scale"), 1, m_frame_data.hmge_scale.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("deg_to_rad", m_frame_data.deg_to_rad);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("inv_area", m_frame_data.inv_area);
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "camera", 1, GL_FALSE, m_3d_matrix_data.camera.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xy_array", 8,
                                                               m_frame_data.xy_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xyz_array", 4,
                                                               m_frame_data.xyz_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("st_array", 4,
                                                               m_frame_data.st_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_x", 1,
                                                               m_frame_data.basis_x.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_y", 1,
                                                               m_frame_data.basis_y.data());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Render (for real)

void SpriteVulkan3::flush_sprites(BaseSharedRenderState* render_state,
                            ScopedProfilerNode& prof,
                            bool double_draw) {
  // Enable prim restart, we need this to break up the triangle strips
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  // upload vertex buffer
  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices_3d.data());

  // two passes through the buckets. first to build the index buffer
  u32 idx_offset = 0;
  for (const auto bucket : m_bucket_list) {
    memcpy(&m_index_buffer_data[idx_offset], bucket->ids.data(), bucket->ids.size() * sizeof(u32));
    bucket->offset_in_idx_buffer = idx_offset;
    idx_offset += bucket->ids.size();
  }

  // now upload it
  m_ogl.index_buffer->writeToGpuBuffer(m_index_buffer_data.data());

  // now do draws!
  for (const auto bucket : m_bucket_list) {
    u32 tbp = bucket->key >> 32;
    DrawMode mode;
    mode.as_int() = bucket->key & 0xffffffff;

    VulkanTexture* tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp);

    if (!tex) {
      fmt::print("Failed to find texture at {}, using random\n", tbp);
      tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
    }
    ASSERT(tex);

    auto settings = vulkan_background_common::setup_vulkan_from_draw_mode(mode, m_sampler_helper, m_pipeline_config_info, false);

    m_sprite_3d_fragment_uniform_buffer->SetUniform1f("alpha_min", double_draw ? settings.aref_first : 0.016);
    m_sprite_3d_fragment_uniform_buffer->SetUniform1f("alpha_max", 10.f);

    prof.add_draw_call();
    prof.add_tri(2 * (bucket->ids.size() / 5));

    //glDrawElements(GL_TRIANGLE_STRIP, bucket->ids.size(), GL_UNSIGNED_INT,
    //               (void*)(bucket->offset_in_idx_buffer * sizeof(u32)));

    if (double_draw) {
      switch (settings.kind) {
        case DoubleDrawKind::NONE:
          break;
        case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
          prof.add_draw_call();
          prof.add_tri(2 * (bucket->ids.size() / 5));
          m_sprite_3d_instanced_fragment_uniform_buffer->SetUniform1f("alpha_min", -10.f);
          m_sprite_3d_instanced_fragment_uniform_buffer->SetUniform1f("alpha_max",
                                                                     settings.aref_second);
          //glDepthMask(GL_FALSE);
          //glDrawElements(GL_TRIANGLE_STRIP, bucket->ids.size(), GL_UNSIGNED_INT,
          //               (void*)(bucket->offset_in_idx_buffer * sizeof(u32)));
          break;
        default:
          ASSERT(false);
      }
    }
  }

  m_sprite_buckets.clear();
  m_bucket_list.clear();
  m_last_bucket_key = UINT64_MAX;
  m_last_bucket = nullptr;
  m_sprite_idx = 0;
}

void SpriteVulkan3::direct_renderer_reset_state() {
  m_direct.reset_state();
}

void SpriteVulkan3::direct_renderer_render_vif(u32 vif0,
                                         u32 vif1,
                                         const u8* data,
                                         u32 size,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  m_direct.render_vif(vif0, vif1, data, size, render_state, prof);
}
void SpriteVulkan3::direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                            ScopedProfilerNode& prof) {
  //m_direct.flush_pending(render_state, prof);
}
void SpriteVulkan3::SetSprite3UniformVertexFourFloatVector(const char* name,
                                                     u32 numberOfFloats,
                                                     float* data,
                                                     u32 flags) {
  m_sprite_3d_vertex_uniform_buffer->SetUniform4f(name, data[0], data[1], data[2], data[3], flags);
}
void SpriteVulkan3::SetSprite3UniformMatrixFourFloatVector(const char* name,
                                                     u32 numberOfMatrices,
                                                     bool isTransponsedMatrix,
                                                     float* data,
                                                     u32 flags) {
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(name, numberOfMatrices, isTransponsedMatrix, data, true);
}

void SpriteVulkan3::EnableSprite3GraphicsBlending() {
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}


Sprite3dVertexUniformBuffer::Sprite3dVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                        VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device, sizeof(Sprite3dVertexUniformShaderData), 1, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"hvdf_offset", offsetof(Sprite3dVertexUniformShaderData, hvdf_offset)},
      {"camera", offsetof(Sprite3dVertexUniformShaderData, camera)},
      {"hud_matrix", offsetof(Sprite3dVertexUniformShaderData, hud_matrix)},
      {"hud_hvdf_offset", offsetof(Sprite3dVertexUniformShaderData, hud_hvdf_offset)},
      {"hud_hvdf_user", offsetof(Sprite3dVertexUniformShaderData, hud_hvdf_user)},
      {"pfog0", offsetof(Sprite3dVertexUniformShaderData, pfog0)},
      {"fog_min", offsetof(Sprite3dVertexUniformShaderData, fog_min)},
      {"fog_max", offsetof(Sprite3dVertexUniformShaderData, fog_max)},
      {"min_scale", offsetof(Sprite3dVertexUniformShaderData, min_scale)},
      {"max_scale", offsetof(Sprite3dVertexUniformShaderData, max_scale)},
      {"deg_to_rad", offsetof(Sprite3dVertexUniformShaderData, deg_to_rad)},
      {"inv_area", offsetof(Sprite3dVertexUniformShaderData, inv_area)},
      {"basis_x", offsetof(Sprite3dVertexUniformShaderData, basis_x)},
      {"basis_y", offsetof(Sprite3dVertexUniformShaderData, basis_y)},
      {"xy_array", offsetof(Sprite3dVertexUniformShaderData, xy_array)},
      {"xyz_array", offsetof(Sprite3dVertexUniformShaderData, xyz_array)},
      {"st_array", offsetof(Sprite3dVertexUniformShaderData, st_array)},
      {"height_scale", offsetof(Sprite3dVertexUniformShaderData, height_scale)},
  };
}

Sprite3dFragmentUniformBuffer::Sprite3dFragmentUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(Sprite3dFragmentUniformShaderData),
                          1,
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"alpha_min", offsetof(Sprite3dFragmentUniformShaderData, alpha_min)},
      {"alpha_max", offsetof(Sprite3dFragmentUniformShaderData, alpha_max)},
  };
}

SpriteDistortInstancedFragmentUniformBuffer::SpriteDistortInstancedFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                              VkDeviceSize minOffsetAlignment)
      : UniformVulkanBuffer(device, sizeof(math::Vector4f), 1, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"u_color", 0}
  };
}

void SpriteVulkan3::glow_renderer_cancel_sprite() {
  m_glow_renderer.cancel_sprite();
}

SpriteGlowOutput* SpriteVulkan3::glow_renderer_alloc_sprite() {
  return m_glow_renderer.alloc_sprite();
}

void SpriteVulkan3::glow_renderer_flush(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  m_glow_renderer.flush(render_state, prof);
}
