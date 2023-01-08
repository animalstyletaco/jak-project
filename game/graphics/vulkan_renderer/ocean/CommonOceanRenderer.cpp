#include "CommonOceanRenderer.h"

CommonOceanVulkanRenderer::CommonOceanVulkanRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info)
    : m_device(device), m_pipeline_layout{device}, m_vulkan_info{vulkan_info} {
  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);
  for (int i = 0; i < 2 * NUM_BUCKETS; i++) {
    m_ogl.index_buffers[i] = std::make_unique<IndexBuffer>(
        device, sizeof(u32), m_indices[i].size(), 1);
  }
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
      device, sizeof(Vertex), m_vertices.size(), 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  CreatePipelineLayout();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  m_ocean_uniform_vertex_buffer = std::make_unique<CommonOceanVertexUniformBuffer>(m_device, 1, 1);
  m_ocean_uniform_fragment_buffer =
      std::make_unique<CommonOceanFragmentUniformBuffer>(m_device, 1, 1);

  auto vertex_buffer_descriptor_info = m_ocean_uniform_vertex_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_ocean_uniform_fragment_buffer->descriptorInfo();

  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);

  m_samplers.resize(2 * NUM_BUCKETS);
  m_descriptor_image_infos.resize(2 * NUM_BUCKETS);

  m_fragment_descriptor_writer->writeImage(
      1, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  InitializeVertexInputAttributes();
}

void CommonOceanVulkanRenderer::CreatePipelineLayout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_vertex_descriptor_layout->getDescriptorSetLayout(),
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  std::array<VkPushConstantRange, 2> pushConstantRanges;
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(int);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset = pushConstantRanges[0].size;
  pushConstantRanges[1].size = sizeof(int);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
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

void CommonOceanVulkanRenderer::flush_near(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_TRUE;

  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices.data(), m_next_free_vertex * sizeof(Vertex), 0);

  m_ocean_uniform_fragment_buffer->SetUniform4f(
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

  for (int bucket = 0; bucket < NUM_BUCKETS; bucket++) {
    VulkanTexture* tex = nullptr;
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

        tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(8160);
        if (!tex) {
          tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }

        m_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        m_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        m_sampler_info.magFilter = VK_FILTER_LINEAR;
        m_sampler_info.minFilter = VK_FILTER_LINEAR;

        if (m_samplers[bucket]) {
          vkDestroySampler(m_device->getLogicalDevice(), m_samplers[bucket], nullptr);
        }

        if (vkCreateSampler(m_device->getLogicalDevice(), &m_sampler_info, nullptr,
                            &m_samplers[bucket]) != VK_SUCCESS) {
          lg::error("Failed to create sampler in Generic-Vulkan-Renderer. \n");
        }
      }

      break;
      case 1:
        //glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        m_ocean_uniform_fragment_buffer->SetUniform1f("alpha_mult", 1.f);
        break;
      case 2:
        tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(m_envmap_tex);
        if (!tex) {
          m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }

        //glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        //glBlendEquation(GL_FUNC_ADD);

        m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
    }
    m_ogl.index_buffers[bucket]->writeToGpuBuffer(m_indices[bucket].data(),
                                                  m_next_free_index[bucket] * sizeof(u32));

    if (m_samplers[bucket]) {
      m_descriptor_image_infos[bucket] = VkDescriptorImageInfo{
          m_samplers[bucket], tex->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

      auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
      write_descriptors_info[0] = m_fragment_descriptor_writer->writeImageDescriptorSet(
          1, &m_descriptor_image_infos[bucket], 1);
    }

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), (void*)&bucket);
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 4, sizeof(int), (void*)&bucket);
    
    m_vulkan_info.swap_chain->drawIndexedCommandBuffer(
        m_vulkan_info.render_command_buffer, m_ogl.vertex_buffer.get(),
        m_ogl.index_buffers[bucket].get(), m_pipeline_config_info.pipelineLayout,
        m_descriptor_sets);
    //glDrawElements(GL_TRIANGLE_STRIP, m_next_free_index[bucket], GL_UNSIGNED_INT, nullptr);
    prof.add_draw_call();
    prof.add_tri(m_next_free_index[bucket]);
  }
}

void CommonOceanVulkanRenderer::flush_mid(
    BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) {

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  //CreateVertexBuffer(m_vertices);

  m_ocean_uniform_fragment_buffer->SetUniform4f(
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

  for (int bucket = NUM_BUCKETS; bucket < NUM_BUCKETS + 2; bucket++) {
    switch (bucket) {
      case 0: {
        auto tex = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(8160);
        if (!tex) {
          m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
        }
        m_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        m_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        m_ocean_uniform_fragment_buffer->SetUniform1i("tex_T0", 0);

        m_sampler_info.magFilter = VK_FILTER_LINEAR;
        m_sampler_info.minFilter = VK_FILTER_LINEAR;
        m_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      }

      break;
      case 1:
        auto tex = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(m_envmap_tex);
        if (!tex) {
          m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
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

        m_sampler_info.magFilter = VK_FILTER_LINEAR;
        m_sampler_info.minFilter = VK_FILTER_LINEAR;
        m_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    }

    m_ogl.index_buffers[bucket]->writeToGpuBuffer(m_indices[bucket].data(),
                                                  m_next_free_index[bucket] * sizeof(u32));

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), (void*)&bucket);
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 4, sizeof(int), (void*)&bucket);

    m_vulkan_info.swap_chain->drawIndexedCommandBuffer(
      m_vulkan_info.render_command_buffer, m_ogl.vertex_buffer.get(), m_ogl.index_buffers[bucket].get(),
      m_pipeline_config_info.pipelineLayout, m_descriptor_sets);

    //glDrawElements(GL_TRIANGLE_STRIP, m_next_free_index[bucket], GL_UNSIGNED_INT, nullptr);

    prof.add_draw_call();
    prof.add_tri(m_next_free_index[bucket]);
  }
}

CommonOceanVulkanRenderer::~CommonOceanVulkanRenderer() {
  for (auto& sampler : m_samplers) {
    if (sampler) {
      vkDestroySampler(m_device->getLogicalDevice(), sampler, nullptr);
    }
  }
}

CommonOceanVertexUniformBuffer::CommonOceanVertexUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device, sizeof(int), instanceCount, minOffsetAlignment) {
  section_name_to_memory_offset_map = 
    {{"bucket", 0}};
}

CommonOceanFragmentUniformBuffer::CommonOceanFragmentUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment )
    : UniformVulkanBuffer(device,
                    sizeof(CommonOceanFragmentUniformShaderData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"color_mult", offsetof(CommonOceanFragmentUniformShaderData, color_mult)},
      {"alpha_mult", offsetof(CommonOceanFragmentUniformShaderData, alpha_mult)},
      {"fog_color", offsetof(CommonOceanFragmentUniformShaderData, fog_color)},
      {"bucket", offsetof(CommonOceanFragmentUniformShaderData, fog_color)},
      {"tex_T0", offsetof(CommonOceanFragmentUniformShaderData, bucket)}};
}
