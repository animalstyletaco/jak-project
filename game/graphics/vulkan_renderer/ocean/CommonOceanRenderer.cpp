#include "CommonOceanRenderer.h"

CommonOceanRenderer::CommonOceanRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device)
    : m_pipeline_layout{device} {
  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);

  m_vertices.resize(4096 * 10);  // todo decrease
  for (auto& buf : m_indices) {
    buf.resize(4096 * 10);
  }

  // set up the vertex array
  for (int i = 0; i < NUM_BUCKETS; i++) {
    m_ogl.index_buffers[i] = std::make_unique<IndexBuffer>(
        device, sizeof(u32), m_indices[i].size(),
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);
  }
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
      device, sizeof(Vertex), m_vertices.size(),
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1);

  InitializeVertexInputAttributes();
}

void CommonOceanRenderer::InitializeVertexInputAttributes() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, xyz);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R4G4_UNORM_PACK8;
  attributeDescriptions[1].offset = offsetof(Vertex, rgba);

  // FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(Vertex, stq);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R4G4_UNORM_PACK8;
  attributeDescriptions[3].offset = offsetof(Vertex, fog);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void CommonOceanRenderer::SetShaders(SharedRenderState* render_state) {
  //auto& shader = render_state->shaders[ShaderId::OCEAN_COMMON];
}

CommonOceanRenderer::~CommonOceanRenderer() {
}

void CommonOceanRenderer::init_for_near() {
  m_next_free_vertex = 0;
  for (auto& x : m_next_free_index) {
    x = 0;
  }
}

void CommonOceanRenderer::kick_from_near(const u8* data) {
  bool eop = false;

  u32 offset = 0;
  while (!eop) {
    GifTag tag(data + offset);
    offset += 16;

    if (tag.nreg() == 3) {
      ASSERT(tag.pre());
      if (GsPrim(tag.prim()).kind() == GsPrim::Kind::TRI_STRIP) {
        handle_near_vertex_gif_data_strip(data, offset, tag.nloop());
      } else {
        handle_near_vertex_gif_data_fan(data, offset, tag.nloop());
      }
      offset += 16 * 3 * tag.nloop();
    } else if (tag.nreg() == 1) {
      handle_near_adgif(data, offset, tag.nloop());
      offset += 16 * 1 * tag.nloop();
    } else {
      ASSERT(false);
    }

    eop = tag.eop();
  }
}

void CommonOceanRenderer::handle_near_vertex_gif_data_strip(const u8* data, u32 offset, u32 loop) {
  m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = UINT32_MAX;
  bool reset_last = false;
  for (u32 i = 0; i < loop; i++) {
    auto& dest_vert = m_vertices[m_next_free_vertex++];

    // stq
    memcpy(dest_vert.stq.data(), data + offset, 12);
    offset += 16;

    // rgba
    dest_vert.rgba[0] = data[offset];
    dest_vert.rgba[1] = data[offset + 4];
    dest_vert.rgba[2] = data[offset + 8];
    dest_vert.rgba[3] = data[offset + 12];
    offset += 16;

    // xyz
    u32 x = 0, y = 0;
    memcpy(&x, data + offset, 4);
    memcpy(&y, data + offset + 4, 4);

    u64 upper;
    memcpy(&upper, data + offset + 8, 8);
    u32 z = (upper >> 4) & 0xffffff;
    offset += 16;

    dest_vert.xyz[0] = (float)(x << 16) / (float)UINT32_MAX;
    dest_vert.xyz[1] = (float)(y << 16) / (float)UINT32_MAX;
    dest_vert.xyz[2] = (float)(z << 8) / (float)UINT32_MAX;

    u8 f = (upper >> 36);
    dest_vert.fog = f;

    auto vidx = m_next_free_vertex - 1;
    bool adc = upper & (1ull << 47);
    if (!adc) {
      m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = vidx;
      reset_last = false;
    } else {
      if (reset_last) {
        m_next_free_index[m_current_bucket] -= 3;
      }
      m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = UINT32_MAX;
      m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = vidx - 1;
      m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = vidx;
      reset_last = true;
    }
  }
}

