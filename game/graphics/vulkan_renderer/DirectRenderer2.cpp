#include "DirectRenderer2.h"

#include <immintrin.h>

#include "common/log/log.h"

#include "third-party/imgui/imgui.h"

DirectVulkanRenderer2::DirectVulkanRenderer2(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                           VulkanInitializationInfo& vulkan_info,
                                 u32 max_verts,
                                 u32 max_inds,
                                 u32 max_draws,
                                 const std::string& name,
                                 bool use_ftoi_mod)
    : BaseDirectRenderer2(max_verts, max_inds, max_draws, name, use_ftoi_mod),
      m_pipeline_layout(device),
      m_vulkan_info(vulkan_info) {
  // allocate buffers
  m_vertices.vertices.resize(max_verts);
  m_vertices.indices.resize(max_inds);
  m_draw_buffer.resize(max_draws);

  m_ogl.index_buffer = std::make_unique<IndexBuffer>(
      device, sizeof(u32), max_inds, 1);

  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
    device, sizeof(Vertex), max_verts, 1);

  InitializeShaderModule();
}

void DirectVulkanRenderer2::InitializeInputVertexAttribute() {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(DirectVulkanRenderer2::Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(DirectVulkanRenderer2::Vertex, xyz);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[1].offset = offsetof(DirectVulkanRenderer2::Vertex, rgba);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(DirectVulkanRenderer2::Vertex, stq);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8G8B8A8_UINT;
  attributeDescriptions[3].offset = offsetof(DirectVulkanRenderer2::Vertex, tex_unit);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void DirectVulkanRenderer2::InitializeShaderModule() {
  auto& shader = m_vulkan_info.shaders[ShaderId::DIRECT2];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Collision Fragment";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Collision Fragment";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

DirectVulkanRenderer2::~DirectVulkanRenderer2() {
}

void DirectVulkanRenderer2::reset_buffers() {
  m_next_free_draw = 0;
  m_vertices.next_index = 0;
  m_vertices.next_vertex = 0;
  m_state.next_vertex_starts_strip = true;
  m_current_state_has_open_draw = false;
}

void DirectVulkanRenderer2::reset_state() {
  m_state = {};
  m_stats = {};
  if (m_next_free_draw || m_vertices.next_vertex || m_vertices.next_index) {
    fmt::print("[{}] Call to reset_state while there was pending draw data!\n", m_name);
  }
  reset_buffers();
}

void DirectVulkanRenderer2::flush_pending(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof, UniformVulkanBuffer& uniform_buffer) {
  // skip, if we're empty.
  if (m_next_free_draw == 0) {
    reset_buffers();
    return;
  }

  // first, upload:
  Timer upload_timer;

  m_ogl.vertex_buffer->map();
  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices.vertices.data());
  m_ogl.vertex_buffer->unmap();

  m_ogl.index_buffer->map();
  m_ogl.index_buffer->writeToGpuBuffer(m_vertices.indices.data());
  m_ogl.index_buffer->unmap();

  m_stats.upload_wait += upload_timer.getSeconds();
  m_stats.num_uploads++;
  m_stats.upload_bytes +=
      (m_vertices.next_vertex * sizeof(Vertex)) + (m_vertices.next_index * sizeof(u32));

  // draw call loop
  // draw_call_loop_simple(render_state, prof);
  draw_call_loop_grouped(render_state, prof, uniform_buffer);

  reset_buffers();
}

void DirectVulkanRenderer2::draw_call_loop_simple(SharedVulkanRenderState* render_state,
                                            ScopedProfilerNode& prof,
                                            UniformVulkanBuffer& uniform_buffer) {
  fmt::print("------------------------\n");
  for (u32 draw_idx = 0; draw_idx < m_next_free_draw; draw_idx++) {
    const auto& draw = m_draw_buffer[draw_idx];
    fmt::print("{}", draw.to_single_line_string());
    setup_vulkan_for_draw_mode(draw, render_state, uniform_buffer);
    setup_vulkan_tex(0, draw.tbp, draw.mode.get_filt_enable(), draw.mode.get_clamp_s_enable(),
                     draw.mode.get_clamp_t_enable(), render_state);
    void* offset = (void*)(draw.start_index * sizeof(u32));
    int end_idx;
    if (draw_idx == m_next_free_draw - 1) {
      end_idx = m_vertices.next_index;
    } else {
      end_idx = m_draw_buffer[draw_idx + 1].start_index;
    }
    glDrawElements(GL_TRIANGLE_STRIP, end_idx - draw.start_index, GL_UNSIGNED_INT, (void*)offset);
    prof.add_draw_call();
    prof.add_tri((end_idx - draw.start_index) - 2);
  }
}

void DirectVulkanRenderer2::draw_call_loop_grouped(SharedVulkanRenderState* render_state,
                                             ScopedProfilerNode& prof,
                                             UniformVulkanBuffer& uniform_buffer) {

  u32 draw_idx = 0;
  while (draw_idx < m_next_free_draw) {
    const auto& draw = m_draw_buffer[draw_idx];
    u32 end_of_draw_group = draw_idx;  // this is inclusive
    setup_vulkan_for_draw_mode(draw, render_state, uniform_buffer);
    setup_vulkan_tex(draw.tex_unit, draw.tbp, draw.mode.get_filt_enable(),
                     draw.mode.get_clamp_s_enable(), draw.mode.get_clamp_t_enable(), render_state);

    for (u32 draw_to_consider = draw_idx + 1; draw_to_consider < draw_idx + TEX_UNITS;
         draw_to_consider++) {
      if (draw_to_consider >= m_next_free_draw) {
        break;
      }
      const auto& next_draw = m_draw_buffer[draw_to_consider];
      if (next_draw.mode.as_int() != draw.mode.as_int()) {
        break;
      }
      if (next_draw.fix != draw.fix) {
        break;
      }
      m_stats.saved_draws++;
      end_of_draw_group++;
      setup_vulkan_tex(next_draw.tex_unit, next_draw.tbp, next_draw.mode.get_filt_enable(),
                       next_draw.mode.get_clamp_s_enable(), next_draw.mode.get_clamp_t_enable(),
                       render_state);
    }

    u32 end_idx;
    if (end_of_draw_group == m_next_free_draw - 1) {
      end_idx = m_vertices.next_index;
    } else {
      end_idx = m_draw_buffer[end_of_draw_group + 1].start_index;
    }
    void* offset = (void*)(draw.start_index * sizeof(u32));
    // fmt::print("drawing {:4d} with abe {} tex {} {}", end_idx - draw.start_index,
    // (int)draw.mode.get_ab_enable(), end_of_draw_group - draw_idx, draw.to_single_line_string() );
    // fmt::print("{}\n", draw.mode.to_string());
    glDrawElements(GL_TRIANGLE_STRIP, end_idx - draw.start_index, GL_UNSIGNED_INT, (void*)offset);
    prof.add_draw_call();
    prof.add_tri((end_idx - draw.start_index) / 3);
    draw_idx = end_of_draw_group + 1;
  }
}

void DirectVulkanRenderer2::setup_vulkan_for_draw_mode(const Draw& draw,
                                                 SharedVulkanRenderState* render_state,
                                                 UniformBuffer& uniform_buffer) {
  // compute alpha_reject:
  float alpha_reject = 0.f;
  if (draw.mode.get_at_enable()) {
    switch (draw.mode.get_alpha_test()) {
      case DrawMode::AlphaTest::ALWAYS:
        break;
      case DrawMode::AlphaTest::GEQUAL:
        alpha_reject = draw.mode.get_aref() / 128.f;
        break;
      case DrawMode::AlphaTest::NEVER:
        break;
      default:
        ASSERT_MSG(false, fmt::format("unknown alpha test: {}", (int)draw.mode.get_alpha_test()));
    }
  }

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
  
  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_FALSE;
  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  // setup blending and color mult
  float color_mult = 1.f;
  if (draw.mode.get_ab_enable()) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
    m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
    m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

    if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_DST_SRC_DST) {
      // (Cs - Cd) * As + Cd
      // Cs * As  + (1 - As) * Cd
      // s, d
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
      
      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      
      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_SRC_DST) {
      // (Cs - 0) * As + Cd
      // Cs * As + (1) * Cd
      // s, d
      ASSERT(draw.fix == 0);

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
      
      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      
      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::ZERO_SRC_SRC_DST) {
      // (0 - Cs) * As + Cd
      // Cd - Cs * As
      // s, d

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
      
      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      
      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_DST_FIX_DST) {
      // (Cs - Cd) * fix + Cd
      // Cs * fix + (1 - fx) * Cd

      m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[3] = draw.fix / 127.0f;
      
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
      
      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
      
      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_SRC_SRC_SRC) {
      // this is very weird...
      // Cs

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
      
      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      
      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_DST_DST) {
      // (Cs - 0) * Ad + Cd

      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
      
      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      
      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      color_mult = 0.5;
    } else {
      ASSERT(false);
    }
  }

  m_pipeline_config_info.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
  if (draw.mode.get_zt_enable()) {
    m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
    switch (draw.mode.get_depth_test()) {
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

  if (draw.mode.get_depth_write_enable()) {
    glDepthMask(GL_TRUE);
  } else {
    glDepthMask(GL_FALSE);
  }

  if (draw.tbp == UINT16_MAX) {
    // not using a texture
    ASSERT(false);
  } else {
    // yes using a texture
    uniform_buffer.SetUniform1f("alpha_reject", alpha_reject);
    uniform_buffer.SetUniform1f("color_mult", color_mult);
    uniform_buffer.SetUniform4f("fog_color", render_state->fog_color[0] / 255.f,
                render_state->fog_color[1] / 255.f, render_state->fog_color[2] / 255.f,
                render_state->fog_intensity / 255);
  }
}

void DirectVulkanRenderer2::setup_vulkan_tex(u16 unit,
                                       u16 tbp,
                                       bool filter,
                                       bool clamp_s,
                                       bool clamp_t,
                                       SharedVulkanRenderState* render_state) {
  // look up the texture
  VulkanTexture* tex;
  u32 tbp_to_lookup = tbp & 0x7fff;
  bool use_mt4hh = tbp & 0x8000;

  if (use_mt4hh) {
    tex = m_vulkan_info.texture_pool->lookup_mt4hh_vulkan_texture(tbp_to_lookup);
  } else {
    tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp_to_lookup);
  }

  if (!tex) {
    // TODO Add back
    if (tbp_to_lookup >= 8160 && tbp_to_lookup <= 8600) {
      fmt::print("Failed to find texture at {}, using random (eye zone)\n", tbp_to_lookup);

      tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
    } else {
      fmt::print("Failed to find texture at {}, using random\n", tbp_to_lookup);
      tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
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
  if (clamp_s) {
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  if (clamp_t) {
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }

  if (filter) {
    if (!m_debug.disable_mip) {
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
  } else {
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
  }
}

