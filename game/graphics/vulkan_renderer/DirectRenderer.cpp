#include "DirectRenderer.h"

#include "common/dma/gs.h"
#include "common/log/log.h"
#include "common/util/Assert.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

DirectVulkanRenderer::DirectVulkanRenderer(const std::string& name,
                                           int my_id,
                                           std::shared_ptr<GraphicsDeviceVulkan> device,
                                           VulkanInitializationInfo& vulkan_info,
                                           int batch_size)
    : BaseDirectRenderer(name, my_id, batch_size), BucketVulkanRenderer(device, vulkan_info) {
  m_push_constant.height_scale = 0.5;
  m_push_constant.scissor_adjust = -512 / 416.f;

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  m_ogl.vertex_buffer_max_verts = batch_size * 3 * 2;
  m_ogl.vertex_buffer_bytes = m_ogl.vertex_buffer_max_verts * sizeof(BaseDirectRenderer::Vertex);
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(device, m_ogl.vertex_buffer_bytes, 1, 1);

  m_direct_basic_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(
      m_direct_basic_fragment_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer->writeImage(
      0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  allocate_new_descriptor_set();

  create_pipeline_layout();
  InitializeInputVertexAttribute();
}

void DirectVulkanRenderer::set_current_index(u32 imageIndex) {
  while (imageIndex >= totalImageCount) {
    allocate_new_descriptor_set();
  }

  currentImageIndex = imageIndex;
}

void DirectVulkanRenderer::allocate_new_descriptor_set() {
  auto descriptorSetLayout = m_direct_basic_fragment_descriptor_layout->getDescriptorSetLayout();

  // One helper for renderer and one for debug renderer
  m_graphics_helper_map.insert(
      std::pair<u32, RendererGraphicsHelper>(totalImageCount++, RendererGraphicsHelper()));
  auto& graphics_helper = m_graphics_helper_map[totalImageCount - 1];

  graphics_helper.sampler = std::make_unique<VulkanSamplerHelper>(m_device);

  m_descriptor_sets.emplace_back();
  m_vulkan_info.descriptor_pool->allocateDescriptor(&descriptorSetLayout,
                                                    &m_descriptor_sets[totalImageCount - 1]);
}

void DirectVulkanRenderer::SetShaderModule(ShaderId shaderId) {
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

  m_pipeline_config_info.attributeDescriptions.clear();
  if (shaderId == ShaderId::DEBUG_RED) {
    m_pipeline_config_info.attributeDescriptions.insert(
        m_pipeline_config_info.attributeDescriptions.end(), debugRedAttributeDescriptions.begin(),
        debugRedAttributeDescriptions.end());
    m_pipeline_config_info.pipelineLayout = m_pipeline_layout;

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant.scissor_adjust),
                       (void*)&m_push_constant.scissor_adjust);
  } else if (shaderId == ShaderId::DIRECT_BASIC) {
    m_pipeline_config_info.attributeDescriptions.insert(
        m_pipeline_config_info.attributeDescriptions.end(),
        directBasicAttributeDescriptions.begin(), directBasicAttributeDescriptions.end());
    m_pipeline_config_info.pipelineLayout = m_pipeline_layout;

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant),
                       (void*)&m_push_constant);
  } else {
    m_pipeline_config_info.attributeDescriptions.insert(
        m_pipeline_config_info.attributeDescriptions.end(),
        directBasicTexturedAttributeDescriptions.begin(),
        directBasicTexturedAttributeDescriptions.end());
    m_pipeline_config_info.pipelineLayout = m_textured_pipeline_layout;

    m_textured_pipeline_push_constant.height_scale = m_push_constant.height_scale;
    m_textured_pipeline_push_constant.scissor_adjust = m_push_constant.scissor_adjust;
    m_textured_pipeline_push_constant.offscreen_mode = m_offscreen_mode;

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_textured_pipeline_push_constant),
                       (void*)&m_textured_pipeline_push_constant);
  }
}

void DirectVulkanRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(BaseDirectRenderer::Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(BaseDirectRenderer::Vertex, xyzf);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[1].offset = offsetof(BaseDirectRenderer::Vertex, rgba);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(BaseDirectRenderer::Vertex, stq);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8G8B8A8_UINT;
  attributeDescriptions[3].offset = offsetof(BaseDirectRenderer::Vertex, tex_unit);

  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 4;
  attributeDescriptions[4].format = VK_FORMAT_R8_UINT;
  attributeDescriptions[4].offset = offsetof(BaseDirectRenderer::Vertex, use_uv);

  attributeDescriptions[5].binding = 0;
  attributeDescriptions[5].location = 5;
  attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[5].offset = offsetof(BaseDirectRenderer::Vertex, scissor);

  debugRedAttributeDescriptions[0] = attributeDescriptions[0];
  directBasicAttributeDescriptions[0] = attributeDescriptions[0];
  directBasicTexturedAttributeDescriptions[0] = attributeDescriptions[0];

  directBasicAttributeDescriptions[1] = attributeDescriptions[1];
  directBasicTexturedAttributeDescriptions[1] = attributeDescriptions[1];

  directBasicTexturedAttributeDescriptions[2] = attributeDescriptions[2];
  directBasicTexturedAttributeDescriptions[3] = attributeDescriptions[3];
  directBasicTexturedAttributeDescriptions[4] = attributeDescriptions[4];
  directBasicTexturedAttributeDescriptions[5] = attributeDescriptions[5];
}

void DirectVulkanRenderer::render(DmaFollower& dma,
                                  SharedVulkanRenderState* render_state,
                                  ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_config_info.multisampleInfo.rasterizationSamples =
      m_vulkan_info.swap_chain->get_render_pass_sample_count();
  BaseDirectRenderer::render(dma, render_state, prof);
}

void DirectVulkanRenderer::create_pipeline_layout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_direct_basic_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(m_push_constant);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  pipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  VkPipelineLayoutCreateInfo texturedPipelineLayoutInfo{};
  texturedPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  texturedPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  texturedPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  std::array<VkPushConstantRange, 2> texturedPushConstantRanges = {};
  texturedPushConstantRanges[0].offset = 0;
  texturedPushConstantRanges[0].size = sizeof(m_textured_pipeline_push_constant);
  texturedPushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  texturedPushConstantRanges[1].offset = sizeof(math::Vector4f);
  texturedPushConstantRanges[1].size = sizeof(m_direct_basic_fragment_push_constant);
  texturedPushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  texturedPipelineLayoutInfo.pPushConstantRanges = texturedPushConstantRanges.data();
  texturedPipelineLayoutInfo.pushConstantRangeCount = texturedPushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &texturedPipelineLayoutInfo, nullptr,
                             &m_textured_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  VkPipelineLayoutCreateInfo debugRedPipelineLayoutInfo{};
  debugRedPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  debugRedPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  debugRedPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  VkPushConstantRange debugRedPushConstantRange = {};
  debugRedPushConstantRange.offset = 0;
  debugRedPushConstantRange.size = sizeof(m_push_constant.scissor_adjust);
  debugRedPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  debugRedPipelineLayoutInfo.pPushConstantRanges = &debugRedPushConstantRange;
  debugRedPipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &debugRedPipelineLayoutInfo, nullptr,
                             &m_debug_red_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

DirectVulkanRenderer::~DirectVulkanRenderer() {
  if (m_pipeline_layout) {
    vkDestroyPipelineLayout(m_device->getLogicalDevice(), m_pipeline_layout, nullptr);
  }
  if (m_textured_pipeline_layout) {
    vkDestroyPipelineLayout(m_device->getLogicalDevice(), m_textured_pipeline_layout, nullptr);
  }
  if (m_debug_red_pipeline_layout) {
    vkDestroyPipelineLayout(m_device->getLogicalDevice(), m_debug_red_pipeline_layout, nullptr);
  }
  m_vulkan_info.descriptor_pool->freeDescriptors(m_descriptor_sets);
}

void DirectVulkanRenderer::flush_pending(BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();

  BaseDirectRenderer::flush_pending(render_state, prof);
}