void CommonOceanRenderer::handle_near_vertex_gif_data_fan(const u8* data, u32 offset, u32 loop) {
  u32 ind_of_fan_start = UINT32_MAX;
  bool fan_running = false;
  // :regs0 (gif-reg-id st) :regs1 (gif-reg-id rgbaq) :regs2 (gif-reg-id xyzf2)
  for (u32 i = 0; i < loop; i++) {
    auto& dest_vert = m_vertices[m_next_free_vertex++];

    // stq
    memcpy(dest_vert.stq.data(), data + offset, 12);
    offset += 16;

    // rgba
    dest_vert.rgba[0] = data[offset];
    dest_vert.rgba[1] = data[offset + 4];
    dest_vert.rgba[2] = data[offset + 8];
    dest_vert.rgba[3] = data[offset + 12];
    offset += 16;

    // xyz
    u32 x = 0, y = 0;
    memcpy(&x, data + offset, 4);
    memcpy(&y, data + offset + 4, 4);

    u64 upper;
    memcpy(&upper, data + offset + 8, 8);
    u32 z = (upper >> 4) & 0xffffff;
    offset += 16;

    dest_vert.xyz[0] = (float)(x << 16) / (float)UINT32_MAX;
    dest_vert.xyz[1] = (float)(y << 16) / (float)UINT32_MAX;
    dest_vert.xyz[2] = (float)(z << 8) / (float)UINT32_MAX;

    u8 f = (upper >> 36);
    dest_vert.fog = f;

    auto vidx = m_next_free_vertex - 1;

    if (ind_of_fan_start == UINT32_MAX) {
      ind_of_fan_start = vidx;
    } else {
      if (fan_running) {
        // hack to draw fans with strips. this isn't efficient, but fans happen extremely rarely
        // (you basically have to put the camera intersecting the ocean and looking fwd)
        m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = UINT32_MAX;
        m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = vidx;
        m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = vidx - 1;
        m_indices[m_current_bucket][m_next_free_index[m_current_bucket]++] = ind_of_fan_start;
      } else {
        fan_running = true;
      }
    }
  }
}

void CommonOceanRenderer::handle_near_adgif(const u8* data, u32 offset, u32 count) {
  u32 most_recent_tbp = 0;

  for (u32 i = 0; i < count; i++) {
    u64 value;
    GsRegisterAddress addr;
    memcpy(&value, data + offset + 16 * i, sizeof(u64));
    memcpy(&addr, data + offset + 16 * i + 8, sizeof(GsRegisterAddress));
    switch (addr) {
      case GsRegisterAddress::MIPTBP1_1:
        // ignore this, it's just mipmapping settings
        break;
      case GsRegisterAddress::TEX1_1: {
        GsTex1 reg(value);
        ASSERT(reg.mmag());
      } break;
      case GsRegisterAddress::CLAMP_1: {
        bool s = value & 0b001;
        bool t = value & 0b100;
        ASSERT(s == t);
        if (s) {
          m_current_bucket = VertexBucket::ENV_MAP;
        }
      } break;
      case GsRegisterAddress::TEX0_1: {
        GsTex0 reg(value);
        ASSERT(reg.tfx() == GsTex0::TextureFunction::MODULATE);
        if (!reg.tcc()) {
          m_current_bucket = VertexBucket::RGB_TEXTURE;
        }
        most_recent_tbp = reg.tbp0();
      } break;
      case GsRegisterAddress::ALPHA_1: {
        // ignore, we've hardcoded alphas.
      } break;
      case GsRegisterAddress::FRAME_1: {
        u32 mask = value >> 32;
        if (mask) {
          m_current_bucket = VertexBucket::ALPHA;
        }
      } break;

      default:
        fmt::print("reg: {}\n", register_address_name(addr));
        break;
    }
  }

  if (m_current_bucket == VertexBucket::ENV_MAP) {
    m_envmap_tex = most_recent_tbp;
  }

  if (m_vertices.size() - 128 < m_next_free_vertex) {
    ASSERT(false);  // add more vertices.
  }
}

