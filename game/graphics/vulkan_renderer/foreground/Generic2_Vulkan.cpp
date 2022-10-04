#include "Generic2.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

void Generic2::vulkan_setup() {
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
    m_device, sizeof(Vertex), m_verts.size(),
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);
  m_ogl.index_buffer = std::make_unique<IndexBuffer>(
      m_device, sizeof(u32), m_indices.size(),
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

  m_vertex_uniform_buffer = std::make_unique<GenericCommonVertexUniformBuffer>(
    m_device, sizeof(GenericCommonVertexUniformShaderData),
    1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);
  m_fragment_uniform_buffer = std::make_unique<GenericCommonFragmentUniformBuffer>(
      m_device, sizeof(GenericCommonFragmentUniformShaderData), 1,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  auto vertex_buffer_descriptor_info = m_vertex_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_fragment_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);
}

void Generic2::vulkan_cleanup() {

}

void Generic2::init_shaders(ShaderLibrary& shaders) {
}

void Generic2::vulkan_bind_and_setup_proj(SharedRenderState* render_state) {
  m_fragment_uniform_buffer->SetUniform4f(
              "fog_color", render_state->fog_color[0] / 255.f,
              render_state->fog_color[1] / 255.f, render_state->fog_color[2] / 255.f,
              render_state->fog_intensity / 255);
  m_vertex_uniform_buffer->SetUniform4f("scale", m_drawing_config.proj_scale[0],
                                        m_drawing_config.proj_scale[1],
              m_drawing_config.proj_scale[2], 0);
  m_vertex_uniform_buffer->SetUniform1f("mat_23", m_drawing_config.proj_mat_23);
  m_vertex_uniform_buffer->SetUniform1f("mat_32", m_drawing_config.proj_mat_32);
  m_vertex_uniform_buffer->SetUniform1f("mat_33", 0);
  m_vertex_uniform_buffer->SetUniform3f(
              "fog_constants", m_drawing_config.pfog0, m_drawing_config.fog_min,
              m_drawing_config.fog_max);
  m_vertex_uniform_buffer->SetUniform4f(
              "hvdf_offset", m_drawing_config.hvdf_offset[0], m_drawing_config.hvdf_offset[1],
              m_drawing_config.hvdf_offset[2], m_drawing_config.hvdf_offset[3]);
}

void Generic2::setup_vulkan_for_draw_mode(const DrawMode& draw_mode,
                                          u8 fix,
                                          SharedRenderState* render_state) {
  // compute alpha_reject:
  float alpha_reject = 0.f;
  if (draw_mode.get_at_enable()) {
    switch (draw_mode.get_alpha_test()) {
      case DrawMode::AlphaTest::ALWAYS:
        break;
      case DrawMode::AlphaTest::GEQUAL:
        alpha_reject = draw_mode.get_aref() / 128.f;
        break;
      case DrawMode::AlphaTest::NEVER:
        break;
      default:
        ASSERT_MSG(false, fmt::format("unknown alpha test: {}", (int)draw_mode.get_alpha_test()));
    }
  }

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  if (draw_mode.get_ab_enable() && draw_mode.get_alpha_blend() != DrawMode::AlphaBlend::DISABLED) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    switch (draw_mode.get_alpha_blend()) {
      case DrawMode::AlphaBlend::SRC_DST_SRC_DST:
        // glBlendEquation(GL_FUNC_ADD);
        // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        break;
      case DrawMode::AlphaBlend::SRC_0_SRC_DST:
        // glBlendEquation(GL_FUNC_ADD);
        // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
      case DrawMode::AlphaBlend::SRC_0_FIX_DST:
        // glBlendEquation(GL_FUNC_ADD);
        // glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
      case DrawMode::AlphaBlend::SRC_DST_FIX_DST:
        // Cv = (Cs - Cd) * FIX + Cd
        // Cs * FIX * 0.5
        // Cd * FIX * 0.5
        // glBlendEquation(GL_FUNC_ADD);
        // glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_CONSTANT_COLOR, GL_ONE, GL_ZERO);

        break;
      case DrawMode::AlphaBlend::ZERO_SRC_SRC_DST:
        // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        // glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
      default:
        ASSERT(false);
    }
  }

  m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_FALSE;
  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  // setup blending and color mult
  float color_mult = 1.f;
  if (draw_mode.get_ab_enable()) {
    //glBlendColor(1, 1, 1, 1);
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

    if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_DST_SRC_DST) {
      // (Cs - Cd) * As + Cd
      // Cs * As  + (1 - As) * Cd
      // s, d
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_SRC_DST) {
      // (Cs - 0) * As + Cd
      // Cs * As + (1) * Cd
      // s, d
      ASSERT(fix == 0);

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::ZERO_SRC_SRC_DST) {
      // (0 - Cs) * As + Cd
      // Cd - Cs * As
      // s, d

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_DST_FIX_DST) {
      // (Cs - Cd) * fix + Cd
      // Cs * fix + (1 - fx) * Cd

      m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[3] = fix / 127.0f;

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_SRC_SRC_SRC) {
      // this is very weird...
      // Cs
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_DST_DST) {
      // (Cs - 0) * Ad + Cd
      //glBlendFunc(GL_DST_ALPHA, GL_ONE);
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
      color_mult = 0.5f;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;

    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_FIX_DST) {
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else {
      ASSERT(false);
    }
  }

  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;

  // setup ztest
  if (draw_mode.get_zt_enable()) {
    m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
    switch (draw_mode.get_depth_test()) {
      case GsTest::ZTest::NEVER:
        m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_NEVER;
        break;
      case GsTest::ZTest::ALWAYS:
        m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        break;
      case GsTest::ZTest::GEQUAL:
        m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
        break;
      case GsTest::ZTest::GREATER:
        m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;
        break;
      default:
        ASSERT(false);
    }
  } else {
    // you aren't supposed to turn off z test enable, the GS had some bugs
    ASSERT(false);
  }

  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = draw_mode.get_depth_write_enable() ? VK_TRUE : VK_FALSE;

  m_fragment_uniform_buffer->SetUniform1f("alpha_reject", alpha_reject);
  m_fragment_uniform_buffer->SetUniform1f("color_mult", color_mult);
  m_fragment_uniform_buffer->SetUniform4f(
              "fog_color", render_state->fog_color[0] / 255.f,
              render_state->fog_color[1] / 255.f, render_state->fog_color[2] / 255.f,
              render_state->fog_intensity / 255);
}

