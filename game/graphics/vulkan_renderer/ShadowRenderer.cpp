#include "ShadowRenderer.h"

#include <cfloat>

#include "third-party/imgui/imgui.h"

ShadowRenderer::ShadowRenderer(const std::string& name, BucketId my_id, VkDevice device)
    : BucketRenderer(name, my_id, device) {
  // set up the vertex array
  u32 index_buffer[MAX_INDICES] = {0};
  Vertex vertex_buffer[MAX_VERTICES];
  for (int i = 0; i < 2; i++) {
    //m_index_buffers[i] = CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer, MAX_INDICES);
  }

  // m_vertex_buffer = CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, index_buffer, MAX_VERTEX);
  // xyz
  InitializeInputVertexAttribute();
}

void ShadowRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, xyz);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}

void ShadowRenderer::draw_debug_window() {
  ImGui::Checkbox("Volume", &m_debug_draw_volume);
  ImGui::Text("Vert: %d, Front: %d, Back: %d\n", m_next_vertex, m_next_front_index,
              m_next_back_index);
}

ShadowRenderer::~ShadowRenderer() {
  //glDeleteBuffers(1, &m_ogl.vertex_buffer);
  //glDeleteBuffers(2, m_ogl.index_buffer);
  //glDeleteVertexArrays(1, &m_ogl.vao);
}

void ShadowRenderer::xgkick(u16 imm) {
  u32 ind_of_fan_start = UINT32_MAX;
  bool fan_running = false;
  const u8* data = (const u8*)(m_vu_data + imm);

  u8 rgba[4] = {1, 2, 3, 4};

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
        GsPrim prim(tag.prim());
        ASSERT(prim.kind() == GsPrim::Kind::TRI_FAN);
      }
      for (u32 loop = 0; loop < tag.nloop(); loop++) {
        for (u32 reg = 0; reg < nreg; reg++) {
          switch (reg_desc[reg]) {
            case GifTag::RegisterDescriptor::AD: {
              u64 value;
              GsRegisterAddress addr;
              memcpy(&value, data + offset, sizeof(u64));
              memcpy(&addr, data + offset + 8, sizeof(GsRegisterAddress));

              switch (addr) {
                case GsRegisterAddress::TEXFLUSH:
                  break;
                case GsRegisterAddress::RGBAQ: {
                  rgba[0] = data[0 + offset];
                  rgba[1] = data[1 + offset];
                  rgba[2] = data[2 + offset];
                  rgba[3] = data[3 + offset];
                  float Q;
                  memcpy(&Q, data + offset + 4, 4);
                  // fmt::print("rgba: {} {} {} {}: {}\n", rgba[0], rgba[1], rgba[2], rgba[3], Q);
                } break;
                default:
                  ASSERT_MSG(false, fmt::format("Address {} is not supported",
                                                register_address_name(addr)));
              }
            } break;
            case GifTag::RegisterDescriptor::ST: {
              float s, t;
              memcpy(&s, data + offset, 4);
              memcpy(&t, data + offset + 4, 4);
              // fmt::print("st: {} {}\n", s, t);
            } break;
            case GifTag::RegisterDescriptor::RGBAQ:
              for (int i = 0; i < 4; i++) {
                rgba[i] = data[offset + i * 4];
              }
              // fmt::print("rgbap: {} {} {} {}\n", rgba[0], rgba[1], rgba[2], rgba[3]);
              break;
            case GifTag::RegisterDescriptor::XYZF2:
              // handle_xyzf2_packed(data + offset, render_state, prof);
              {
                u32 x, y;
                memcpy(&x, data + offset, 4);
                memcpy(&y, data + offset + 4, 4);

                u64 upper;
                memcpy(&upper, data + offset + 8, 8);
                u32 z = (upper >> 4) & 0xffffff;

                x <<= 16;
                y <<= 16;
                z <<= 8;
                u32 vidx = m_next_vertex++;
                auto& v = m_vertices[vidx];
                ASSERT(m_next_vertex < MAX_VERTICES);
                v.xyz[0] = (float)x / (float)UINT32_MAX;
                v.xyz[1] = (float)y / (float)UINT32_MAX;
                v.xyz[2] = (float)z / (float)UINT32_MAX;

                if (ind_of_fan_start == UINT32_MAX) {
                  ind_of_fan_start = vidx;
                } else {
                  if (fan_running) {
                    // todo, actually use triangle fans in opengl...
                    if (rgba[0] > 0) {
                      // back
                      m_back_indices[m_next_back_index++] = vidx;
                      m_back_indices[m_next_back_index++] = vidx - 1;
                      m_back_indices[m_next_back_index++] = ind_of_fan_start;
                    } else {
                      m_front_indices[m_next_front_index++] = vidx;
                      m_front_indices[m_next_front_index++] = vidx - 1;
                      m_front_indices[m_next_front_index++] = ind_of_fan_start;
                    }
                  } else {
                    fan_running = true;
                  }
                }

                // fmt::print("xyzfadc: {} {} {} {} {}\n", x, y, z, f, adc);
              }
              break;
            default:
              fmt::print("Register {} is not supported in packed mode yet\n",
                         reg_descriptor_name(reg_desc[reg]));
              ASSERT(false);
          }
          offset += 16;  // PACKED = quadwords
        }
      }
    } else {
      ASSERT(false);  // format not packed or reglist.
    }

    eop = tag.eop();
  }
}