void DirectVulkanRenderer::update_graphics_prim(BaseSharedRenderState* render_state) {
  // currently gouraud is handled in setup.
  const auto& state = m_prim_graphics_state;
  if (state.texture_enable) {
    float alpha_reject = 0.0;
    if (m_test_state.alpha_test_enable) {
      switch (m_test_state.alpha_test) {
        case GsTest::AlphaTest::ALWAYS:
          break;
        case GsTest::AlphaTest::GEQUAL:
        case GsTest::AlphaTest::GREATER:
          alpha_reject = m_test_state.aref / 128.f;
          break;
        case GsTest::AlphaTest::NEVER:
          break;
        default:
          ASSERT_MSG(false, fmt::format("unknown alpha test: {}", (int)m_test_state.alpha_test));
      }
    }

    SetShaderModule(ShaderId::DIRECT_BASIC_TEXTURED);
    m_direct_basic_fragment_push_constant.alpha_reject = alpha_reject;
    m_direct_basic_fragment_push_constant.color_mult = m_ogl.color_mult;
    m_direct_basic_fragment_push_constant.alpha_mult = m_ogl.alpha_mult;
    m_direct_basic_fragment_push_constant.fog_color =
        math::Vector4f{render_state->fog_color[0] / 255.f, render_state->fog_color[1] / 255.f,
                       render_state->fog_color[2] / 255.f, render_state->fog_intensity / 255};
    m_textured_pipeline_push_constant.offscreen_mode = state.ta0 / 255.f;
    m_direct_basic_fragment_push_constant.scissor_enable = (m_scissor_enable && !m_offscreen_mode);
    auto swapchain_extents = m_vulkan_info.swap_chain->getSwapChainExtent();

    m_direct_basic_fragment_push_constant.game_sizes = math::Vector4f{
        512.0f, render_state->GetHeightScale(), swapchain_extents.width, swapchain_extents.height};

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(math::Vector4f),
                       sizeof(m_direct_basic_fragment_push_constant),
                       &m_direct_basic_fragment_push_constant);

  } else {
    SetShaderModule(ShaderId::DIRECT_BASIC);
  }
  if (state.fogging_enable) {
    //    ASSERT(false);
  }
  if (state.aa_enable) {
    ASSERT(false);
  }
  if (state.ctxt) {
    ASSERT(false);
  }
  if (state.fix) {
    ASSERT(false);
  }
}

void DirectVulkanRenderer::update_graphics_texture(BaseSharedRenderState* render_state, int unit) {
  VulkanTexture* tex;
  auto& state = m_buffered_tex_state[unit];
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
    lg::warn("Failed to find texture at {}, using random (direct: {})", state.texture_base_ptr,
             name_and_id());
    tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
  }
  ASSERT(tex);

  if (state.m_clamp_state.clamp_s || state.m_clamp_state.clamp_t) {
    m_pipeline_config_info.rasterizationInfo.depthClampEnable = VK_TRUE;
  } else {
    m_pipeline_config_info.rasterizationInfo.depthClampEnable = VK_FALSE;
  }

  std::unique_ptr<VulkanSamplerHelper>& sampler = m_graphics_helper_map[currentImageIndex].sampler;

  VkSamplerCreateInfo& samplerInfo = sampler->GetSamplerCreateInfo();
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

  sampler->CreateSampler();
  m_descriptor_image_info = VkDescriptorImageInfo{sampler->GetSampler(), tex->getImageView(),
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[0] =
      m_fragment_descriptor_writer->writeImageDescriptorSet(0, &m_descriptor_image_info, 1);

  m_fragment_descriptor_writer->overwrite(m_descriptor_sets[currentImageIndex]);
}