void Generic2::setup_vulkan_tex(u16 unit,
                                u16 tbp,
                                bool filter,
                                bool clamp_s,
                                bool clamp_t,
                                SharedRenderState* render_state) {
  // look up the texture
  TextureInfo* texture = NULL;
  u32 tbp_to_lookup = tbp & 0x7fff;
  bool use_mt4hh = tbp & 0x8000;

  if (use_mt4hh) {
    texture = render_state->texture_pool->lookup_mt4hh(tbp_to_lookup);
  } else {
    texture = render_state->texture_pool->lookup(tbp_to_lookup);
  }

  if (!texture) {
    // TODO Add back
    if (tbp_to_lookup >= 8160 && tbp_to_lookup <= 8600) {
      fmt::print("Failed to find texture at {}, using random (eye zone)\n", tbp_to_lookup);

      texture = render_state->texture_pool->get_placeholder_texture();
    } else {
      fmt::print("Failed to find texture at {}, using random\n", tbp_to_lookup);
      texture = render_state->texture_pool->get_placeholder_texture();
    }
  }

  m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
  m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
  m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  m_pipeline_config_info.rasterizationInfo.depthBiasEnable = VK_FALSE;
  if (clamp_s || clamp_t) {
    m_pipeline_config_info.rasterizationInfo.depthClampEnable = VK_TRUE;
  } else {
    m_pipeline_config_info.rasterizationInfo.depthClampEnable = VK_FALSE;
  }

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

  if (clamp_s) {
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  if (clamp_t) {
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }

  if (filter) {
    //if (mipmap) { //TODO: Add option for mipmapping
    //  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    //}
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
  } else {
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
  }
}

void Generic2::do_draws_for_alpha(SharedRenderState* render_state,
                                  ScopedProfilerNode& prof,
                                  DrawMode::AlphaBlend alpha,
                                  bool hud) {
  for (u32 i = 0; i < m_next_free_bucket; i++) {
    auto& bucket = m_buckets[i];
    auto& first = m_adgifs[bucket.start];
    if (first.mode.get_alpha_blend() == alpha && first.uses_hud == hud) {
      setup_vulkan_for_draw_mode(first.mode, first.fix, render_state);
      setup_vulkan_tex(0, first.tbp, first.mode.get_filt_enable(), first.mode.get_clamp_s_enable(),
                       first.mode.get_clamp_t_enable(), render_state);
      //glDrawElements(GL_TRIANGLE_STRIP, bucket.idx_count, GL_UNSIGNED_INT,
      //               (void*)(sizeof(u32) * bucket.idx_idx));
      prof.add_draw_call();
      prof.add_tri(bucket.tri_count);
    }
  }
}

void Generic2::do_hud_draws(SharedRenderState* render_state, ScopedProfilerNode& prof) {
  for (u32 i = 0; i < m_next_free_bucket; i++) {
    auto& bucket = m_buckets[i];
    auto& first = m_adgifs[bucket.start];
    if (first.uses_hud) {
      setup_vulkan_for_draw_mode(first.mode, first.fix, render_state);
      setup_vulkan_tex(0, first.tbp, first.mode.get_filt_enable(), first.mode.get_clamp_s_enable(),
                       first.mode.get_clamp_t_enable(), render_state);
      //glDrawElements(GL_TRIANGLE_STRIP, bucket.idx_count, GL_UNSIGNED_INT,
      //               (void*)(sizeof(u32) * bucket.idx_idx));
      prof.add_draw_call();
      prof.add_tri(bucket.tri_count);
    }
  }
}

void Generic2::do_draws(SharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_ogl.vertex_buffer->map(m_next_free_vert * sizeof(Vertex), 0);
  m_ogl.vertex_buffer->writeToBuffer(m_verts.data());
  m_ogl.vertex_buffer->unmap();

  m_ogl.index_buffer->map(m_next_free_idx * sizeof(u32), 0);
  m_ogl.index_buffer->writeToBuffer(m_indices.data());
  m_ogl.index_buffer->unmap();

  //glEnable(GL_PRIMITIVE_RESTART);
  //glPrimitiveRestartIndex(UINT32_MAX);

  vulkan_bind_and_setup_proj(render_state);
  constexpr DrawMode::AlphaBlend alpha_order[ALPHA_MODE_COUNT] = {
      DrawMode::AlphaBlend::SRC_0_FIX_DST,    DrawMode::AlphaBlend::SRC_SRC_SRC_SRC,
      DrawMode::AlphaBlend::SRC_DST_SRC_DST,  DrawMode::AlphaBlend::SRC_0_SRC_DST,
      DrawMode::AlphaBlend::ZERO_SRC_SRC_DST, DrawMode::AlphaBlend::SRC_DST_FIX_DST,
      DrawMode::AlphaBlend::SRC_0_DST_DST,
  };

  for (int i = 0; i < ALPHA_MODE_COUNT; i++) {
    if (m_alpha_draw_enable[i]) {
      do_draws_for_alpha(render_state, prof, alpha_order[i], false);
    }
  }

  if (m_drawing_config.uses_hud) {
    m_vertex_uniform_buffer->SetUniform4f(
        "scale", m_drawing_config.hud_scale[0], m_drawing_config.hud_scale[1],
                m_drawing_config.hud_scale[2], 0);
    m_vertex_uniform_buffer->SetUniform1f("mat_23", m_drawing_config.hud_mat_23);
    m_vertex_uniform_buffer->SetUniform1f("mat_32", m_drawing_config.hud_mat_32);
    m_vertex_uniform_buffer->SetUniform1f("mat_33", m_drawing_config.hud_mat_33);

    do_hud_draws(render_state, prof);
  }
}

void Generic2::InitializeVertexBuffer(SharedRenderState* render_state) {
  auto& shader = render_state->shaders[ShaderId::SHRUB];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Vertex Fragment";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Shrub Fragment";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; //Is there a way to normalize floats
  attributeDescriptions[0].offset = offsetof(Vertex, xyz);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[1].offset = offsetof(Vertex, rgba);

  // FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(Vertex, st);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8_UINT;
  attributeDescriptions[3].offset = offsetof(Vertex, tex_unit);
  m_pipeline_config_info.attributeDescriptions.insert(
    m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(), attributeDescriptions.end());
}

GenericCommonVertexUniformBuffer::GenericCommonVertexUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VkDeviceSize instanceSize,
    uint32_t instanceCount,
    uint32_t memoryPropertyFlags,
    VkDeviceSize minOffsetAlignment)
    : UniformBuffer(device, instanceSize, instanceCount, memoryPropertyFlags, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"mat_32", offsetof(GenericCommonVertexUniformShaderData, mat_32)},
      {"fog_constants", offsetof(GenericCommonVertexUniformShaderData, fog_constants)},
      {"scale", offsetof(GenericCommonVertexUniformShaderData, scale)},
      {"mat_23", offsetof(GenericCommonVertexUniformShaderData, mat_23)},
      {"mat_33", offsetof(GenericCommonVertexUniformShaderData, mat_33)},
      {"hvdf_offset", offsetof(GenericCommonVertexUniformShaderData, hvdf_offset)}};
}

GenericCommonFragmentUniformBuffer::GenericCommonFragmentUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VkDeviceSize instanceSize,
    uint32_t instanceCount,
    uint32_t memoryPropertyFlags,
    VkDeviceSize minOffsetAlignment)
    : UniformBuffer(device, instanceSize, instanceCount, memoryPropertyFlags, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"alpha_reject", offsetof(GenericCommonFragmentUniformShaderData, alpha_reject)},
      {"color_mult", offsetof(GenericCommonFragmentUniformShaderData, color_mult)},
      {"fog_color", offsetof(GenericCommonFragmentUniformShaderData, fog_color)}};
}

