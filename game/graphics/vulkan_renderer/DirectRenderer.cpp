#include "DirectRenderer.h"

#include "common/dma/gs.h"
#include "common/log/log.h"
#include "common/util/Assert.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

DirectRenderer::DirectRenderer(const std::string& name,
                               BucketId my_id,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               VulkanInitializationInfo& vulkan_info,
                               int batch_size)
    : BucketRenderer(name, my_id, device, vulkan_info), m_prim_buffer(batch_size) {
  m_ogl.vertex_buffer_max_verts = batch_size * 3 * 2;
  m_ogl.vertex_buffer_bytes = m_ogl.vertex_buffer_max_verts * sizeof(DirectRenderer::Vertex);

  m_direct_basic_fragment_uniform_buffer = std::make_unique<DirectBasicTexturedFragmentUniformBuffer>(
    m_device,
    sizeof(DirectBasicTexturedFragmentUniformShaderData), 1,
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

  InitializeInputVertexAttribute();
}

void DirectRenderer::SetShaderModule(Shader& shader) {
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Direct Renderer Vertex";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Direct Renderer Fragment";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

void DirectRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(DirectRenderer::Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(DirectRenderer::Vertex, xyzf);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[1].offset = offsetof(DirectRenderer::Vertex, rgba);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(DirectRenderer::Vertex, stq);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8G8B8A8_UINT;
  attributeDescriptions[3].offset = offsetof(DirectRenderer::Vertex, tex_unit);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

DirectRenderer::~DirectRenderer() {
   //TODO: Delete allocated vulkan objects here
  // DeleteVertexBuffer();
  // DeleteIndexBuffer();
}

/*!
 * Render from a DMA bucket.
 */
void DirectRenderer::render(DmaFollower& dma,
                            SharedRenderState* render_state,
                            ScopedProfilerNode& prof) {

  //Create Buffer here if one isn't available
  //glBufferData(GL_ARRAY_BUFFER, m_ogl.vertex_buffer_bytes, nullptr, GL_STREAM_DRAW);
  
  // if we're rendering from a bucket, we should start off we a totally reset state:
  reset_state();
  setup_common_state(render_state);

  // just dump the DMA data into the other the render function
  while (dma.current_tag_offset() != render_state->next_bucket) {
    auto data = dma.read_and_advance();
    if (data.size_bytes && m_enabled) {
      render_vif(data.vif0(), data.vif1(), data.data, data.size_bytes, render_state, prof);
    }

    if (dma.current_tag_offset() == render_state->default_regs_buffer) {
      //      reset_state();
      dma.read_and_advance();  // cnt
      ASSERT(dma.current_tag().kind == DmaTag::Kind::RET);
      dma.read_and_advance();  // ret
    }
  }

  if (m_enabled) {
    flush_pending(render_state, prof);
  }
}

void DirectRenderer::reset_state() {
  m_test_state_needs_gl_update = true;
  m_test_state = TestState();

  m_blend_state_needs_gl_update = true;
  m_blend_state = BlendState();

  m_prim_gl_state_needs_gl_update = true;
  m_prim_gl_state = PrimGlState();

  for (int i = 0; i < TEXTURE_STATE_COUNT; ++i) {
    m_buffered_tex_state[i] = TextureState();
  }
  m_tex_state_from_reg = {};
  m_next_free_tex_state = 0;
  m_current_tex_state_idx = -1;

  m_prim_building = PrimBuildState();

  m_stats = {};
}

void DirectRenderer::draw_debug_window() {
  ImGui::Checkbox("Wireframe", &m_debug_state.wireframe);
  ImGui::SameLine();
  ImGui::Checkbox("No-texture", &m_debug_state.disable_texture);
  ImGui::SameLine();
  ImGui::Checkbox("red", &m_debug_state.red);
  ImGui::SameLine();
  ImGui::Checkbox("always", &m_debug_state.always_draw);
  ImGui::SameLine();
  ImGui::Checkbox("no mip", &m_debug_state.disable_mipmap);

  ImGui::Text("Triangles: %d", m_stats.triangles);
  ImGui::SameLine();
  ImGui::Text("Draws: %d", m_stats.draw_calls);

  ImGui::Text("Flush from state change:");
  ImGui::Text("  tex0: %d", m_stats.flush_from_tex_0);
  ImGui::Text("  tex1: %d", m_stats.flush_from_tex_1);
  ImGui::Text("  zbuf: %d", m_stats.flush_from_zbuf);
  ImGui::Text("  test: %d", m_stats.flush_from_test);
  ImGui::Text("  alph: %d", m_stats.flush_from_alpha);
  ImGui::Text("  clmp: %d", m_stats.flush_from_clamp);
  ImGui::Text("  prim: %d", m_stats.flush_from_prim);
  ImGui::Text("  texstate: %d", m_stats.flush_from_state_exhaust);
  ImGui::Text(" Total: %d/%d",
              m_stats.flush_from_prim + m_stats.flush_from_clamp + m_stats.flush_from_alpha +
                  m_stats.flush_from_test + m_stats.flush_from_zbuf + m_stats.flush_from_tex_1 +
                  m_stats.flush_from_tex_0 + m_stats.flush_from_state_exhaust,
              m_stats.draw_calls);
}

float u32_to_float(u32 in) {
  double x = (double)in / UINT32_MAX;
  return x;
}

float u32_to_sc(u32 in) {
  float flt = u32_to_float(in);
  return (flt - 0.5) * 16.0;
}

void DirectRenderer::flush_pending(SharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  if (m_blend_state_needs_gl_update) {
    update_vulkan_blend();
    m_blend_state_needs_gl_update = false;
  }

  if (m_prim_gl_state_needs_gl_update) {
    update_vulkan_prim(render_state);
    m_prim_gl_state_needs_gl_update = false;
  }

  if (m_test_state_needs_gl_update) {
    update_vulkan_test();
    m_test_state_needs_gl_update = false;
  }

  for (int i = 0; i < TEXTURE_STATE_COUNT; i++) {
    auto& tex_state = m_buffered_tex_state[i];
    if (tex_state.used) {
      update_vulkan_texture(render_state, i);
      tex_state.used = false;
    }
  }
  m_next_free_tex_state = 0;
  m_current_tex_state_idx = -1;

  // NOTE: sometimes we want to update the GL state without actually rendering anything, such as sky
  // textures, so we only return after we've updated the full state
  if (m_prim_buffer.vert_count == 0) {
    return;
  }

  if (m_debug_state.disable_texture) {
    // a bit of a hack, this forces the non-textured shader always.
    SetShaderModule(render_state->shaders[ShaderId::DIRECT_BASIC]);
    m_blend_state_needs_gl_update = true;
    m_prim_gl_state_needs_gl_update = true;
  }

  if (m_debug_state.red) {
    SetShaderModule(render_state->shaders[ShaderId::DEBUG_RED]);
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
    m_prim_gl_state_needs_gl_update = true;
    m_blend_state_needs_gl_update = true;
  }

  // hacks
  if (m_debug_state.always_draw) {
    m_pipeline_config_info.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
    m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  }

  // render!
  // update buffers:
  //CreateVertexBuffer(m_prim_buffer.vertices);

  int draw_count = 0;
  //glDrawArrays(GL_TRIANGLES, 0, m_prim_buffer.vert_count);
  draw_count++;
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

  if (m_debug_state.wireframe) {
    SetShaderModule(render_state->shaders[ShaderId::DEBUG_RED]);
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    //glDrawArrays(GL_TRIANGLES, 0, m_prim_buffer.vert_count);

    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    m_blend_state_needs_gl_update = true;
    m_prim_gl_state_needs_gl_update = true;
    draw_count++;
  }

  //glActiveTexture(GL_TEXTURE0);
  int n_tris = draw_count * (m_prim_buffer.vert_count / 3);
  prof.add_tri(n_tris);
  prof.add_draw_call(draw_count);
  m_stats.triangles += n_tris;
  m_stats.draw_calls += draw_count;
  m_prim_buffer.vert_count = 0;
}

void DirectRenderer::update_vulkan_prim(SharedRenderState* render_state) {
  // currently gouraud is handled in setup.
  const auto& state = m_prim_gl_state;
  if (state.texture_enable) {
    float alpha_reject = 0.0;
    if (m_test_state.alpha_test_enable) {
      switch (m_test_state.alpha_test) {
        case GsTest::AlphaTest::ALWAYS:
          break;
        case GsTest::AlphaTest::GEQUAL:
          alpha_reject = m_test_state.aref / 128.f;
          break;
        case GsTest::AlphaTest::NEVER:
          break;
        default:
          ASSERT_MSG(false, fmt::format("unknown alpha test: {}", (int)m_test_state.alpha_test));
      }
    }

    m_direct_basic_fragment_uniform_buffer->SetUniform1f("alpha_reject", alpha_reject);
    m_direct_basic_fragment_uniform_buffer->SetUniform1f("color_mult", m_ogl.color_mult);
    m_direct_basic_fragment_uniform_buffer->SetUniform1f("alpha_mult", m_ogl.alpha_mult);
    m_direct_basic_fragment_uniform_buffer->SetUniform4f("fog_color",
                render_state->fog_color[0] / 255.f, render_state->fog_color[1] / 255.f,
                render_state->fog_color[2] / 255.f, render_state->fog_intensity / 255);

  } else {
    SetShaderModule(render_state->shaders[ShaderId::DIRECT_BASIC]);
  }
  if (state.fogging_enable) {
    //    ASSERT(false);
  }
  if (state.aa_enable) {
    ASSERT(false);
  }
  if (state.use_uv) {
    ASSERT(false);
  }
  if (state.ctxt) {
    ASSERT(false);
  }
  if (state.fix) {
    ASSERT(false);
  }
}

void DirectRenderer::update_vulkan_texture(SharedRenderState* render_state, int unit) {
  TextureInfo* tex;
  auto& state = m_buffered_tex_state[unit];
  if (!state.used) {
    // nothing used this state, don't bother binding the texture.
    return;
  }
  if (state.using_mt4hh) {
    tex = render_state->texture_pool->lookup_mt4hh(state.texture_base_ptr);
  } else {
    tex = render_state->texture_pool->lookup(state.texture_base_ptr);
  }

  if (!tex) {
    // TODO Add back
    if (state.texture_base_ptr >= 8160 && state.texture_base_ptr <= 8600) {
      fmt::print("Failed to find texture at {}, using random (eye zone)\n", state.texture_base_ptr);

      tex = render_state->texture_pool->get_placeholder_texture();
    } else {
      fmt::print("Failed to find texture at {}, using random\n", state.texture_base_ptr);
      tex = render_state->texture_pool->get_placeholder_texture();
    }
  }
  ASSERT(tex);

  m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
  m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
  m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  m_pipeline_config_info.rasterizationInfo.depthBiasEnable = VK_FALSE;
  if (state.m_clamp_state.clamp_s || state.m_clamp_state.clamp_t) {
    m_pipeline_config_info.rasterizationInfo.depthClampEnable = VK_TRUE;
  } else {
    m_pipeline_config_info.rasterizationInfo.depthClampEnable = VK_FALSE;
  }

  // VkPhysicalDeviceProperties properties{};
  // vkGetPhysicalDeviceProperties(physicalDevice, &properties);

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
  if (state.m_clamp_state.clamp_s) {
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  if (state.m_clamp_state.clamp_t) {
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }

  if (state.enable_tex_filt) {
    if (!m_debug_state.disable_mipmap) {
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
  } else {
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
  }
}

void DirectRenderer::update_vulkan_blend() {
  const auto& state = m_blend_state;
  m_ogl.color_mult = 1.f;
  m_ogl.alpha_mult = 1.f;
  m_prim_gl_state_needs_gl_update = true;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;


  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  if (state.alpha_blend_enable) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;
    m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

    if (state.a == GsAlpha::BlendMode::SOURCE && state.b == GsAlpha::BlendMode::DEST &&
        state.c == GsAlpha::BlendMode::SOURCE && state.d == GsAlpha::BlendMode::DEST) {
      // (Cs - Cd) * As + Cd
      // Cs * As  + (1 - As) * Cd
      // s, d

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (state.a == GsAlpha::BlendMode::SOURCE &&
               state.b == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               state.c == GsAlpha::BlendMode::SOURCE && state.d == GsAlpha::BlendMode::DEST) {
      // (Cs - 0) * As + Cd
      // Cs * As + (1) * Cd
      // s, d
      ASSERT(state.fix == 0);

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (state.a == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               state.b == GsAlpha::BlendMode::SOURCE && state.c == GsAlpha::BlendMode::SOURCE &&
               state.d == GsAlpha::BlendMode::DEST) {
      // (0 - Cs) * As + Cd
      // Cd - Cs * As
      // s, d

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (state.a == GsAlpha::BlendMode::SOURCE && state.b == GsAlpha::BlendMode::DEST &&
               state.c == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               state.d == GsAlpha::BlendMode::DEST) {
      // (Cs - Cd) * fix + Cd
      // Cs * fix + (1 - fx) * Cd

      m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[3] = state.fix / 127.0f;

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (state.a == GsAlpha::BlendMode::SOURCE && state.b == GsAlpha::BlendMode::SOURCE &&
               state.c == GsAlpha::BlendMode::SOURCE && state.d == GsAlpha::BlendMode::SOURCE) {
      // trick to disable alpha blending.
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
    } else if (state.a == GsAlpha::BlendMode::SOURCE &&
               state.b == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               state.c == GsAlpha::BlendMode::DEST && state.d == GsAlpha::BlendMode::DEST) {
      // (Cs - 0) * Ad + Cd
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      m_ogl.color_mult = 0.5;
    } else {
      // unsupported blend: a 0 b 2 c 2 d 1
      lg::error("unsupported blend: a {} b {} c {} d {}", (int)state.a, (int)state.b, (int)state.c,
                (int)state.d);
      //      ASSERT(false);
    }
  }
}

void DirectRenderer::update_vulkan_test() {
  const auto& state = m_test_state;

  m_pipeline_config_info.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;

  if (state.zte) {
    m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
    switch (state.ztst) {
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
  }

  if (state.date) {
    ASSERT(false);
  }

  bool alpha_trick_to_disable = m_test_state.alpha_test_enable &&
                                m_test_state.alpha_test == GsTest::AlphaTest::NEVER &&
                                m_test_state.afail == GsTest::AlphaFail::FB_ONLY;
  if (state.depth_writes && !alpha_trick_to_disable) {
    m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_TRUE;
  } else {
    m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  }
}

void DirectRenderer::setup_common_state(SharedRenderState* /*render_state*/) {
  // todo texture clamp.
}

namespace {
/*!
 * If it's a direct, returns the qwc.
 * If it's ignorable (nop, flush), returns 0.
 * Otherwise, assert.
 */
u32 get_direct_qwc_or_nop(const VifCode& code) {
  switch (code.kind) {
    case VifCode::Kind::NOP:
    case VifCode::Kind::FLUSHA:
      return 0;
    case VifCode::Kind::DIRECT:
      if (code.immediate == 0) {
        return 65536;
      } else {
        return code.immediate;
      }
    default:
      ASSERT(false);
  }
}
}  // namespace

/*!
 * Render VIF data.
 */
void DirectRenderer::render_vif(u32 vif0,
                                u32 vif1,
                                const u8* data,
                                u32 size,
                                SharedRenderState* render_state,
                                ScopedProfilerNode& prof) {
  // here we process VIF data. Basically we just go forward, looking for DIRECTs.
  // We skip stuff like flush and nops.

  // read the vif cmds at the front.
  u32 gif_qwc = get_direct_qwc_or_nop(VifCode(vif0));
  if (gif_qwc) {
    // we got a direct. expect the second thing to be a nop/similar.
    ASSERT(get_direct_qwc_or_nop(VifCode(vif1)) == 0);
  } else {
    gif_qwc = get_direct_qwc_or_nop(VifCode(vif1));
  }

  u32 offset_into_data = 0;
  while (offset_into_data < size) {
    if (gif_qwc) {
      if (offset_into_data & 0xf) {
        // not aligned. should get nops.
        u32 vif;
        memcpy(&vif, data + offset_into_data, 4);
        offset_into_data += 4;
        ASSERT(get_direct_qwc_or_nop(VifCode(vif)) == 0);
      } else {
        // aligned! do a gif transfer!
        render_gif(data + offset_into_data, gif_qwc * 16, render_state, prof);
        offset_into_data += gif_qwc * 16;
      }
    } else {
      // we are reading VIF data.
      u32 vif;
      memcpy(&vif, data + offset_into_data, 4);
      offset_into_data += 4;
      gif_qwc = get_direct_qwc_or_nop(VifCode(vif));
    }
  }
}

/*!
 * Render GIF data.
 */
void DirectRenderer::render_gif(const u8* data,
                                u32 size,
                                SharedRenderState* render_state,
                                ScopedProfilerNode& prof) {
  if (size != UINT32_MAX) {
    ASSERT(size >= 16);
  }

  bool eop = false;

  u32 offset = 0;
  while (!eop) {
    if (size != UINT32_MAX) {
      ASSERT(offset < size);
    }
    GifTag tag(data + offset);
    offset += 16;

    // unpack registers.
    // faster to do it once outside of the nloop loop.
    GifTag::RegisterDescriptor reg_desc[16];
    u32 nreg = tag.nreg();
    for (u32 i = 0; i < nreg; i++) {
      reg_desc[i] = tag.reg(i);
    }

    auto format = tag.flg();
    if (format == GifTag::Format::PACKED) {
      if (tag.pre()) {
        handle_prim(tag.prim(), render_state, prof);
      }
      for (u32 loop = 0; loop < tag.nloop(); loop++) {
        for (u32 reg = 0; reg < nreg; reg++) {
          // fmt::print("{}\n", reg_descriptor_name(reg_desc[reg]));
          switch (reg_desc[reg]) {
            case GifTag::RegisterDescriptor::AD:
              handle_ad(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::ST:
              handle_st_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::RGBAQ:
              handle_rgbaq_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              handle_xyzf2_packed(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::PRIM:
              handle_prim_packed(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::TEX0_1:
              handle_tex0_1_packed(data + offset);
              break;
            default:
              fmt::print("Register {} is not supported in packed mode yet\n",
                         reg_descriptor_name(reg_desc[reg]));
              ASSERT(false);
          }
          offset += 16;  // PACKED = quadwords
        }
      }
    } else if (format == GifTag::Format::REGLIST) {
      for (u32 loop = 0; loop < tag.nloop(); loop++) {
        for (u32 reg = 0; reg < nreg; reg++) {
          u64 register_data;
          memcpy(&register_data, data + offset, 8);
          // fmt::print("loop: {} reg: {} {}\n", loop, reg, reg_descriptor_name(reg_desc[reg]));
          switch (reg_desc[reg]) {
            case GifTag::RegisterDescriptor::PRIM:
              handle_prim(register_data, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::RGBAQ:
              handle_rgbaq(register_data);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              handle_xyzf2(register_data, render_state, prof);
              break;
            default:
              fmt::print("Register {} is not supported in reglist mode yet\n",
                         reg_descriptor_name(reg_desc[reg]));
              ASSERT(false);
          }
          offset += 8;  // PACKED = quadwords
        }
      }
    } else {
      ASSERT(false);  // format not packed or reglist.
    }

    eop = tag.eop();
  }

  if (size != UINT32_MAX) {
    if ((offset + 15) / 16 != size / 16) {
      ASSERT_MSG(false, fmt::format("DirectRenderer size failed in {}. expected: {}, got: {}",
                                    name_and_id(), size, offset));
    }
  }

  //  fmt::print("{}\n", GifTag(data).print());
}

void DirectRenderer::handle_ad(const u8* data,
                               SharedRenderState* render_state,
                               ScopedProfilerNode& prof) {
  u64 value;
  GsRegisterAddress addr;
  memcpy(&value, data, sizeof(u64));
  memcpy(&addr, data + 8, sizeof(GsRegisterAddress));

  // fmt::print("{}\n", register_address_name(addr));
  switch (addr) {
    case GsRegisterAddress::ZBUF_1:
      handle_zbuf1(value, render_state, prof);
      break;
    case GsRegisterAddress::TEST_1:
      handle_test1(value, render_state, prof);
      break;
    case GsRegisterAddress::ALPHA_1:
      handle_alpha1(value, render_state, prof);
      break;
    case GsRegisterAddress::PABE:
      handle_pabe(value);
      break;
    case GsRegisterAddress::CLAMP_1:
      handle_clamp1(value);
      break;
    case GsRegisterAddress::PRIM:
      handle_prim(value, render_state, prof);
      break;

    case GsRegisterAddress::TEX1_1:
      handle_tex1_1(value);
      break;
    case GsRegisterAddress::TEXA:
      handle_texa(value);
      break;
    case GsRegisterAddress::TEXCLUT:
      // TODO
      // the only thing the direct renderer does with texture is font, which does no tricks with
      // CLUT. The texture upload process will do all of the lookups with the default CLUT.
      // So we'll just assume that the TEXCLUT is set properly and ignore this.
      break;
    case GsRegisterAddress::FOGCOL:
      // TODO
      break;
    case GsRegisterAddress::TEX0_1:
      handle_tex0_1(value);
      break;
    case GsRegisterAddress::MIPTBP1_1:
    case GsRegisterAddress::MIPTBP2_1:
      // TODO this has the address of different mip levels.
      break;
    case GsRegisterAddress::TEXFLUSH:
      break;
    case GsRegisterAddress::FRAME_1:
      break;
    case GsRegisterAddress::RGBAQ:
      // shadow scissor does this?
      {
        m_prim_building.rgba_reg[0] = data[0];
        m_prim_building.rgba_reg[1] = data[1];
        m_prim_building.rgba_reg[2] = data[2];
        m_prim_building.rgba_reg[3] = data[3];
        memcpy(&m_prim_building.Q, data + 4, 4);
      }
      break;
    default:
      ASSERT_MSG(false, fmt::format("Address {} is not supported", register_address_name(addr)));
  }
}

void DirectRenderer::handle_tex1_1(u64 val) {
  GsTex1 reg(val);
  // for now, we aren't going to handle mipmapping. I don't think it's used with direct.
  //   ASSERT(reg.mxl() == 0);
  // if that's true, we can ignore LCM, MTBA, L, K

  bool want_tex_filt = reg.mmag();
  if (want_tex_filt != m_tex_state_from_reg.enable_tex_filt) {
    m_tex_state_from_reg.enable_tex_filt = want_tex_filt;
    // we changed the state_from_reg, we no longer know if it points to a texture state.
    m_current_tex_state_idx = -1;
  }

  // MMAG/MMIN specify texture filtering. For now, assume always linear
  //  ASSERT(reg.mmag() == true);
  //  if (!(reg.mmin() == 1 || reg.mmin() == 4)) {  // with mipmap off, both of these are linear
  //                                                //    lg::error("unsupported mmin");
  //  }
}

void DirectRenderer::handle_tex0_1_packed(const u8* data) {
  u64 val;
  memcpy(&val, data, sizeof(u64));
  handle_tex0_1(val);
}

void DirectRenderer::handle_tex0_1(u64 val) {
  GsTex0 reg(val);
  // update tbp
  if (m_tex_state_from_reg.current_register != reg) {
    m_tex_state_from_reg.texture_base_ptr = reg.tbp0();
    m_tex_state_from_reg.using_mt4hh = reg.psm() == GsTex0::PSM::PSMT4HH;
    m_tex_state_from_reg.current_register = reg;
    m_tex_state_from_reg.tcc = reg.tcc();
    m_tex_state_from_reg.decal = reg.tfx() == GsTex0::TextureFunction::DECAL;
    ASSERT(reg.tfx() == GsTex0::TextureFunction::DECAL ||
           reg.tfx() == GsTex0::TextureFunction::MODULATE);

    // we changed the state_from_reg, we no longer know if it points to a texture state.
    m_current_tex_state_idx = -1;
  }

  // tbw: assume they got it right
  // psm: assume they got it right
  // tw: assume they got it right
  // th: assume they got it right

  // MERC hack
  // ASSERT(reg.tfx() == GsTex0::TextureFunction::MODULATE);

  // cbp: assume they got it right
  // cpsm: assume they got it right
  // csm: assume they got it right
}

void DirectRenderer::handle_texa(u64 val) {
  GsTexa reg(val);

  // rgba16 isn't used so this doesn't matter?
  // but they use sane defaults anyway
  ASSERT(reg.ta0() == 0);
  ASSERT(reg.ta1() == 0x80);  // note: check rgba16_to_rgba32 if this changes.

  ASSERT(reg.aem() == false);
}

void DirectRenderer::handle_st_packed(const u8* data) {
  memcpy(&m_prim_building.st_reg.x(), data + 0, 4);
  memcpy(&m_prim_building.st_reg.y(), data + 4, 4);
  memcpy(&m_prim_building.Q, data + 8, 4);
}

void DirectRenderer::handle_rgbaq_packed(const u8* data) {
  // TODO update Q from st.
  m_prim_building.rgba_reg[0] = data[0];
  m_prim_building.rgba_reg[1] = data[4];
  m_prim_building.rgba_reg[2] = data[8];
  m_prim_building.rgba_reg[3] = data[12];
}

void DirectRenderer::handle_xyzf2_packed(const u8* data,
                                         SharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  u32 x, y;
  memcpy(&x, data, 4);
  memcpy(&y, data + 4, 4);

  u64 upper;
  memcpy(&upper, data + 8, 8);
  u32 z = (upper >> 4) & 0xffffff;

  u8 f = (upper >> 36);
  bool adc = upper & (1ull << 47);
  handle_xyzf2_common(x << 16, y << 16, z << 8, f, render_state, prof, !adc);
}

void DirectRenderer::handle_zbuf1(u64 val,
                                  SharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  // note: we can basically ignore this. There's a single z buffer that's always configured the same
  // way - 24-bit, at offset 448.
  GsZbuf x(val);
  ASSERT(x.psm() == TextureFormat::PSMZ24);
  ASSERT(x.zbp() == 448);

  bool write = !x.zmsk();
  //  ASSERT(write);
  if (write != m_test_state.depth_writes) {
    m_stats.flush_from_zbuf++;
    flush_pending(render_state, prof);
    m_test_state_needs_gl_update = true;
    m_prim_gl_state_needs_gl_update = true;
    m_test_state.depth_writes = write;
  }
}

void DirectRenderer::handle_test1(u64 val,
                                  SharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  GsTest reg(val);
  if (reg.alpha_test_enable()) {
    // ASSERT(reg.alpha_test() == GsTest::AlphaTest::ALWAYS);
  }
  ASSERT(!reg.date());
  if (m_test_state.current_register != reg) {
    m_stats.flush_from_test++;
    flush_pending(render_state, prof);
    m_test_state.from_register(reg);
    m_test_state_needs_gl_update = true;
    m_prim_gl_state_needs_gl_update = true;
  }
}

void DirectRenderer::handle_alpha1(u64 val,
                                   SharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  GsAlpha reg(val);
  if (m_blend_state.current_register != reg) {
    m_stats.flush_from_alpha++;
    flush_pending(render_state, prof);
    m_blend_state.from_register(reg);
    m_blend_state_needs_gl_update = true;
  }
}

void DirectRenderer::handle_pabe(u64 val) {
  ASSERT(val == 0);  // not really sure how to handle this yet.
}

void DirectRenderer::handle_clamp1(u64 val) {
  if (!(val == 0b101 || val == 0 || val == 1 || val == 0b100)) {
    //    fmt::print("clamp: 0x{:x}\n", val);
    //    ASSERT(false);
  }

  if (m_tex_state_from_reg.m_clamp_state.current_register != val) {
    m_current_tex_state_idx = -1;
    m_tex_state_from_reg.m_clamp_state.current_register = val;
    m_tex_state_from_reg.m_clamp_state.clamp_s = val & 0b001;
    m_tex_state_from_reg.m_clamp_state.clamp_t = val & 0b100;
  }
}

void DirectRenderer::handle_prim_packed(const u8* data,
                                        SharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  u64 val;
  memcpy(&val, data, sizeof(u64));
  handle_prim(val, render_state, prof);
}

void DirectRenderer::handle_prim(u64 val,
                                 SharedRenderState* render_state,
                                 ScopedProfilerNode& prof) {
  if (m_prim_building.tri_strip_startup) {
    m_prim_building.tri_strip_startup = 0;
    m_prim_building.building_idx = 0;
  } else {
    if (m_prim_building.building_idx > 0) {
      ASSERT(false);  // shouldn't leave any half-finished prims
    }
  }
  // need to flush any in progress prims to the buffer.

  GsPrim prim(val);
  if (m_prim_gl_state.current_register != prim || m_blend_state.alpha_blend_enable != prim.abe()) {
    m_stats.flush_from_prim++;
    flush_pending(render_state, prof);
    m_prim_gl_state.from_register(prim);
    m_blend_state.alpha_blend_enable = prim.abe();
    m_prim_gl_state_needs_gl_update = true;
    m_blend_state_needs_gl_update = true;
  }

  m_prim_building.kind = prim.kind();
}

void DirectRenderer::handle_rgbaq(u64 val) {
  ASSERT((val >> 32) == 0);  // q = 0
  memcpy(m_prim_building.rgba_reg.data(), &val, 4);
}

int DirectRenderer::get_texture_unit_for_current_reg(SharedRenderState* render_state,
                                                     ScopedProfilerNode& prof) {
  if (m_current_tex_state_idx != -1) {
    return m_current_tex_state_idx;
  }

  if (m_next_free_tex_state >= TEXTURE_STATE_COUNT) {
    m_stats.flush_from_state_exhaust++;
    flush_pending(render_state, prof);
    return get_texture_unit_for_current_reg(render_state, prof);
  } else {
    ASSERT(!m_buffered_tex_state[m_next_free_tex_state].used);
    m_buffered_tex_state[m_next_free_tex_state] = m_tex_state_from_reg;
    m_buffered_tex_state[m_next_free_tex_state].used = true;
    m_current_tex_state_idx = m_next_free_tex_state++;
    return m_current_tex_state_idx;
  }
}

void DirectRenderer::handle_xyzf2_common(u32 x,
                                         u32 y,
                                         u32 z,
                                         u8 f,
                                         SharedRenderState* render_state,
                                         ScopedProfilerNode& prof,
                                         bool advance) {
  if (m_my_id == BucketId::MERC_TFRAG_TEX_LEVEL0) {
    // fmt::print("0x{:x}, 0x{:x}, 0x{:x}\n", x, y, z);
  }
  if (m_prim_buffer.is_full()) {
    lg::warn("Buffer wrapped in {} ({} verts, {} bytes)", m_name, m_ogl.vertex_buffer_max_verts,
             m_prim_buffer.vert_count * sizeof(Vertex));
    flush_pending(render_state, prof);
  }

  m_prim_building.building_stq.at(m_prim_building.building_idx) = math::Vector<float, 3>(
      m_prim_building.st_reg.x(), m_prim_building.st_reg.y(), m_prim_building.Q);
  m_prim_building.building_rgba.at(m_prim_building.building_idx) = m_prim_building.rgba_reg;
  m_prim_building.building_vert.at(m_prim_building.building_idx) = math::Vector<u32, 4>{x, y, z, f};

  m_prim_building.building_idx++;

  int tex_unit = get_texture_unit_for_current_reg(render_state, prof);
  bool tcc = m_buffered_tex_state[tex_unit].tcc;
  bool decal = m_buffered_tex_state[tex_unit].decal;
  bool fge = m_prim_gl_state.fogging_enable;

  switch (m_prim_building.kind) {
    case GsPrim::Kind::SPRITE: {
      if (m_prim_building.building_idx == 2) {
        // build triangles from the sprite.
        auto& corner1_vert = m_prim_building.building_vert[0];
        auto& corner1_rgba = m_prim_building.building_rgba[0];
        auto& corner2_vert = m_prim_building.building_vert[1];
        auto& corner2_rgba = m_prim_building.building_rgba[1];
        // should use most recent vertex z.
        math::Vector<u32, 4> corner3_vert{corner1_vert[0], corner2_vert[1], corner2_vert[2]};
        math::Vector<u32, 4> corner4_vert{corner2_vert[0], corner1_vert[1], corner2_vert[2]};

        if (m_prim_gl_state.gouraud_enable) {
          // I'm not really sure what the GS does here.
          ASSERT(false);
        }
        auto& corner3_rgba = corner2_rgba;
        auto& corner4_rgba = corner2_rgba;

        m_prim_buffer.push(corner1_rgba, corner1_vert, {}, 0, tcc, decal, fge);
        m_prim_buffer.push(corner3_rgba, corner3_vert, {}, 0, tcc, decal, fge);
        m_prim_buffer.push(corner2_rgba, corner2_vert, {}, 0, tcc, decal, fge);
        m_prim_buffer.push(corner2_rgba, corner2_vert, {}, 0, tcc, decal, fge);
        m_prim_buffer.push(corner4_rgba, corner4_vert, {}, 0, tcc, decal, fge);
        m_prim_buffer.push(corner1_rgba, corner1_vert, {}, 0, tcc, decal, fge);
        m_prim_building.building_idx = 0;
      }
    } break;
    case GsPrim::Kind::TRI_STRIP: {
      if (m_prim_building.building_idx == 3) {
        m_prim_building.building_idx = 0;
      }

      if (m_prim_building.tri_strip_startup < 3) {
        m_prim_building.tri_strip_startup++;
      }
      if (m_prim_building.tri_strip_startup >= 3) {
        if (advance) {
          for (int i = 0; i < 3; i++) {
            m_prim_buffer.push(m_prim_building.building_rgba[i], m_prim_building.building_vert[i],
                               m_prim_building.building_stq[i], tex_unit, tcc, decal, fge);
          }
        }
      }

    } break;

    case GsPrim::Kind::TRI:
      if (m_prim_building.building_idx == 3) {
        m_prim_building.building_idx = 0;
        for (int i = 0; i < 3; i++) {
          m_prim_buffer.push(m_prim_building.building_rgba[i], m_prim_building.building_vert[i],
                             m_prim_building.building_stq[i], tex_unit, tcc, decal, fge);
        }
      }
      break;

    case GsPrim::Kind::TRI_FAN: {
      if (m_prim_building.tri_strip_startup < 2) {
        m_prim_building.tri_strip_startup++;
      } else {
        if (m_prim_building.building_idx == 2) {
          // nothing.
        } else if (m_prim_building.building_idx == 3) {
          m_prim_building.building_idx = 1;
        }
        for (int i = 0; i < 3; i++) {
          m_prim_buffer.push(m_prim_building.building_rgba[i], m_prim_building.building_vert[i],
                             m_prim_building.building_stq[i], tex_unit, tcc, decal, fge);
        }
      }
    } break;

    case GsPrim::Kind::LINE: {
      if (m_prim_building.building_idx == 2) {
        math::Vector<double, 3> pt0 = m_prim_building.building_vert[0].xyz().cast<double>();
        math::Vector<double, 3> pt1 = m_prim_building.building_vert[1].xyz().cast<double>();
        auto normal = (pt1 - pt0).normalized().cross(math::Vector<double, 3>{0, 0, 1});

        double line_width = (1 << 19);
        //        debug_print_vtx(m_prim_building.building_vert[0]);
        //        debug_print_vtx(m_prim_building.building_vert[1]);

        math::Vector<double, 3> a = pt0 + normal * line_width;
        math::Vector<double, 3> b = pt1 + normal * line_width;
        math::Vector<double, 3> c = pt0 - normal * line_width;
        math::Vector<double, 3> d = pt1 - normal * line_width;
        math::Vector<u32, 4> ai{a.x(), a.y(), a.z(), 0};
        math::Vector<u32, 4> bi{b.x(), b.y(), b.z(), 0};
        math::Vector<u32, 4> ci{c.x(), c.y(), c.z(), 0};
        math::Vector<u32, 4> di{d.x(), d.y(), d.z(), 0};

        // ACB:
        m_prim_buffer.push(m_prim_building.building_rgba[0], ai, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[0], ci, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[1], bi, {}, 0, false, false, false);
        // b c d
        m_prim_buffer.push(m_prim_building.building_rgba[1], bi, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[0], ci, {}, 0, false, false, false);
        m_prim_buffer.push(m_prim_building.building_rgba[1], di, {}, 0, false, false, false);
        //

        m_prim_building.building_idx = 0;
      }
    } break;
    default:
      ASSERT_MSG(false, fmt::format("prim type {} is unsupported in {}.", (int)m_prim_building.kind,
                                    name_and_id()));
  }
}

void DirectRenderer::handle_xyzf2(u64 val,
                                  SharedRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  u32 x = val & 0xffff;
  u32 y = (val >> 16) & 0xffff;
  u32 z = (val >> 32) & 0xffffff;
  u32 f = (val >> 56) & 0xff;

  handle_xyzf2_common(x << 16, y << 16, z << 8, f, render_state, prof, true);
}

void DirectRenderer::TestState::from_register(GsTest reg) {
  current_register = reg;
  alpha_test_enable = reg.alpha_test_enable();
  if (alpha_test_enable) {
    alpha_test = reg.alpha_test();
    aref = reg.aref();
    afail = reg.afail();
  }

  date = reg.date();
  if (date) {
    datm = reg.datm();
  }

  zte = reg.zte();
  ztst = reg.ztest();
}

void DirectRenderer::BlendState::from_register(GsAlpha reg) {
  current_register = reg;
  a = reg.a_mode();
  b = reg.b_mode();
  c = reg.c_mode();
  d = reg.d_mode();
  fix = reg.fix();
}

void DirectRenderer::PrimGlState::from_register(GsPrim reg) {
  current_register = reg;
  gouraud_enable = reg.gouraud();
  texture_enable = reg.tme();
  fogging_enable = reg.fge();
  aa_enable = reg.aa1();
  use_uv = reg.fst();
  ctxt = reg.ctxt();
  fix = reg.fix();
}

DirectRenderer::PrimitiveBuffer::PrimitiveBuffer(int max_triangles) {
  vertices.resize(max_triangles * 3);
  max_verts = max_triangles * 3;
}

void DirectRenderer::PrimitiveBuffer::push(const math::Vector<u8, 4>& rgba,
                                           const math::Vector<u32, 4>& vert,
                                           const math::Vector<float, 3>& st,
                                           int unit,
                                           bool tcc,
                                           bool decal,
                                           bool fog_enable) {
  auto& v = vertices[vert_count];
  v.rgba = rgba;
  v.xyzf[0] = (float)vert[0] / (float)UINT32_MAX;
  v.xyzf[1] = (float)vert[1] / (float)UINT32_MAX;
  v.xyzf[2] = (float)vert[2] / (float)UINT32_MAX;
  v.xyzf[3] = (float)vert[3];
  v.stq = st;
  v.tex_unit = unit;
  v.tcc = tcc;
  v.decal = decal;
  v.fog_enable = fog_enable;
  vert_count++;
}