void ShadowRenderer::render(DmaFollower& dma,
                            SharedRenderState* render_state,
                            ScopedProfilerNode& prof) {
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  m_next_vertex = 0;
  m_next_back_index = 0;
  m_next_front_index = 0;

  // jump to bucket
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.size_bytes == 0);

  // see if bucket is empty or not
  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }

  {
    // constants
    auto constants = dma.read_and_advance();
    auto v0 = constants.vifcode0();
    auto v1 = constants.vifcode1();
    ASSERT(v0.kind == VifCode::Kind::STCYCL);
    ASSERT(v0.immediate == 0x404);
    ASSERT(v1.kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(v1.immediate == Vu1Data::CONSTANTS);
    ASSERT(v1.num == 13);
    memcpy(m_vu_data + v1.immediate, constants.data, v1.num * 16);
  }

  {
    // gif constants
    auto constants = dma.read_and_advance();
    auto v0 = constants.vifcode0();
    auto v1 = constants.vifcode1();
    ASSERT(v0.kind == VifCode::Kind::STCYCL);
    ASSERT(v0.immediate == 0x404);
    ASSERT(v1.kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(v1.immediate == Vu1Data::GIF_CONSTANTS);
    ASSERT(v1.num == 4);
    memcpy(m_vu_data + v1.immediate, constants.data, v1.num * 16);
  }

  {
    // matrix constants
    auto constants = dma.read_and_advance();
    auto v0 = constants.vifcode0();
    auto v1 = constants.vifcode1();
    ASSERT(v0.kind == VifCode::Kind::STCYCL);
    ASSERT(v0.immediate == 0x404);
    ASSERT(v1.kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(v1.immediate == Vu1Data::MATRIX);
    ASSERT(v1.num == 4);
    memcpy(m_vu_data + v1.immediate, constants.data, v1.num * 16);
  }

  {
    // exec 10
    auto mscal = dma.read_and_advance();
    ASSERT(mscal.vifcode1().kind == VifCode::Kind::FLUSHE);
    ASSERT(mscal.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(mscal.vifcode0().immediate == Vu1Code::INIT);
    run_mscal10_vu2c();
  }

  {
    // init gs direct
    dma.read_and_advance();
  }

  while (dma.current_tag().kind != DmaTag::Kind::CALL) {
    auto next = dma.read_and_advance();
    auto v1 = next.vifcode1();
    if (next.vifcode0().kind == VifCode::Kind::FLUSHA &&
        next.vifcode1().kind == VifCode::Kind::UNPACK_V4_32) {
      auto up = next.vifcode1();
      VifCodeUnpack unpack(up);
      ASSERT(!unpack.use_tops_flag);
      ASSERT((u32)unpack.addr_qw + up.num < 1024);
      memcpy(m_vu_data + unpack.addr_qw, next.data, up.num * 16);
      ASSERT(up.num * 16 == next.size_bytes);
    } else if (next.vifcode0().kind == VifCode::Kind::NOP &&
               next.vifcode1().kind == VifCode::Kind::UNPACK_V4_32) {
      auto up = next.vifcode1();
      VifCodeUnpack unpack(up);
      ASSERT(!unpack.use_tops_flag);
      ASSERT((u32)unpack.addr_qw + up.num < 1024);
      memcpy(m_vu_data + unpack.addr_qw, next.data, up.num * 16);
      ASSERT(up.num * 16 == next.size_bytes);
    } else if (next.vifcode0().kind == VifCode::Kind::NOP &&
               next.vifcode1().kind == VifCode::Kind::UNPACK_V4_8) {
      auto up = VifCodeUnpack(v1);
      ASSERT(!up.use_tops_flag);
      ASSERT(up.is_unsigned);
      u16 addr = up.addr_qw;
      ASSERT(addr + v1.num <= 1024);

      u32 temp[4];
      for (u32 i = 0; i < v1.num; i++) {
        for (u32 j = 0; j < 4; j++) {
          temp[j] = next.data[4 * i + j];
        }
        memcpy(m_vu_data + addr + i, temp, 16);
      }

      u32 offset = 4 * v1.num;
      ASSERT(offset + 16 == next.size_bytes);

      u32 after[4];
      memcpy(&after, next.data + offset, 16);
      ASSERT(after[0] == 0);
      ASSERT(after[1] == 0);
      ASSERT(after[2] == 0);
      VifCode mscal(after[3]);
      ASSERT(mscal.kind == VifCode::Kind::MSCALF);
      run_mscal_vu2c(mscal.immediate);
    } else if (next.vifcode0().kind == VifCode::Kind::FLUSHA &&
               next.vifcode1().kind == VifCode::Kind::DIRECT) {
      dma.read_and_advance();
      dma.read_and_advance();
      dma.read_and_advance();

    } else {
      ASSERT_MSG(false, fmt::format("{} {}", next.vifcode0().print(), next.vifcode1().print()));
    }
  }

  for (int i = 0; i < 4; i++) {
    dma.read_and_advance();
  }
  ASSERT(dma.current_tag_offset() == render_state->next_bucket);

  draw(render_state, prof);
}

void ShadowRenderer::draw(SharedRenderState* render_state, ScopedProfilerNode& prof) {
  // enable stencil!
  glStencilMask(0xFF);

  u32 clear_vertices = m_next_vertex;
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.3, 0.3, 0), 0};
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.3, 0.7, 0), 0};
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.7, 0.3, 0), 0};
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.7, 0.7, 0), 0};
  m_front_indices[m_next_front_index++] = clear_vertices;
  m_front_indices[m_next_front_index++] = clear_vertices + 1;
  m_front_indices[m_next_front_index++] = clear_vertices + 2;
  m_front_indices[m_next_front_index++] = clear_vertices + 3;
  m_front_indices[m_next_front_index++] = clear_vertices + 2;
  m_front_indices[m_next_front_index++] = clear_vertices + 1;

  //SetVertexBuffer(0, m_vertices, m_next_vertex); glBufferData(GL_ARRAY_BUFFER, m_next_vertex * sizeof(Vertex), m_vertices, GL_STREAM_DRAW);

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

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

  glDepthMask(GL_FALSE);  // no depth writes.
  if (m_debug_draw_volume) {
    colorBlendAttachment.blendEnable = VK_TRUE;

    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  } else {
    colorBlendAttachment.colorWriteMask = 0;
  }

  depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
  depthStencil.front.writeMask = 0xFF;

  depthStencil.back.failOp = VK_STENCIL_OP_KEEP;
  depthStencil.back.writeMask = 0xFF;


  // First pass.
  // here, we don't write depth or color.
  // but we increment stencil on depth fail.

  {
    m_uniform_buffer.SetUniform4f("color_uniform",
                0., 0.4, 0., 0.5);
    // SetIndexBuffer(0, m_back_indices, m_next_front_index * sizeof(u32));

    depthStencil.front.compareMask = 0;
    depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS;

    depthStencil.back.compareMask = 0;
    depthStencil.back.compareOp = VK_COMPARE_OP_ALWAYS;

    depthStencil.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    depthStencil.back.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;

    glDrawElements(GL_TRIANGLES, (m_next_front_index - 6), GL_UNSIGNED_INT, nullptr);

    if (m_debug_draw_volume) {
      colorBlendAttachment.blendEnable = VK_FALSE;
      m_uniform_buffer.SetUniform4f("color_uniform", 0.,
          0.0, 0., 0.5);
      rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
      //glDrawElements(GL_TRIANGLES, (m_next_front_index - 6), GL_UNSIGNED_INT, nullptr);
      rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
      colorBlendAttachment.blendEnable = VK_TRUE;
    }
    prof.add_draw_call();
    prof.add_tri(m_next_back_index / 3);
  }

  {
    m_uniform_buffer.SetUniform4f("color_uniform",
                0.4, 0.0, 0., 0.5);

    //SetIndexBuffer(0, m_back_indices, m_next_back_index * sizeof(u32));

    // Second pass.

    depthStencil.front.compareMask = 0;
    depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS;

    depthStencil.back.compareMask = 0;
    depthStencil.back.compareOp = VK_COMPARE_OP_ALWAYS;

    depthStencil.front.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    depthStencil.back.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    //glDrawElements(GL_TRIANGLES, m_next_back_index, GL_UNSIGNED_INT, nullptr);
    if (m_debug_draw_volume) {
      colorBlendAttachment.blendEnable = VK_FALSE;
      m_uniform_buffer.SetUniform4f("color_uniform", 0., 0.0, 0., 0.5);
      rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
      //glDrawElements(GL_TRIANGLES, (m_next_back_index - 0), GL_UNSIGNED_INT, nullptr);
      rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
      colorBlendAttachment.blendEnable = VK_TRUE;
    }

    prof.add_draw_call();
    prof.add_tri(m_next_front_index / 3);
  }

  // finally, draw shadow.
  m_uniform_buffer.SetUniform4f("color_uniform", 0.13, 0.13, 0.13, 0.5);
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  depthStencil.front.compareMask = 0xFF;
  depthStencil.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;

  depthStencil.back.compareMask = 0xFF;
  depthStencil.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;

  depthStencil.front.passOp = VK_STENCIL_OP_KEEP;
  depthStencil.back.passOp = VK_STENCIL_OP_KEEP;

  colorBlendAttachment.blendEnable = VK_TRUE;

  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional

  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(sizeof(u32) * (m_next_front_index - 6)));
  prof.add_draw_call();
  prof.add_tri(2);

  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
  glDepthMask(GL_TRUE);

  depthStencil.stencilTestEnable = VK_FALSE;
}