void DirectVulkanRenderer::update_graphics_blend() {
  const auto& state = m_blend_state;
  m_ogl.color_mult = 1.f;
  m_ogl.alpha_mult = 1.f;
  m_prim_graphics_state_needs_graphics_update = true;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
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
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

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

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor =
          VK_BLEND_FACTOR_CONSTANT_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;

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

void DirectVulkanRenderer::update_graphics_test() {
  const auto& state = m_test_state;

  m_pipeline_config_info.depthStencilInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
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
        m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
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

void DirectVulkanRenderer::render_and_draw_buffers(BaseSharedRenderState* render_state,
                                                   ScopedProfilerNode& prof) {
  // NOTE: sometimes we want to update the GL state without actually rendering anything, such as sky
  // textures, so we only return after we've updated the full state
  if (m_prim_buffer.vert_count == 0) {
    return;
  }

  if (m_debug_state.disable_texture) {
    // a bit of a hack, this forces the non-textured shader always.
    SetShaderModule(ShaderId::DIRECT_BASIC);
    m_blend_state_needs_graphics_update = true;
    m_prim_graphics_state_needs_graphics_update = true;
  }

  if (m_debug_state.red) {
    SetShaderModule(ShaderId::DEBUG_RED);
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
    m_prim_graphics_state_needs_graphics_update = true;
    m_blend_state_needs_graphics_update = true;
  }

  // hacks
  if (m_debug_state.always_draw) {
    m_pipeline_config_info.depthStencilInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
    m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  }

  // render!
  // update buffers:
  m_ogl.vertex_buffer->writeToGpuBuffer(m_prim_buffer.vertices.data(),
                                        sizeof(Vertex) * m_prim_buffer.vert_count);

  int draw_count = 0;

  m_graphics_helper_map[currentImageIndex].graphics_pipeline_layout->updateGraphicsPipeline(
      m_vulkan_info.render_command_buffer, m_pipeline_config_info);
  m_graphics_helper_map[currentImageIndex].graphics_pipeline_layout->bind(
      m_vulkan_info.render_command_buffer);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = m_ogl.vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, 1,
                          &m_descriptor_sets[currentImageIndex], 0, nullptr);
  // Hack OpenGL and Vulkan Y-Axis are inverted in VkCmdDraw(...) (OpenGL equivalent -
  // glDrawArrays(...)) This doesn't need to happen when using vkCmdDrawIndexed(...) (OpenGL
  // Equivalent - glDrawElements(...))
  for (unsigned i = 0; i < m_prim_buffer.vert_count; i++) {
    m_prim_buffer.vertices[i].xyzf[1] *= -1;
  }
  vkCmdDraw(m_vulkan_info.render_command_buffer, m_prim_buffer.vert_count, 1, 0, 0);

  draw_count++;

  if (m_debug_state.wireframe) {
    SetShaderModule(ShaderId::DEBUG_RED);
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
    m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;

    m_debug_graphics_pipeline_layout.updateGraphicsPipeline(m_vulkan_info.render_command_buffer,
                                                            m_pipeline_config_info);
    m_debug_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, 1,
                            &m_descriptor_sets[currentImageIndex], 0, nullptr);
    vkCmdDraw(m_vulkan_info.render_command_buffer, m_prim_buffer.vert_count, 1, 0, 0);

    m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
    m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
    m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;

    m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
    m_blend_state_needs_graphics_update = true;
    m_prim_graphics_state_needs_graphics_update = true;
    draw_count++;
  }
  for (unsigned i = 0; i < m_prim_buffer.vert_count; i++) {
    m_prim_buffer.vertices[i].xyzf[1] *= -1;
  }

  int n_tris = draw_count * (m_prim_buffer.vert_count / 3);
  prof.add_tri(n_tris);
  prof.add_draw_call(draw_count);
  m_stats.triangles += n_tris;
  m_stats.draw_calls += draw_count;
  m_prim_buffer.vert_count = 0;
}

void DirectVulkanRenderer::handle_trxdir(u64 dir,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  ASSERT(m_blit_buf_state.expect == 3);
  m_blit_buf_state.expect++;

  auto get_tex_func = [&](const std::string& name, u16 tbp) {
    auto result = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp);
    if (!result) {
      fmt::print("{} tbp {} not found\n", name, tbp);
    } else {
      fmt::print("{} tbp {} found\n", name, tbp);
    }
    return result;
  };
  fmt::print("GS TEXTURE COPY --\n");
  fmt::print("src w/psm: {}/{} dst w/psm: {}/{}\n", m_blit_buf_state.sbw, m_blit_buf_state.spsm,
             m_blit_buf_state.dbw, m_blit_buf_state.dpsm);
  switch (dir) {
    case 0: {  // host->local
      fmt::print("-- FROM EE\n");
      auto dst_tex = get_tex_func("dst", m_blit_buf_state.dbp);
      // ASSERT_MSG(false, "nyi trxdir host->local");
    } break;
    case 1: {  // local->host
      fmt::print("-- FROM GS\n");
      auto src_tex = get_tex_func("src", m_blit_buf_state.sbp);
      // ASSERT_MSG(false, "nyi trxdir local->host");
    } break;
    case 2: {  // local->local
      fmt::print("-- GS <-> GS\n");
      auto src_tex = get_tex_func("src", m_blit_buf_state.sbp);
      auto dst_tex = get_tex_func("dst", m_blit_buf_state.dbp);
    } break;
    case 3:  // disable
      fmt::print("-- HUH???\n");
      ASSERT_MSG(false, "nyi trxdir disable");
      break;
  }
}
