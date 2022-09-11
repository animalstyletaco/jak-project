#include "DirectRenderer2.h"

#include <immintrin.h>

#include "common/log/log.h"

#include "third-party/imgui/imgui.h"

DirectRenderer2::DirectRenderer2(u32 max_verts,
                                 u32 max_inds,
                                 u32 max_draws,
                                 const std::string& name,
                                 bool use_ftoi_mod)
    : m_name(name), m_use_ftoi_mod(use_ftoi_mod) {
  // allocate buffers
  m_vertices.vertices.resize(max_verts);
  m_vertices.indices.resize(max_inds);
  m_draw_buffer.resize(max_draws);

  // set up the vertex array
  //CreateVertexBuffer(m_vertices.vertices);
  //CreateIndexBuffer(m_vertices.indices);
}

void DirectRenderer2::InitializeInputVertexAttribute() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_TRUE;

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(DirectRenderer2::Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(DirectRenderer2::Vertex, xyz);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[1].offset = offsetof(DirectRenderer2::Vertex, rgba);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(DirectRenderer2::Vertex, stq);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8G8B8A8_UINT;
  attributeDescriptions[3].offset = offsetof(DirectRenderer2::Vertex, tex_unit);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}

void DirectRenderer2::SetShaderModule(Shader& shader) {
}

DirectRenderer2::~DirectRenderer2() {
   //DeleteVertexBuffer();
  // DeleteIndexBuffer();
}

void DirectRenderer2::init_shaders(ShaderLibrary& shaders) {
  SetShaderModule(shaders[ShaderId::DIRECT2]);
}

void DirectRenderer2::reset_buffers() {
  m_next_free_draw = 0;
  m_vertices.next_index = 0;
  m_vertices.next_vertex = 0;
  m_state.next_vertex_starts_strip = true;
  m_current_state_has_open_draw = false;
}

void DirectRenderer2::reset_state() {
  m_state = {};
  m_stats = {};
  if (m_next_free_draw || m_vertices.next_vertex || m_vertices.next_index) {
    fmt::print("[{}] Call to reset_state while there was pending draw data!\n", m_name);
  }
  reset_buffers();
}

std::string DirectRenderer2::Vertex::print() const {
  return fmt::format("{} {} {}\n", xyz.to_string_aligned(), stq.to_string_aligned(), rgba[0]);
}

std::string DirectRenderer2::Draw::to_string() const {
  std::string result;
  result += mode.to_string();
  result += fmt::format("TBP: 0x{:x}\n", tbp);
  result += fmt::format("fix: 0x{:x}\n", fix);
  return result;
}

std::string DirectRenderer2::Draw::to_single_line_string() const {
  return fmt::format("mode 0x{:8x} tbp 0x{:4x} fix 0x{:2x}\n", mode.as_int(), tbp, fix);
}

void DirectRenderer2::flush_pending(SharedRenderState* render_state, ScopedProfilerNode& prof, UniformBuffer& uniform_buffer) {
  // skip, if we're empty.
  if (m_next_free_draw == 0) {
    reset_buffers();
    return;
  }

  // first, upload:
  Timer upload_timer;

  //SetVertexBuffer(m_vertices.vertices);
  //SetIndexBuffer(m_vertices.indices);

  m_stats.upload_wait += upload_timer.getSeconds();
  m_stats.num_uploads++;
  m_stats.upload_bytes +=
      (m_vertices.next_vertex * sizeof(Vertex)) + (m_vertices.next_index * sizeof(u32));

  // draw call loop
  // draw_call_loop_simple(render_state, prof);
  draw_call_loop_grouped(render_state, prof, uniform_buffer);

  reset_buffers();
}

