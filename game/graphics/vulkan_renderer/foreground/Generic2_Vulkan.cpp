#include "Generic2.h"

GenericVulkan2::GenericVulkan2(const std::string& name,
                               int my_id,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               VulkanInitializationInfo& vulkan_info,
                               u32 num_verts,
                               u32 num_frags,
                               u32 num_adgif,
                               u32 num_buckets)
    : BucketVulkanRenderer(device, vulkan_info), BaseGeneric2(name, my_id, num_verts, num_frags, num_adgif, num_buckets) {
  graphics_setup();
}

GenericVulkan2::~GenericVulkan2() {
  graphics_cleanup();
}

/*!
 * Main render function for GenericVulkan2. This will be passed a DMA "follower" from the main
 * VulkanRenderer that can read a DMA chain, starting at the DMA "bucket" that was filled by the
 * generic renderer. This renderer is expected to follow the chain until it reaches "next_bucket"
 * and then return.
 */
void GenericVulkan2::render(DmaFollower& dma,
                            SharedVulkanRenderState* render_state,
                            ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseGeneric2::render(dma, render_state, prof);
}

void GenericVulkan2::graphics_setup() {
  m_descriptor_image_infos.resize(m_buckets.size(), *m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  m_graphics_pipeline_layouts.resize(m_buckets.size(), m_device);
  m_samplers.resize(m_buckets.size(), m_device);

  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
    m_device, sizeof(Vertex), m_verts.size(), 1);
  m_ogl.index_buffer = std::make_unique<IndexBuffer>(
      m_device, sizeof(u32), m_indices.size(), 1);

  m_vertex_uniform_buffer = std::make_unique<GenericCommonVertexUniformBuffer>(
    m_device, 1, 1);
  m_fragment_uniform_buffer = std::make_unique<GenericCommonFragmentUniformBuffer>(
      m_device, m_buckets.size(), 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout = DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT).build();

  create_pipeline_layout();
  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, m_vulkan_info.descriptor_pool);


  m_vertex_buffer_descriptor_info = m_vertex_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .build(m_vertex_descriptor_set);
  m_fragment_buffer_descriptor_info = m_fragment_uniform_buffer->descriptorInfo();
  m_fragment_descriptor_writer->writeBuffer(0, &m_fragment_buffer_descriptor_info);

  auto descriptorSetLayout = m_fragment_descriptor_layout->getDescriptorSetLayout();
  m_fragment_descriptor_sets.resize(m_buckets.size());
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{m_buckets.size(), descriptorSetLayout};

  m_vulkan_info.descriptor_pool->allocateDescriptor(
      descriptorSetLayouts.data(), m_fragment_descriptor_sets.data(), m_fragment_descriptor_sets.size());

  m_fragment_descriptor_writer->writeImage(
      1, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());  // Using placeholder for initializating
                                                         // descriptor writer info for now
  InitializeInputAttributes();
}

