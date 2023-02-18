#include "ShadowRenderer.h"

#include <cfloat>

#include "third-party/imgui/imgui.h"

ShadowVulkanRenderer::ShadowVulkanRenderer(
    const std::string& name,
    int my_id,
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VulkanInitializationInfo& vulkan_info)
    : BaseShadowRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  m_pipeline_layouts.resize(5, m_device);

  // set up the vertex array
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
      device, sizeof(Vertex), MAX_VERTICES, 1);

  for (int i = 0; i < 2; i++) {
    m_ogl.index_buffers[i] = std::make_unique<IndexBuffer>(
        device, sizeof(u32), MAX_INDICES, 1);
  }

  m_uniform_buffer = std::make_unique<ShadowRendererUniformBuffer>(
      device, 5);

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  create_pipeline_layout();
  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(1);
  m_fragment_buffer_descriptor_info = VkDescriptorBufferInfo{
      m_uniform_buffer->getBuffer(),
      0,
      sizeof(math::Vector4f),
  };

  m_fragment_descriptor_writer->writeBuffer(0, &m_fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  // xyz
  InitializeInputVertexAttribute();
}

void ShadowVulkanRenderer::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  BaseShadowRenderer::render(dma, render_state, prof);
}

void ShadowVulkanRenderer::init_shaders(VulkanShaderLibrary& shaders) {
  auto& shader = shaders[ShaderId::SHADOW];

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

void ShadowVulkanRenderer::create_pipeline_layout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void ShadowVulkanRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, xyz);
  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescriptions[0]);
}

ShadowVulkanRenderer::~ShadowVulkanRenderer() {
}

void ShadowVulkanRenderer::draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  u32 draw_idx = 0;
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();

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

  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices);

  m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
  m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
  m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  m_pipeline_config_info.rasterizationInfo.depthBiasEnable = VK_FALSE;

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  if (m_debug_draw_volume) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  } else {
    m_pipeline_config_info.colorBlendAttachment.colorWriteMask = 0;
  }

  m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.writeMask = 0xFF;

  m_pipeline_config_info.depthStencilInfo.back.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.back.writeMask = 0xFF;

  // First pass.
  // here, we don't write depth or color.
  // but we increment stencil on depth fail.

  {
    m_uniform_buffer->SetUniform4f("color_uniform",
                0., 0.4, 0., 0.5, draw_idx);
    m_ogl.index_buffers[0]->writeToGpuBuffer(m_back_indices, m_next_front_index * sizeof(u32), 0);

    m_pipeline_config_info.depthStencilInfo.front.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.back.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    m_pipeline_config_info.depthStencilInfo.back.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;

    VulkanDraw(draw_idx, 0);

    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_uniform_buffer->SetUniform4f("color_uniform", 0.,
          0.0, 0., 0.5, draw_idx);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      VulkanDraw(draw_idx, 0);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    }
    prof.add_draw_call();
    prof.add_tri(m_next_back_index / 3);
  }

  {
    m_uniform_buffer->SetUniform4f("color_uniform",
                0.4, 0.0, 0., 0.5, draw_idx);

    m_ogl.index_buffers[1]->writeToGpuBuffer(m_back_indices, m_next_back_index * sizeof(u32), 0);

    // Second pass.

    m_pipeline_config_info.depthStencilInfo.front.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.back.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    m_pipeline_config_info.depthStencilInfo.back.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    VulkanDraw(draw_idx, 1);
    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_uniform_buffer->SetUniform4f("color_uniform", 0., 0.0, 0., 0.5, draw_idx);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      VulkanDraw(draw_idx, 1);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    }

    prof.add_draw_call();
    prof.add_tri(m_next_front_index / 3);
  }

  // finally, draw shadow.
  m_uniform_buffer->SetUniform4f("color_uniform", 0.13, 0.13, 0.13, 0.5, draw_idx);
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  m_pipeline_config_info.depthStencilInfo.front.compareMask = 0xFF;
  m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;

  m_pipeline_config_info.depthStencilInfo.back.compareMask = 0xFF;
  m_pipeline_config_info.depthStencilInfo.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;

  m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.back.passOp = VK_STENCIL_OP_KEEP;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  VulkanDraw(draw_idx, 0);

  prof.add_draw_call();
  prof.add_tri(2);

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
  //glDepthMask(GL_TRUE);

  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
}

void ShadowVulkanRenderer::VulkanDraw(uint32_t& pipeline_layout_id, uint32_t indexBufferId) {
  auto& index_buffer = m_ogl.index_buffers[indexBufferId % 2];
  m_fragment_buffer_descriptor_info = m_uniform_buffer->descriptorInfo();

  m_pipeline_layouts[pipeline_layout_id].createGraphicsPipeline(m_pipeline_config_info);
  m_pipeline_layouts[pipeline_layout_id].bind(m_vulkan_info.render_command_buffer);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = m_ogl.vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                       index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  uint32_t dynamicDescriptorOffset = pipeline_layout_id * sizeof(math::Vector4f);
  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, m_descriptor_sets.size(),
                          m_descriptor_sets.data(), 1, &dynamicDescriptorOffset);

  vkCmdDrawIndexed(m_vulkan_info.render_command_buffer,
                   index_buffer->getBufferSize() / sizeof(unsigned), 1, 0, 0, 0);
  pipeline_layout_id++;
}

ShadowRendererUniformBuffer::ShadowRendererUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                         uint32_t instanceCount,
                                                         VkDeviceSize minOffsetAlignment) :
  UniformVulkanBuffer(device, sizeof(math::Vector4f), instanceCount, minOffsetAlignment){
  section_name_to_memory_offset_map = {
      {"color_uniform", 0}
  };
}
