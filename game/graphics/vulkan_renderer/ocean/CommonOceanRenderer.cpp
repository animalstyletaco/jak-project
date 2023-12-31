#include "CommonOceanRenderer.h"

CommonOceanVulkanRenderer::CommonOceanVulkanRenderer(std::shared_ptr<GraphicsDeviceVulkan> device,
                                                     VulkanInitializationInfo& vulkan_info)
    : m_device(device), m_vulkan_info{vulkan_info} {
  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);

  m_vertex_push_constant.height_scale = 0.5;
  m_vertex_push_constant.scissor_adjust = -512 / 416.0;

  InitializeVertexInputAttributes();
  InitializeShaders();

  vertex_buffer = std::make_unique<VertexBuffer>(device, sizeof(Vertex), m_vertices.size(), 1);

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  CreatePipelineLayout();
  m_ocean_near = std::make_unique<OceanVulkanGraphicsHelper>(
      device, 4096 * 10, m_fragment_descriptor_layout, vulkan_info.descriptor_pool,
      vulkan_info.texture_pool->get_placeholder_descriptor_image_info()),
  m_ocean_mid = std::make_unique<OceanVulkanGraphicsHelper>(
      device, 4096 * 10, m_fragment_descriptor_layout, vulkan_info.descriptor_pool,
      vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  m_pipeline_config_info.inputAssemblyInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;
}

void CommonOceanVulkanRenderer::CreatePipelineLayout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  std::array<VkPushConstantRange, 2> pushConstantRanges{};
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(VertexPushConstant);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset = pushConstantRanges[0].size;
  pushConstantRanges[1].size = sizeof(FragmentPushConstant);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  m_device->createPipelineLayout(&pipelineLayoutInfo, nullptr,
                                 &m_pipeline_config_info.pipelineLayout);
}

void CommonOceanVulkanRenderer::InitializeShaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::OCEAN_COMMON];

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
void CommonOceanVulkanRenderer::InitializeVertexInputAttributes() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, xyz);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[1].offset = offsetof(Vertex, rgba);

  // FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(Vertex, stq);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8_UINT;
  attributeDescriptions[3].offset = offsetof(Vertex, fog);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void CommonOceanVulkanRenderer::flush_near(BaseSharedRenderState* render_state,
                                           ScopedProfilerNode& prof) {
  setup_graphics_draw(render_state);

  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  for (int bucket = 0; bucket < NUM_BUCKETS; bucket++) {
    VulkanTexture* tex = nullptr;

    auto& sampler_create_info = m_ocean_near->ocean_samplers[bucket]->GetSamplerCreateInfo();
    switch (bucket) {
      case 0: {
        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        auto tbp = GetOceanTextureId();
        tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp);
        if (!tex) {
          tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }
        break;
      }

      case 1: {
        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        m_fragment_push_constant.alpha_mult = 1.f;

        auto tbp = GetOceanTextureId();
        tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp);
        if (!tex) {
          tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }

        break;
      }

      case 2:
        tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(m_envmap_tex);
        if (!tex) {
          tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }
        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
    }
    sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    sampler_create_info.magFilter = VK_FILTER_LINEAR;
    sampler_create_info.minFilter = VK_FILTER_LINEAR;

    m_ocean_near->ocean_samplers[bucket]->CreateSampler();

    FinalizeVulkanDraw(m_ocean_near, tex, bucket);

    prof.add_draw_call();
    prof.add_tri(m_next_free_index[bucket]);
  }
}

void CommonOceanVulkanRenderer::flush_mid(BaseSharedRenderState* render_state,
                                          ScopedProfilerNode& prof) {
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
  setup_graphics_draw(render_state);

  // draw it in reverse
  reverse_indices(m_indices[1].data(), m_next_free_index[1]);

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  m_pipeline_config_info.colorBlendInfo.pAttachments = &m_pipeline_config_info.colorBlendAttachment;

  for (int bucket = 0; bucket < 2; bucket++) {
    VulkanTexture* tex = nullptr;

    auto& sampler_create_info = m_ocean_mid->ocean_samplers[bucket]->GetSamplerCreateInfo();
    switch (bucket) {
      case 0: {
        auto tbp = GetOceanTextureId();
        tex = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(tbp)->get_selected_texture();
        if (!tex) {
          tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        m_ocean_mid->ocean_samplers[bucket]->CreateSampler();
      }

      break;
      case 1:
        tex = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(m_envmap_tex)
                  ->get_selected_texture();
        if (!tex) {
          tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }

        m_pipeline_config_info.colorBlendInfo.logicOpEnable = VK_TRUE;

        m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        m_ocean_mid->ocean_samplers[bucket]->CreateSampler();
        break;
    }

    FinalizeVulkanDraw(m_ocean_mid, tex, bucket);

    prof.add_draw_call();
    prof.add_tri(m_next_free_index[bucket]);
  }
}

void CommonOceanVulkanRenderer::setup_graphics_draw(BaseSharedRenderState* render_state) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();

  m_vulkan_info.swap_chain->setViewportScissor(m_command_buffer);
  m_graphics_pipeline_layout.updateGraphicsPipeline(m_command_buffer,
                                                    m_pipeline_config_info);
  m_graphics_pipeline_layout.bind(m_command_buffer);

  vertex_buffer->writeToGpuBuffer(m_vertices.data(), m_next_free_vertex * sizeof(Vertex), 0);

  m_fragment_push_constant.fog_color =
  math::Vector4f{render_state->fog_color[0], render_state->fog_color[1],
                 render_state->fog_color[2], render_state->fog_intensity} / 255.f;

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);
}