void GenericVulkan2::create_pipeline_layout() {
  //If push constants are needed put them here

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{m_vertex_descriptor_layout->getDescriptorSetLayout(),
                                                          m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  VkPushConstantRange pushConstantVertexRange = {};
  pushConstantVertexRange.offset = 0;
  pushConstantVertexRange.size = sizeof(m_push_constant);
  pushConstantVertexRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkPushConstantRange pushConstantFragmentRange = {};
  pushConstantFragmentRange.offset = pushConstantVertexRange.size;
  pushConstantFragmentRange.size = sizeof(int);
  pushConstantFragmentRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkPushConstantRange, 2> pushConstantRanges = {pushConstantVertexRange,
                                                           pushConstantFragmentRange};

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void GenericVulkan2::graphics_cleanup() {
  m_vulkan_info.descriptor_pool->freeDescriptors(m_fragment_descriptor_sets);
}

void GenericVulkan2::init_shaders(VulkanShaderLibrary& shaders) {
  auto& shader = shaders[ShaderId::GENERIC];

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

void GenericVulkan2::graphics_bind_and_setup_proj(BaseSharedRenderState* render_state) {
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

  m_vertex_uniform_buffer->map();
  m_vertex_uniform_buffer->flush();
  m_vertex_uniform_buffer->unmap();
}

void GenericVulkan2::setup_graphics_for_draw_mode(const DrawMode& draw_mode,
                                          u8 fix,
                                          BaseSharedRenderState* render_state, uint32_t bucket) {
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
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    } else if (draw_mode.get_alpha_blend() == DrawMode::AlphaBlend::SRC_0_SRC_DST) {
      // (Cs - 0) * As + Cd
      // Cs * As + (1) * Cd
      // s, d
      // fix is ignored. it's usually 0, except for lightning, which sets it to 0x80.

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
      color_mult = 1.0f;

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

  m_fragment_uniform_buffer->SetUniform1f("alpha_reject", alpha_reject, bucket);
  m_fragment_uniform_buffer->SetUniform1f("color_mult", color_mult, bucket);
  m_fragment_uniform_buffer->SetUniform4f(
              "fog_color", render_state->fog_color[0] / 255.f,
              render_state->fog_color[1] / 255.f, render_state->fog_color[2] / 255.f,
              render_state->fog_intensity / 255, bucket);
}

void GenericVulkan2::setup_graphics_tex(u16 unit,
                                        u16 tbp,
                                        bool filter,
                                        bool clamp_s,
                                        bool clamp_t,
                                        BaseSharedRenderState* render_state, u32 bucketId) {
  // look up the texture
  VulkanTexture* texture = NULL;
  u32 tbp_to_lookup = tbp & 0x7fff;
  bool use_mt4hh = tbp & 0x8000;

  if (use_mt4hh) {
    texture = m_vulkan_info.texture_pool->lookup_mt4hh_vulkan_texture(tbp_to_lookup);
  } else {
    texture = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp_to_lookup);
  }

  if (!texture) {
    // TODO Add back
    if (tbp_to_lookup >= 8160 && tbp_to_lookup <= 8600) {
      fmt::print("Failed to find texture at {}, using random (eye zone)\n", tbp_to_lookup);

      texture = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
    } else {
      fmt::print("Failed to find texture at {}, using random\n", tbp_to_lookup);
      texture = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
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
  samplerInfo.maxAnisotropy = m_device->getMaxSamplerAnisotropy();
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
    // if (mipmap) { //TODO: Add option for mipmapping
    //  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    //}
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
  } else {
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
  }

  m_samplers[bucketId].CreateSampler();

  m_descriptor_image_infos[bucketId] = VkDescriptorImageInfo{
      m_samplers[bucketId].GetSampler(), texture->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}

void GenericVulkan2::do_hud_draws(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  for (u32 i = 0; i < m_next_free_bucket; i++) {
    auto& bucket = m_buckets[i];
    auto& first = m_adgifs[bucket.start];
    if (first.uses_hud) {
      setup_graphics_for_draw_mode(first.mode, first.fix, render_state, i);
      setup_graphics_tex(0, first.tbp, first.mode.get_filt_enable(), first.mode.get_clamp_s_enable(),
                       first.mode.get_clamp_t_enable(), render_state, i);

      FinalizeVulkanDraws(i, bucket.idx_count, bucket.idx_idx);

      prof.add_draw_call();
      prof.add_tri(bucket.tri_count);
    }
  }
}

void GenericVulkan2::do_draws(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  if (m_next_free_vert > 0) {
    m_ogl.vertex_buffer->writeToGpuBuffer(m_verts.data(), m_next_free_vert * sizeof(Vertex), 0);
  }

  if (m_next_free_idx > 0) {
    m_ogl.index_buffer->writeToGpuBuffer(m_indices.data(), m_next_free_idx * sizeof(u32), 0);
  }

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = m_ogl.vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, m_ogl.index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  // glEnable(GL_PRIMITIVE_RESTART);
  // glPrimitiveRestartIndex(UINT32_MAX);

  graphics_bind_and_setup_proj(render_state);
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
    m_vertex_uniform_buffer->SetUniform4f("scale", m_drawing_config.hud_scale[0],
                                          m_drawing_config.hud_scale[1],
                                          m_drawing_config.hud_scale[2], 0);
    m_vertex_uniform_buffer->SetUniform1f("mat_23", m_drawing_config.hud_mat_23);
    m_vertex_uniform_buffer->SetUniform1f("mat_32", m_drawing_config.hud_mat_32);
    m_vertex_uniform_buffer->SetUniform1f("mat_33", m_drawing_config.hud_mat_33);

    do_hud_draws(render_state, prof);
  }
}

void GenericVulkan2::do_draws_for_alpha(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof,
                                        DrawMode::AlphaBlend alpha,
                                        bool hud) {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  for (u32 i = 0; i < m_next_free_bucket; i++) {
    auto& bucket = m_buckets[i];
    auto& first = m_adgifs[bucket.start];
    if (first.mode.get_alpha_blend() == alpha && first.uses_hud == hud) {
      setup_graphics_for_draw_mode(first.mode, first.fix, render_state, i);
      setup_graphics_tex(0, first.tbp, first.mode.get_filt_enable(), first.mode.get_clamp_s_enable(),
                       first.mode.get_clamp_t_enable(), render_state, i);

      FinalizeVulkanDraws(i, bucket.idx_count, bucket.idx_idx);

      prof.add_draw_call();
      prof.add_tri(bucket.tri_count);
    }
  }
}

void GenericVulkan2::FinalizeVulkanDraws(u32 bucket, u32 indexCount, u32 firstIndex) {
  if (!m_next_free_bucket) {
    return;
  }

  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[1] = m_fragment_descriptor_writer->writeImageDescriptorSet(1, &m_descriptor_image_infos[bucket]);

  m_fragment_descriptor_writer->overwrite(m_fragment_descriptor_sets[bucket]);
  
  m_graphics_pipeline_layouts[bucket].createGraphicsPipeline(m_pipeline_config_info);
  m_graphics_pipeline_layouts[bucket].bind(m_vulkan_info.render_command_buffer);

  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                 VK_SHADER_STAGE_VERTEX_BIT, 0,
                 sizeof(m_push_constant), (void*)&m_push_constant);

  std::vector<VkDescriptorSet> descriptor_sets = {m_vertex_descriptor_set,
                                                  m_fragment_descriptor_sets[bucket]};

  std::array<uint32_t, 1> dynamic_descriptor_offsets = {
      bucket * m_fragment_uniform_buffer->getAlignmentSize()};

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, descriptor_sets.size(),
                          descriptor_sets.data(), dynamic_descriptor_offsets.size(),
                          dynamic_descriptor_offsets.data());

  vkCmdDrawIndexed(m_vulkan_info.render_command_buffer,
                   indexCount, 1, firstIndex, 0, 0);
}

void GenericVulkan2::InitializeInputAttributes() {
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
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(GenericCommonVertexUniformShaderData),
                          instanceCount,
                          minOffsetAlignment) {
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
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                    sizeof(GenericCommonFragmentUniformShaderData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"alpha_reject", offsetof(GenericCommonFragmentUniformShaderData, alpha_reject)},
      {"color_mult", offsetof(GenericCommonFragmentUniformShaderData, color_mult)},
      {"fog_color", offsetof(GenericCommonFragmentUniformShaderData, fog_color)}};
}