void CommonOceanRenderer::flush_near(SharedRenderState* render_state, ScopedProfilerNode& prof,
                                     std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_shader_buffer,
                                     std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_shader_buffer) {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_TRUE;

  m_ogl.vertex_buffer->map(m_next_free_vertex * sizeof(Vertex));
  m_ogl.vertex_buffer->writeToBuffer(m_vertices.data());
  m_ogl.vertex_buffer->unmap();

  uniform_fragment_shader_buffer->SetUniform4f(
      "fog_color",
              render_state->fog_color[0] / 255.f, render_state->fog_color[1] / 255.f,
              render_state->fog_color[2] / 255.f, render_state->fog_intensity / 255);

  //glDepthMask(GL_FALSE);

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  VkSamplerCreateInfo sampler_info{};

  for (int bucket = 0; bucket < NUM_BUCKETS; bucket++) {
    switch (bucket) {
      case 0: {
        //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        //glBlendEquation(GL_FUNC_ADD);

        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        auto tex = render_state->texture_pool->lookup(8160);
        if (!tex) {
          tex = render_state->texture_pool->get_placeholder_texture();
        }
        uniform_fragment_shader_buffer->SetUniform1i("tex_T0", 0);
        uniform_vertex_shader_buffer->SetUniform1i("bucket", 0);

        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
      }

      break;
      case 1:
        //glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        uniform_fragment_shader_buffer->SetUniform1f("alpha_mult", 1.f);
        uniform_vertex_shader_buffer->SetUniform1i("bucket", 1);
        break;
      case 2:
        auto tex = render_state->texture_pool->lookup(m_envmap_tex);
        if (!tex) {
          tex = render_state->texture_pool->get_placeholder_texture();
        }

        //glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        //glBlendEquation(GL_FUNC_ADD);

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        uniform_vertex_shader_buffer->SetUniform1i("bucket", 2);
        break;
    }
    m_ogl.index_buffers[bucket]->map(m_next_free_index[bucket] * sizeof(u32));
    m_ogl.index_buffers[bucket]->writeToBuffer(m_indices[bucket].data());
    m_ogl.index_buffers[bucket]->unmap();

    //glDrawElements(GL_TRIANGLE_STRIP, m_next_free_index[bucket], GL_UNSIGNED_INT, nullptr);
    prof.add_draw_call();
    prof.add_tri(m_next_free_index[bucket]);
  }
}

void CommonOceanRenderer::kick_from_mid(const u8* data) {
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
      if (tag.nreg() == 1) {
        ASSERT(!tag.pre());
        ASSERT(tag.nloop() == 5);
        handle_mid_adgif(data, offset);
        offset += 5 * 16;
      } else {
        ASSERT(tag.nreg() == 3);
        ASSERT(tag.pre());
        m_current_bucket = GsPrim(tag.prim()).abe() ? 1 : 0;

        int count = tag.nloop();
        if (GsPrim(tag.prim()).kind() == GsPrim::Kind::TRI_STRIP) {
          handle_near_vertex_gif_data_strip(data, offset, tag.nloop());
        } else {
          handle_near_vertex_gif_data_fan(data, offset, tag.nloop());
        }
        offset += 3 * 16 * count;
        // todo handle.
      }
    } else {
      ASSERT(false);  // format not packed or reglist.
    }

    eop = tag.eop();
  }
}

void CommonOceanRenderer::handle_mid_adgif(const u8* data, u32 offset) {
  u32 most_recent_tbp = 0;

  for (u32 i = 0; i < 5; i++) {
    u64 value;
    GsRegisterAddress addr;
    memcpy(&value, data + offset + 16 * i, sizeof(u64));
    memcpy(&addr, data + offset + 16 * i + 8, sizeof(GsRegisterAddress));
    switch (addr) {
      case GsRegisterAddress::MIPTBP1_1:
      case GsRegisterAddress::MIPTBP2_1:
        // ignore this, it's just mipmapping settings
        break;
      case GsRegisterAddress::TEX1_1: {
        GsTex1 reg(value);
        ASSERT(reg.mmag());
      } break;
      case GsRegisterAddress::CLAMP_1: {
        bool s = value & 0b001;
        bool t = value & 0b100;
        ASSERT(s == t);
      } break;
      case GsRegisterAddress::TEX0_1: {
        GsTex0 reg(value);
        ASSERT(reg.tfx() == GsTex0::TextureFunction::MODULATE);
        most_recent_tbp = reg.tbp0();
      } break;
      case GsRegisterAddress::ALPHA_1: {
      } break;

      default:
        fmt::print("reg: {}\n", register_address_name(addr));
        break;
    }
  }

  if (most_recent_tbp != 8160) {
    m_envmap_tex = most_recent_tbp;
  }

  if (m_vertices.size() - 128 < m_next_free_vertex) {
    ASSERT(false);  // add more vertices.
  }
}

void CommonOceanRenderer::init_for_mid() {
  m_next_free_vertex = 0;
  for (auto& x : m_next_free_index) {
    x = 0;
  }
}

