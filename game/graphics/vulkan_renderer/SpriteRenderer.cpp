#include "SpriteRenderer.h"

#include "game/graphics/general_renderer/dma_helpers.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

SpriteVulkanRenderer::SpriteVulkanRenderer(const std::string& name,
                               int my_id,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               VulkanInitializationInfo& vulkan_info)
    : BaseSpriteRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  auto verts = SPRITE_RENDERER_MAX_SPRITES * 3 * 2;

  m_vertices_3d.resize(verts);
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(m_device, sizeof(SpriteVertex3D), verts, 1);
}

void SpriteVulkanRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(SpriteVertex3D);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(SpriteVertex3D, xyz_sx);

    // TODO: This value needs to be normalized
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 0;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(SpriteVertex3D, quat_sy);

    // TODO: This value needs to be normalized
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 0;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(SpriteVertex3D, rgba);

    // TODO: This value needs to be normalized
  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 0;
  attributeDescriptions[3].format = VK_FORMAT_R16G16_UINT;
  attributeDescriptions[3].offset = offsetof(SpriteVertex3D, flags_matrix);

    // TODO: This value needs to be normalized
  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 0;
  attributeDescriptions[4].format = VK_FORMAT_R16G16B16A16_UINT;
  attributeDescriptions[4].offset = offsetof(SpriteVertex3D, info);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void SpriteVulkanRenderer::render_2d_group0(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  // opengl sprite frame setup
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("hvdf_offset", 1, m_3d_matrix_data.hvdf_offset.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("pfog0", m_frame_data.pfog0);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("min_scale", m_frame_data.min_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("max_scale", m_frame_data.max_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_min", m_frame_data.fog_min);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_max", m_frame_data.fog_max);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("bonus", m_frame_data.bonus);
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("hmge_scale", 1,
                                                               m_frame_data.hmge_scale.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("deg_to_rad", m_frame_data.deg_to_rad);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("inv_area", m_frame_data.inv_area);
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory("camera", 1, GL_FALSE, m_3d_matrix_data.camera.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xy_array", 8, m_frame_data.xy_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xyz_array", 4, m_frame_data.xyz_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("st_array", 4, m_frame_data.st_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_x", 1, m_frame_data.basis_x.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_y", 1, m_frame_data.basis_y.data());

  u16 last_prog = -1;

  while (sprite_common::looks_like_2d_chunk_start(dma)) {
    m_debug_stats.blocks_2d_grp0++;
    // 4 packets per chunk

    // first is the header
    u32 sprite_count = sprite_common::process_sprite_chunk_header(dma);
    m_debug_stats.count_2d_grp0 += sprite_count;

    // second is the vector data
    u32 expected_vec_size = sizeof(SpriteVecData2d) * sprite_count;
    auto vec_data = dma.read_and_advance();
    ASSERT(expected_vec_size <= sizeof(m_vec_data_2d));
    unpack_to_no_stcycl(&m_vec_data_2d, vec_data, VifCode::Kind::UNPACK_V4_32, expected_vec_size,
                        SpriteDataMem::Vector, false, true);

    // third is the adgif data
    u32 expected_adgif_size = sizeof(AdGifData) * sprite_count;
    auto adgif_data = dma.read_and_advance();
    ASSERT(expected_adgif_size <= sizeof(m_adgif));
    unpack_to_no_stcycl(&m_adgif, adgif_data, VifCode::Kind::UNPACK_V4_32, expected_adgif_size,
                        SpriteDataMem::Adgif, false, true);

    // fourth is the actual run!!!!!
    auto run = dma.read_and_advance();
    ASSERT(run.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(run.vifcode1().kind == VifCode::Kind::MSCAL);

    if (m_enabled) {
      if (run.vifcode1().immediate != last_prog) {
        // one-time setups and flushing
        flush_sprites(render_state, prof);
        if (run.vifcode1().immediate == SpriteProgMem::Sprites2dGrp0 &&
            m_prim_graphics_state.current_register != m_frame_data.sprite_2d_giftag.prim()) {
          m_prim_graphics_state.from_register(m_frame_data.sprite_2d_giftag.prim());
        } else if (m_prim_graphics_state.current_register != m_frame_data.sprite_3d_giftag.prim()) {
          m_prim_graphics_state.from_register(m_frame_data.sprite_3d_giftag.prim());
        }
      }

      if (run.vifcode1().immediate == SpriteProgMem::Sprites2dGrp0) {
        if (m_2d_enable) {
          do_block_common(SpriteMode::Mode2D, sprite_count, render_state, prof);
        }
      } else {
        if (m_3d_enable) {
          do_block_common(SpriteMode::Mode3D, sprite_count, render_state, prof);
        }
      }
      last_prog = run.vifcode1().immediate;
    }
  }
}

void SpriteVulkanRenderer::render(DmaFollower& dma,
                                  SharedVulkanRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  BaseSpriteRenderer::render(dma, render_state, prof);

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Render (for real)

void SpriteVulkanRenderer::flush_sprites(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  for (int i = 0; i <= m_adgif_index; ++i) {
    update_graphics_texture(render_state, i);
  }

  if (m_sprite_offset == 0) {
    // nothing to render
    m_adgif_index = 0;
    return;
  }

  update_graphics_blend(m_adgif_state_stack[m_adgif_index]);

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;

  if (m_adgif_state_stack[m_adgif_index].z_write) {
    m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_TRUE;
  } else {
    m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  }

  // render!

  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices_3d.data());

  //glDrawArrays(GL_TRIANGLES, 0, m_sprite_offset * 6);

  int n_tris = m_sprite_offset * 6 / 3;
  prof.add_tri(n_tris);
  prof.add_draw_call(1);

  m_sprite_offset = 0;
  m_adgif_index = 0;
}

void SpriteVulkanRenderer::update_graphics_blend(AdGifState& state) {
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  if (m_prim_graphics_state.alpha_blend_enable) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    if (state.a == GsAlpha::BlendMode::SOURCE && state.b == GsAlpha::BlendMode::DEST &&
        state.c == GsAlpha::BlendMode::SOURCE && state.d == GsAlpha::BlendMode::DEST) {
      // (Cs - Cd) * As + Cd
      // Cs * As  + (1 - As) * Cd
      // s, d
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (state.a == GsAlpha::BlendMode::SOURCE &&
               state.b == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               state.c == GsAlpha::BlendMode::SOURCE && state.d == GsAlpha::BlendMode::DEST) {
      // (Cs - 0) * As + Cd
      // Cs * As + (1) * Cd
      // s, d

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (state.a == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               state.b == GsAlpha::BlendMode::SOURCE && state.c == GsAlpha::BlendMode::SOURCE &&
               state.d == GsAlpha::BlendMode::DEST) {
      // (0 - Cs) * As + Cd
      // Cd - Cs * As
      // s, d

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else {
      // unsupported blend: a 0 b 2 c 2 d 1
      lg::error("unsupported blend: a {} b {} c {} d {} NOTE THIS DOWN IMMEDIATELY!!", (int)state.a,
                (int)state.b, (int)state.c, (int)state.d);
      ASSERT(false);
    }
  }
}

void SpriteVulkanRenderer::update_graphics_texture(BaseSharedRenderState* render_state, int unit) {
  VulkanTexture* tex;
  auto& state = m_adgif_state_stack[unit];
  if (!state.used) {
    // nothing used this state, don't bother binding the texture.
    return;
  }
  if (state.using_mt4hh) {
    tex = m_vulkan_info.texture_pool->lookup_mt4hh_vulkan_texture(state.texture_base_ptr);
  } else {
    tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(state.texture_base_ptr);
  }

  if (!tex) {
    fmt::print("Failed to find texture at {}, using random\n", state.texture_base_ptr);
    tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
  }
  ASSERT(tex);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  // samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  // ST was used in OpenGL, UV is used in Vulkan
  if (state.clamp_s) {
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  if (state.clamp_t) {
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }

  if (state.enable_tex_filt) {
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
  } else {
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
  }

  state.used = false;
}

void SpriteVulkanRenderer::graphics_sprite_frame_setup() {
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat(
      "hud_hvdf_offset", 1, m_hud_matrix_data.hvdf_offset.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat(
      "hud_hvdf_user", 75, m_hud_matrix_data.user_hvdf[0].data());
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "hud_matrix", 1, GL_FALSE, m_hud_matrix_data.matrix.data());
}