CommonOceanVulkanRenderer::OceanVulkanGraphicsHelper::OceanVulkanGraphicsHelper(
    std::shared_ptr<GraphicsDeviceVulkan> device,
    u32 index_count,
    std::unique_ptr<DescriptorLayout>& setLayout,
    std::unique_ptr<DescriptorPool>& descriptor_pool,
    VkDescriptorImageInfo* defaultImageInfo)
    : m_descriptor_pool(descriptor_pool) {
  for (int i = 0; i < NUM_BUCKETS; i++) {
    ocean_samplers[i] = std::make_unique<VulkanSamplerHelper>(device);
    index_buffers[i] = std::make_unique<IndexBuffer>(device, sizeof(u32), index_count, 1);

    fragment_descriptor_writers[i] = std::make_unique<DescriptorWriter>(setLayout, descriptor_pool);
    fragment_descriptor_writers[i]->writeImage(0, defaultImageInfo).build(descriptor_sets[i]);
  }
}

CommonOceanVulkanRenderer::OceanVulkanGraphicsHelper::~OceanVulkanGraphicsHelper() {
  std::vector<VkDescriptorSet> descriptorSets;
  for (uint32_t i = 0; i < NUM_BUCKETS; i++) {
    descriptorSets.push_back(descriptor_sets[i]);
  }
  m_descriptor_pool->freeDescriptors(descriptorSets);
}

void CommonOceanVulkanRenderer::FinalizeVulkanDraw(
    std::unique_ptr<OceanVulkanGraphicsHelper>& ocean_graphics,
    VulkanTexture* texture,
    uint32_t bucket) {
  ocean_graphics->index_buffers[bucket]->writeToGpuBuffer(m_indices[bucket].data(),
                                                          m_next_free_index[bucket] * sizeof(u32));

  if (ocean_graphics->ocean_samplers[bucket]->GetSampler()) {
    ocean_graphics->descriptor_image_infos[bucket] =
        VkDescriptorImageInfo{ocean_graphics->ocean_samplers[bucket]->GetSampler(),
                              texture->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    auto& write_descriptors_sets =
        ocean_graphics->fragment_descriptor_writers[bucket]->getWriteDescriptorSets();
    write_descriptors_sets[0] =
        ocean_graphics->fragment_descriptor_writers[bucket]->writeImageDescriptorSet(
            0, &ocean_graphics->descriptor_image_infos[bucket], 1);
    ocean_graphics->fragment_descriptor_writers[bucket]->overwrite(
        ocean_graphics->descriptor_sets[bucket]);
  }

  m_graphics_pipeline_layout.updateGraphicsPipeline(m_command_buffer,
                                                    m_pipeline_config_info);

  m_vertex_push_constant.bucket = bucket;
  m_fragment_push_constant.bucket = bucket;

  vkCmdPushConstants(m_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_vertex_push_constant),
                     (void*)&m_vertex_push_constant);
  vkCmdPushConstants(m_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_vertex_push_constant),
                     sizeof(m_fragment_push_constant), (void*)&m_fragment_push_constant);

  vkCmdBindIndexBuffer(m_command_buffer,
                       ocean_graphics->index_buffers[bucket]->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

  vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, 1,
                          &ocean_graphics->descriptor_sets[bucket], 0, nullptr);

  vkCmdDrawIndexed(m_command_buffer, m_next_free_index[bucket], 1, 0, 0, 0);
}

CommonOceanVulkanRenderer::~CommonOceanVulkanRenderer() {}
