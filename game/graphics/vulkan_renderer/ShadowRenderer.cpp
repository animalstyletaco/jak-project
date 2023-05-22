#include "ShadowRenderer.h"

#include <cfloat>

#include "third-party/imgui/imgui.h"

ShadowVulkanRenderer::ShadowVulkanRenderer(
    const std::string& name,
    int my_id,
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VulkanInitializationInfo& vulkan_info)
    : BaseShadowRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  m_graphics_pipeline_layouts.resize(5, m_device);

  // set up the vertex array
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
      device, sizeof(Vertex), MAX_VERTICES, 1);

  for (int i = 0; i < 2; i++) {
    m_ogl.index_buffers[i] = std::make_unique<IndexBuffer>(
        device, sizeof(u32), MAX_INDICES, 1);
  }

  create_pipeline_layout();
  // xyz
  InitializeInputVertexAttribute();
}

void ShadowVulkanRenderer::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();
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
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  std::array<VkPushConstantRange, 2> pushConstantRanges;
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(m_push_constant.scissor_adjust);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset = sizeof(m_color_uniform); //Offset need to be a multiple of the push constant size
  pushConstantRanges[1].size = sizeof(m_color_uniform);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

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
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  u32 draw_idx = 0;
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant.scissor_adjust),
                     (void*)&m_push_constant.scissor_adjust);

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
  
  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = m_ogl.vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan, offsets);

  m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
  m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
  m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
  m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  m_pipeline_config_info.rasterizationInfo.depthBiasEnable = VK_FALSE;

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

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
    m_color_uniform = math::Vector4f{0.0, 0.4, 0.0, 0.5};
    m_ogl.index_buffers[0]->writeToGpuBuffer(m_front_indices, m_next_front_index * sizeof(u32), 0);

    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    m_pipeline_config_info.depthStencilInfo.front.reference = 0;
    m_pipeline_config_info.depthStencilInfo.front.compareMask = 0;

    m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;

    m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

    PrepareVulkanDraw(draw_idx, 0);
    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_next_front_index - 6, 1, 0, 0, 0);

    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_color_uniform = math::Vector4f{0.0, 0.0, 0.0, 0.5};
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      PrepareVulkanDraw(draw_idx, 0);
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_next_front_index - 6, 1, 0, 0, 0);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    }
    prof.add_draw_call();
    prof.add_tri(m_next_back_index / 3);
  }

  {
    m_color_uniform = math::Vector4f{0.4, 0.0, 0.0, 0.5};

    m_ogl.index_buffers[1]->writeToGpuBuffer(m_back_indices, m_next_back_index * sizeof(u32), 0);

    // Second pass.
    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    m_pipeline_config_info.depthStencilInfo.front.reference = 0;
    m_pipeline_config_info.depthStencilInfo.front.compareMask = 0;

    m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;

    m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

    PrepareVulkanDraw(draw_idx, 1);
    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_next_back_index, 1, 0, 0, 0);

    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_color_uniform = math::Vector4f{0.0, 0.0, 0.0, 0.5};
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      PrepareVulkanDraw(draw_idx, 1);
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_next_back_index, 1, 0, 0, 0);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    }

    prof.add_draw_call();
    prof.add_tri(m_next_front_index / 3);
  }

  // finally, draw shadow.
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  m_color_uniform = math::Vector4f{0.13, 0.13, 0.13, 0.5};
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
  m_pipeline_config_info.depthStencilInfo.front.reference = 0;
  m_pipeline_config_info.depthStencilInfo.front.compareMask = 0xFF;

  m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;

  m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  PrepareVulkanDraw(draw_idx, 0);
  vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, 6, 1,
                   m_next_front_index - 6, 0, 0);

  prof.add_draw_call();
  prof.add_tri(2);

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
}

void ShadowVulkanRenderer::PrepareVulkanDraw(uint32_t& pipeline_layout_id, uint32_t indexBufferId) {
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_color_uniform), sizeof(m_color_uniform),
                     (void*)&m_color_uniform);

  auto& index_buffer = m_ogl.index_buffers[indexBufferId % 2];

  m_graphics_pipeline_layouts[pipeline_layout_id].createGraphicsPipeline(m_pipeline_config_info);
  m_graphics_pipeline_layouts[pipeline_layout_id].bind(m_vulkan_info.render_command_buffer);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                       index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  pipeline_layout_id++;
}