void reverse_indices(u32* indices, u32 count) {
  if (count) {
    for (u32 a = 0, b = count - 1; a < b; a++, b--) {
      std::swap(indices[a], indices[b]);
    }
  }
}

void CommonOceanRenderer::flush_mid(
    SharedRenderState* render_state,
    ScopedProfilerNode& prof,
    std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_shader_buffer,
    std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_shader_buffer) {

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  //CreateVertexBuffer(m_vertices);

  uniform_fragment_shader_buffer->SetUniform4f(
      "fog_color",
              render_state->fog_color[0] / 255.f, render_state->fog_color[1] / 255.f,
              render_state->fog_color[2] / 255.f, render_state->fog_intensity / 255);

  //glDepthMask(GL_TRUE);
  //TODO: Add depth mask attribute to Texture VkImage

  // note:
  // there are some places where the game draws the same section of ocean twice, in this order:
  // - low poly mesh with ocean texture
  // - low poly mesh with envmap texture
  // - high poly mesh with ocean texture (overwrites previous draw)
  // - high poly mesh with envmap texture (overwrites previous draw)

  // we draw all ocean textures together and all envmap textures togther. luckily, there's a trick
  // we can use to get the same result.
  // first, we'll draw all ocean textures. The high poly mesh is drawn second, so it wins.
  // then, we'll draw all envmaps, but with two changes:
  // - first, we draw it in reverse, so the high poly versions are drawn first
  // - second, we'll modify the shader to set alpha = 0 of the destination. when the low poly
  //    version is drawn on top, it won't draw at all because of the blending mode
  //    (s_factor = DST_ALPHA, d_factor = 1)

  // draw it in reverse
  reverse_indices(m_indices[1].data(), m_next_free_index[1]);

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;


  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  VkSamplerCreateInfo sampler_info{};

  for (int bucket = 0; bucket < 2; bucket++) {
    switch (bucket) {
      case 0: {
        auto tex = render_state->texture_pool->lookup(8160);
        if (!tex) {
          tex = render_state->texture_pool->get_placeholder_texture();
        }
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        uniform_fragment_shader_buffer->SetUniform1i("tex_T0", 0);
        uniform_vertex_shader_buffer->SetUniform1i("bucket", 3);

        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      }

      break;
      case 1:
        auto tex = render_state->texture_pool->lookup(m_envmap_tex);
        if (!tex) {
          tex = render_state->texture_pool->get_placeholder_texture();
        }

        //glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        //glBlendEquation(GL_FUNC_ADD);
        m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;

        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        uniform_vertex_shader_buffer->SetUniform1i("bucket", 4);
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    }

    m_ogl.index_buffers[bucket]->map(m_next_free_index[bucket] * sizeof(u32));
    m_ogl.index_buffers[bucket]->writeToBuffer(m_indices[bucket].data());
    m_ogl.index_buffers[bucket]->unmap();
    //glDrawElements(GL_TRIANGLE_STRIP, m_next_free_index[bucket], GL_UNSIGNED_INT, nullptr);

    prof.add_draw_call();
    prof.add_tri(m_next_free_index[bucket]);
  }
}

CommonOceanVertexUniformBuffer::CommonOceanVertexUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkMemoryPropertyFlags memoryPropertyFlags,
    VkDeviceSize minOffsetAlignment)
    : UniformBuffer(device, sizeof(int), instanceCount, memoryPropertyFlags, minOffsetAlignment) {
  section_name_to_memory_offset_map = 
    {{"bucket", 0}};
}

CommonOceanFragmentUniformBuffer::CommonOceanFragmentUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkMemoryPropertyFlags memoryPropertyFlags,
    VkDeviceSize minOffsetAlignment )
    : UniformBuffer(device,
                    sizeof(CommonOceanFragmentUniformShaderData),
                    instanceCount,
                    memoryPropertyFlags,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"color_mult", offsetof(CommonOceanFragmentUniformShaderData, color_mult)},
      {"alpha_mult", offsetof(CommonOceanFragmentUniformShaderData, alpha_mult)},
      {"fog_color", offsetof(CommonOceanFragmentUniformShaderData, fog_color)},
      {"bucket", offsetof(CommonOceanFragmentUniformShaderData, fog_color)},
      {"tex_T0", offsetof(CommonOceanFragmentUniformShaderData, bucket)}};
}