void DirectRenderer2::draw_call_loop_simple(SharedRenderState* render_state,
                                            ScopedProfilerNode& prof,
                                            UniformBuffer& uniform_buffer) {
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

void DirectRenderer2::draw_call_loop_grouped(SharedRenderState* render_state,
                                             ScopedProfilerNode& prof,
                                             UniformBuffer& uniform_buffer) {

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

void DirectRenderer2::setup_vulkan_for_draw_mode(const Draw& draw,
                                                 SharedRenderState* render_state,
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

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // setup blending and color mult
  float color_mult = 1.f;
  if (draw.mode.get_ab_enable()) {
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlending.blendConstants[0] = 1.0f;
    colorBlending.blendConstants[1] = 1.0f;
    colorBlending.blendConstants[2] = 1.0f;
    colorBlending.blendConstants[3] = 1.0f;

    if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_DST_SRC_DST) {
      // (Cs - Cd) * As + Cd
      // Cs * As  + (1 - As) * Cd
      // s, d
      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_SRC_DST) {
      // (Cs - 0) * As + Cd
      // Cs * As + (1) * Cd
      // s, d
      ASSERT(draw.fix == 0);

      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::ZERO_SRC_SRC_DST) {
      // (0 - Cs) * As + Cd
      // Cd - Cs * As
      // s, d

      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional

      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_DST_FIX_DST) {
      // (Cs - Cd) * fix + Cd
      // Cs * fix + (1 - fx) * Cd

      colorBlending.blendConstants[0] = 0.0f;
      colorBlending.blendConstants[1] = 0.0f;
      colorBlending.blendConstants[2] = 0.0f;
      colorBlending.blendConstants[3] = draw.fix / 127.0f;

      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;

      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_SRC_SRC_SRC) {
      // this is very weird...
      // Cs

      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;

      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (draw.mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_DST_DST) {
      // (Cs - 0) * Ad + Cd

      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      color_mult = 0.5;
    } else {
      ASSERT(false);
    }
  }

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;
  if (draw.mode.get_zt_enable()) {
    depthStencil.depthTestEnable = VK_TRUE;
    switch (draw.mode.get_depth_test()) {
      case GsTest::ZTest::NEVER:
        depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
        break;
      case GsTest::ZTest::ALWAYS:
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        break;
      case GsTest::ZTest::GEQUAL:
        depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
        break;
      case GsTest::ZTest::GREATER:
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
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

void DirectRenderer2::setup_vulkan_tex(u16 unit,
                                       u16 tbp,
                                       bool filter,
                                       bool clamp_s,
                                       bool clamp_t,
                                       SharedRenderState* render_state) {
  // look up the texture
  VkImage tex;
  u32 tbp_to_lookup = tbp & 0x7fff;
  bool use_mt4hh = tbp & 0x8000;

  if (use_mt4hh) {
    tex = render_state->texture_pool->lookup_mt4hh(tbp_to_lookup);
  } else {
    tex = render_state->texture_pool->lookup(tbp_to_lookup);
  }

  if (!tex) {
    // TODO Add back
    if (tbp_to_lookup >= 8160 && tbp_to_lookup <= 8600) {
      fmt::print("Failed to find texture at {}, using random (eye zone)\n", tbp_to_lookup);

      tex = render_state->texture_pool->get_placeholder_texture();
    } else {
      fmt::print("Failed to find texture at {}, using random\n", tbp_to_lookup);
      tex = render_state->texture_pool->get_placeholder_texture();
    }
  }

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  if (clamp_s || clamp_t) {
    rasterizer.depthClampEnable = VK_TRUE;
  } else {
    rasterizer.depthClampEnable = VK_FALSE;
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

void DirectRenderer2::draw_debug_window() {
  ImGui::Text("Uploads: %d", m_stats.num_uploads);
  ImGui::Text("Upload time: %.3f ms", m_stats.upload_wait * 1000);
  ImGui::Text("Upload size: %d bytes", m_stats.upload_bytes);
  ImGui::Text("Flush due to full: %d times", m_stats.flush_due_to_full);
}

void DirectRenderer2::render_gif_data(const u8* data,
                                      SharedRenderState* render_state,
                                      ScopedProfilerNode& prof,
                                      UniformBuffer& uniform_buffer) {
  bool eop = false;

  u32 offset = 0;
  while (!eop) {
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
        handle_prim(tag.prim());
      }
      for (u32 loop = 0; loop < tag.nloop(); loop++) {
        for (u32 reg = 0; reg < nreg; reg++) {
          // fmt::print("{}\n", reg_descriptor_name(reg_desc[reg]));
          switch (reg_desc[reg]) {
            case GifTag::RegisterDescriptor::AD:
              handle_ad(data + offset);
              break;
            case GifTag::RegisterDescriptor::ST:
              handle_st_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::RGBAQ:
              handle_rgbaq_packed(data + offset);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              if (m_use_ftoi_mod) {
                handle_xyzf2_mod_packed(data + offset, render_state, prof, uniform_buffer);
              } else {
                handle_xyzf2_packed(data + offset, render_state, prof, uniform_buffer);
              }
              break;
            case GifTag::RegisterDescriptor::PRIM:
              ASSERT(false);  // handle_prim_packed(data + offset, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::TEX0_1:
              ASSERT(false);  // handle_tex0_1_packed(data + offset);
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
              ASSERT(false);  // handle_prim(register_data, render_state, prof);
              break;
            case GifTag::RegisterDescriptor::RGBAQ:
              ASSERT(false);  // handle_rgbaq(register_data);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              ASSERT(false);  // handle_xyzf2(register_data, render_state, prof);
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
}

void DirectRenderer2::handle_ad(const u8* data) {
  u64 value;
  GsRegisterAddress addr;
  memcpy(&value, data, sizeof(u64));
  memcpy(&addr, data + 8, sizeof(GsRegisterAddress));

  // fmt::print("{}\n", register_address_name(addr));
  switch (addr) {
    case GsRegisterAddress::ZBUF_1:
      handle_zbuf1(value);
      break;
    case GsRegisterAddress::TEST_1:
      handle_test1(value);
      break;
    case GsRegisterAddress::ALPHA_1:
      handle_alpha1(value);
      break;
    case GsRegisterAddress::PABE:
      // ASSERT(false);  // handle_pabe(value);
      ASSERT(value == 0);
      break;
    case GsRegisterAddress::CLAMP_1:
      handle_clamp1(value);
      break;
    case GsRegisterAddress::PRIM:
      ASSERT(false);  // handle_prim(value, render_state, prof);
      break;

    case GsRegisterAddress::TEX1_1:
      handle_tex1_1(value);
      break;
    case GsRegisterAddress::TEXA: {
      GsTexa reg(value);

      // rgba16 isn't used so this doesn't matter?
      // but they use sane defaults anyway
      ASSERT(reg.ta0() == 0);
      ASSERT(reg.ta1() == 0x80);  // note: check rgba16_to_rgba32 if this changes.

      ASSERT(reg.aem() == false);
    } break;
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
    default:
      ASSERT_MSG(false, fmt::format("Address {} is not supported", register_address_name(addr)));
  }
}

void DirectRenderer2::handle_test1(u64 val) {
  GsTest reg(val);
  ASSERT(!reg.date());  // datm doesn't matter
  if (m_state.gs_test != reg) {
    m_current_state_has_open_draw = false;
    m_state.gs_test = reg;
    m_state.as_mode.set_at(reg.alpha_test_enable());
    if (reg.alpha_test_enable()) {
      switch (reg.alpha_test()) {
        case GsTest::AlphaTest::NEVER:
          m_state.as_mode.set_alpha_test(DrawMode::AlphaTest::NEVER);
          break;
        case GsTest::AlphaTest::ALWAYS:
          m_state.as_mode.set_alpha_test(DrawMode::AlphaTest::ALWAYS);
          break;
        case GsTest::AlphaTest::GEQUAL:
          m_state.as_mode.set_alpha_test(DrawMode::AlphaTest::GEQUAL);
          break;
        default:
          ASSERT(false);
      }
    }

    m_state.as_mode.set_aref(reg.aref());
    m_state.as_mode.set_alpha_fail(reg.afail());
    m_state.as_mode.set_zt(reg.zte());
    m_state.as_mode.set_depth_test(reg.ztest());
  }
}

void DirectRenderer2::handle_zbuf1(u64 val) {
  GsZbuf x(val);
  ASSERT(x.psm() == TextureFormat::PSMZ24);
  ASSERT(x.zbp() == 448);
  bool write = !x.zmsk();
  if (write != m_state.as_mode.get_depth_write_enable()) {
    m_current_state_has_open_draw = false;
    m_state.as_mode.set_depth_write_enable(write);
  }
}

void DirectRenderer2::handle_tex0_1(u64 val) {
  GsTex0 reg(val);
  if (m_state.gs_tex0 != reg) {
    m_current_state_has_open_draw = false;
    m_state.gs_tex0 = reg;
    m_state.tbp = reg.tbp0();
    // tbw
    if (reg.psm() == GsTex0::PSM::PSMT4HH) {
      m_state.tbp |= 0x8000;
    }
    // tw/th
    m_state.as_mode.set_tcc(reg.tcc());
    m_state.set_tcc_flag(reg.tcc());
    bool decal = reg.tfx() == GsTex0::TextureFunction::DECAL;
    m_state.as_mode.set_decal(decal);
    m_state.set_decal_flag(decal);
    ASSERT(reg.tfx() == GsTex0::TextureFunction::DECAL ||
           reg.tfx() == GsTex0::TextureFunction::MODULATE);
  }
}

void DirectRenderer2::handle_tex1_1(u64 val) {
  GsTex1 reg(val);
  if (reg.mmag() != m_state.as_mode.get_filt_enable()) {
    m_current_state_has_open_draw = false;
    m_state.as_mode.set_filt_enable(reg.mmag());
  }
}

void DirectRenderer2::handle_clamp1(u64 val) {
  bool clamp_s = val & 0b001;
  bool clamp_t = val & 0b100;

  if ((clamp_s != m_state.as_mode.get_clamp_s_enable()) ||
      (clamp_t != m_state.as_mode.get_clamp_t_enable())) {
    m_current_state_has_open_draw = false;
    m_state.as_mode.set_clamp_s_enable(clamp_s);
    m_state.as_mode.set_clamp_t_enable(clamp_t);
  }
}

void DirectRenderer2::handle_prim(u64 val) {
  m_state.next_vertex_starts_strip = true;
  GsPrim reg(val);
  if (reg != m_state.gs_prim) {
    m_current_state_has_open_draw = false;
    ASSERT(reg.kind() == GsPrim::Kind::TRI_STRIP);
    ASSERT(reg.gouraud());
    if (!reg.tme()) {
      ASSERT(false);  // todo, might need this
    }
    m_state.as_mode.set_fog(reg.fge());
    m_state.set_fog_flag(reg.fge());
    m_state.as_mode.set_ab(reg.abe());
    ASSERT(!reg.aa1());
    ASSERT(!reg.fst());
    ASSERT(!reg.ctxt());
    ASSERT(!reg.fix());
  }
}

void DirectRenderer2::handle_st_packed(const u8* data) {
  memcpy(&m_state.s, data + 0, 4);
  memcpy(&m_state.t, data + 4, 4);
  memcpy(&m_state.Q, data + 8, 4);
}

void DirectRenderer2::handle_rgbaq_packed(const u8* data) {
  m_state.rgba[0] = data[0];
  m_state.rgba[1] = data[4];
  m_state.rgba[2] = data[8];
  m_state.rgba[3] = data[12];
}

void DirectRenderer2::handle_xyzf2_packed(const u8* data,
                                          SharedRenderState* render_state,
                                          ScopedProfilerNode& prof,
                                          UniformBuffer& uniform_buffer) {
  if (m_vertices.close_to_full()) {
    m_stats.flush_due_to_full++;
    flush_pending(render_state, prof, uniform_buffer);
  }

  u32 x, y;
  memcpy(&x, data, 4);
  memcpy(&y, data + 4, 4);

  u64 upper;
  memcpy(&upper, data + 8, 8);
  u32 z = (upper >> 4) & 0xffffff;

  u8 f = (upper >> 36);
  bool adc = !(upper & (1ull << 47));

  if (m_state.next_vertex_starts_strip) {
    m_state.next_vertex_starts_strip = false;
    m_vertices.indices[m_vertices.next_index++] = UINT32_MAX;
  }

  // push the vertex
  auto& vert = m_vertices.vertices[m_vertices.next_vertex++];
  auto vidx = m_vertices.next_vertex - 1;
  if (adc) {
    m_vertices.indices[m_vertices.next_index++] = vidx;
  } else {
    m_vertices.indices[m_vertices.next_index++] = UINT32_MAX;
    m_vertices.indices[m_vertices.next_index++] = vidx - 1;
    m_vertices.indices[m_vertices.next_index++] = vidx;
  }

  if (!m_current_state_has_open_draw) {
    m_current_state_has_open_draw = true;
    if (m_next_free_draw >= m_draw_buffer.size()) {
      ASSERT(false);
    }
    // pick a texture unit to use
    u8 tex_unit = 0;
    if (m_next_free_draw > 0) {
      tex_unit = (m_draw_buffer[m_next_free_draw - 1].tex_unit + 1) % TEX_UNITS;
    }
    auto& draw = m_draw_buffer[m_next_free_draw++];
    draw.mode = m_state.as_mode;
    draw.start_index = m_vertices.next_index;
    draw.tbp = m_state.tbp;
    draw.fix = m_state.gs_alpha.fix();
    // associate this draw with this texture unit.
    draw.tex_unit = tex_unit;
    m_state.tex_unit = tex_unit;
  }

  vert.xyz[0] = x;
  vert.xyz[1] = y;
  vert.xyz[2] = z;
  vert.rgba = m_state.rgba;
  vert.stq = math::Vector<float, 3>(m_state.s, m_state.t, m_state.Q);
  vert.tex_unit = m_state.tex_unit;
  vert.fog = f;
  vert.flags = m_state.vertex_flags;
}

void DirectRenderer2::handle_xyzf2_mod_packed(const u8* data,
                                              SharedRenderState* render_state,
                                              ScopedProfilerNode& prof,
                                              UniformBuffer& uniform_buffer) {
  if (m_vertices.close_to_full()) {
    m_stats.flush_due_to_full++;
    flush_pending(render_state, prof, uniform_buffer);
  }

  float x;
  float y;
  memcpy(&x, data, 4);
  memcpy(&y, data + 4, 4);

  u64 upper;
  memcpy(&upper, data + 8, 8);
  float z;
  memcpy(&z, &upper, 4);

  u8 f = (upper >> 36);
  bool adc = !(upper & (1ull << 47));

  if (m_state.next_vertex_starts_strip) {
    m_state.next_vertex_starts_strip = false;
    m_vertices.indices[m_vertices.next_index++] = UINT32_MAX;
  }

  // push the vertex
  auto& vert = m_vertices.vertices[m_vertices.next_vertex++];

  auto vidx = m_vertices.next_vertex - 1;
  if (adc) {
    m_vertices.indices[m_vertices.next_index++] = vidx;
  } else {
    m_vertices.indices[m_vertices.next_index++] = UINT32_MAX;
    m_vertices.indices[m_vertices.next_index++] = vidx - 1;
    m_vertices.indices[m_vertices.next_index++] = vidx;
  }

  if (!m_current_state_has_open_draw) {
    m_current_state_has_open_draw = true;
    if (m_next_free_draw >= m_draw_buffer.size()) {
      ASSERT(false);
    }
    // pick a texture unit to use
    u8 tex_unit = 0;
    if (m_next_free_draw > 0) {
      tex_unit = (m_draw_buffer[m_next_free_draw - 1].tex_unit + 1) % TEX_UNITS;
    }
    auto& draw = m_draw_buffer[m_next_free_draw++];
    draw.mode = m_state.as_mode;
    draw.start_index = m_vertices.next_index;
    draw.tbp = m_state.tbp;
    draw.fix = m_state.gs_alpha.fix();
    // associate this draw with this texture unit.
    draw.tex_unit = tex_unit;
    m_state.tex_unit = tex_unit;
  }

  // todo move to shader or something.
  vert.xyz[0] = x * 16.f;
  vert.xyz[1] = y * 16.f;
  vert.xyz[2] = z;
  vert.rgba = m_state.rgba;
  vert.stq = math::Vector<float, 3>(m_state.s, m_state.t, m_state.Q);
  vert.tex_unit = m_state.tex_unit;
  vert.fog = f;
  vert.flags = m_state.vertex_flags;
}

void DirectRenderer2::handle_alpha1(u64 val) {
  GsAlpha reg(val);
  if (m_state.gs_alpha != reg) {
    m_state.gs_alpha = reg;
    m_current_state_has_open_draw = false;
    auto a = reg.a_mode();
    auto b = reg.b_mode();
    auto c = reg.c_mode();
    auto d = reg.d_mode();
    if (a == GsAlpha::BlendMode::SOURCE && b == GsAlpha::BlendMode::DEST &&
        c == GsAlpha::BlendMode::SOURCE && d == GsAlpha::BlendMode::DEST) {
      m_state.as_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_SRC_DST);
    } else if (a == GsAlpha::BlendMode::SOURCE && b == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               c == GsAlpha::BlendMode::SOURCE && d == GsAlpha::BlendMode::DEST) {
      m_state.as_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_0_SRC_DST);
    } else if (a == GsAlpha::BlendMode::ZERO_OR_FIXED && b == GsAlpha::BlendMode::SOURCE &&
               c == GsAlpha::BlendMode::SOURCE && d == GsAlpha::BlendMode::DEST) {
      m_state.as_mode.set_alpha_blend(DrawMode::AlphaBlend::ZERO_SRC_SRC_DST);
    } else if (a == GsAlpha::BlendMode::SOURCE && b == GsAlpha::BlendMode::DEST &&
               c == GsAlpha::BlendMode::ZERO_OR_FIXED && d == GsAlpha::BlendMode::DEST) {
      m_state.as_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_FIX_DST);
    } else if (a == GsAlpha::BlendMode::SOURCE && b == GsAlpha::BlendMode::SOURCE &&
               c == GsAlpha::BlendMode::SOURCE && d == GsAlpha::BlendMode::SOURCE) {
      m_state.as_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_SRC_SRC_SRC);
    } else if (a == GsAlpha::BlendMode::SOURCE && b == GsAlpha::BlendMode::ZERO_OR_FIXED &&
               c == GsAlpha::BlendMode::DEST && d == GsAlpha::BlendMode::DEST) {
      m_state.as_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_0_DST_DST);
    } else {
      // unsupported blend: a 0 b 2 c 2 d 1
      // lg::error("unsupported blend: a {} b {} c {} d {}", (int)a, (int)b, (int)c, (int)d);
      //      ASSERT(false);
    }
  }
}
