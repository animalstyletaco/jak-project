#include "Shadow2.h"

ShadowVulkan2::ShadowVulkan2(const std::string& name,
                 int my_id,
                 std::shared_ptr<GraphicsDeviceVulkan> device,
                 VulkanInitializationInfo& vulkan_info)
    : BucketVulkanRenderer(device, vulkan_info), BaseShadow2(name, my_id) {
  for (int i = 0; i < 2; i++) {
    m_ogl.index_buffers[i] = std::make_unique<IndexBuffer>(m_device, sizeof(u32) * kMaxInds, 1);
  }
  m_ogl.vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(ShadowVertex) * kMaxVerts, 1);

  m_ogl.front_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);
  m_ogl.front_debug_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);
  m_ogl.back_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);
  m_ogl.back_debug_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);

  m_ogl.lighten_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);
  m_ogl.darken_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);

  InitializeInputAttributes();
  init_shaders(m_vulkan_info.shaders);
}

ShadowVulkan2::~ShadowVulkan2() {
}

void ShadowVulkan2::init_shaders(VulkanShaderLibrary& library) {
  auto& shader = library[ShaderId::SHADOW2];

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

void ShadowVulkan2::create_pipeline_layout() {
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  std::array<VkPushConstantRange, 2> pushConstantRanges;
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(m_ogl.vertex_push_constant);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset = pushConstantRanges[0].size;
  pushConstantRanges[1].size = sizeof(m_ogl.fragment_push_constant);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void ShadowVulkan2::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseShadow2::render(dma, render_state, prof);
}

void ShadowVulkan2::draw_buffers(BaseSharedRenderState* render_state,
                           ScopedProfilerNode& prof,
                           const FrameConstants& constants) {
  if (!m_front_index_buffer_used && !m_back_index_buffer_used) {
    return;
  }

  if (render_state->stencil_dirty) {
    //FIXME: Figure out how to clear stencil attachment when attached to existing swap chain
    //glClearStencil(0);
    //glClear(GL_STENCIL_BUFFER_BIT);
  }
  render_state->stencil_dirty = true;

  // vertex_push_constant:
  m_ogl.vertex_push_constant.hvdf_offset = constants.constants.hvdfoff;
  m_ogl.vertex_push_constant.fog = constants.constants.fog[0];
  for (int i = 0; i < 4; i++) {
    m_ogl.vertex_push_constant.perspectives[i] = constants.camera.v[i];
  }
  m_ogl.vertex_push_constant.clear_mode = 0;

  // enable stencil!
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.writeMask = 0xFF;

  m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

  u32 clear_vertices = m_vertex_buffer_used;
  m_vertex_buffer[m_vertex_buffer_used++] = ShadowVertex{math::Vector3f(0.3, 0.3, 0), 0};
  m_vertex_buffer[m_vertex_buffer_used++] = ShadowVertex{math::Vector3f(0.3, 0.7, 0), 0};
  m_vertex_buffer[m_vertex_buffer_used++] = ShadowVertex{math::Vector3f(0.7, 0.3, 0), 0};
  m_vertex_buffer[m_vertex_buffer_used++] = ShadowVertex{math::Vector3f(0.7, 0.7, 0), 0};
  m_front_index_buffer[m_front_index_buffer_used++] = clear_vertices;
  m_front_index_buffer[m_front_index_buffer_used++] = clear_vertices + 1;
  m_front_index_buffer[m_front_index_buffer_used++] = clear_vertices + 2;
  m_front_index_buffer[m_front_index_buffer_used++] = clear_vertices + 3;
  m_front_index_buffer[m_front_index_buffer_used++] = UINT32_MAX;
  m_front_index_buffer[m_front_index_buffer_used++] = UINT32_MAX;

  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertex_buffer.data(),
                                        m_vertex_buffer_used * sizeof(ShadowVertex), 0);

  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;

  if (m_debug_draw_volume) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  } else {
    m_pipeline_config_info.colorBlendAttachment.colorWriteMask = 0;
  }

  // First pass.
  // here, we don't write depth or color.
  // but we increment stencil on depth fail.

  {
    m_ogl.fragment_push_constant = math::Vector4f{0., 0.4, 0., 0.5};
    m_ogl.index_buffers[0]->writeToGpuBuffer(m_front_index_buffer.data(),
                                             m_front_index_buffer_used * sizeof(u32), 0);

    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;

    m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

    PrepareVulkanDraw(m_ogl.front_graphics_pipeline_layout);
    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_front_index_buffer_used - 6, 1, 0, 0,
                     0);

    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_ogl.fragment_push_constant = math::Vector4f{0., 0.4, 0., 0.5};
      //m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;

      PrepareVulkanDraw(m_ogl.front_debug_graphics_pipeline_layout);
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_front_index_buffer_used - 6, 1, 0, 0,
                       0);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
      prof.add_draw_call();
      prof.add_tri(m_front_index_buffer_used / 3);
    }
    prof.add_draw_call();
    prof.add_tri(m_front_index_buffer_used / 3);
  }

  {
    m_ogl.fragment_push_constant = math::Vector4f{0.4, 0.0, 0., 0.5};
    m_ogl.index_buffers[1]->writeToGpuBuffer(m_back_index_buffer.data(),
                                             m_back_index_buffer_used * sizeof(u32), 0);

    // Second pass.
    // same settings, but decrement.
    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;

    m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

    PrepareVulkanDraw(m_ogl.back_graphics_pipeline_layout);
    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_back_index_buffer_used, 1, 0, 0, 0);
    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_ogl.fragment_push_constant = math::Vector4f{0., 0.0, 0., 0.5};
      // m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      PrepareVulkanDraw(m_ogl.back_debug_graphics_pipeline_layout);
      vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_back_index_buffer_used, 1, 0, 0, 0);

      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
      prof.add_draw_call();
      prof.add_tri(m_back_index_buffer_used / 3);
    }

    prof.add_draw_call();
    prof.add_tri(m_back_index_buffer_used / 3);
  }

  // finally, draw shadow.
  m_ogl.vertex_push_constant.clear_mode = 1;

  m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
  m_pipeline_config_info.depthStencilInfo.front.reference = 0;
  m_pipeline_config_info.depthStencilInfo.front.compareMask = 0xFF;

  m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;

  m_pipeline_config_info.depthStencilInfo.back = m_pipeline_config_info.depthStencilInfo.front;

  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  bool have_darken = false;
  bool have_lighten = false;
  bool lighten_channel[3] = {false, false, false};
  bool darken_channel[3] = {false, false, false};
  for (int i = 0; i < 3; i++) {
    if (m_color[i] > 128) {
      have_lighten = true;
      lighten_channel[i] = true;
    } else if (m_color[i] < 128) {
      have_darken = true;
      darken_channel[i] = true;
    }
  }

  if (have_darken) {
    m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
        GetColorMaskSettings(darken_channel[0], darken_channel[1], darken_channel[2], false);
    m_ogl.fragment_push_constant = math::Vector4f{(128 - m_color[0]) / 256.f, (128 - m_color[1]) / 256.f,
                (128 - m_color[2]) / 256.f, 0};
    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
    PrepareVulkanDraw(m_ogl.lighten_graphics_pipeline_layout);
    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, 6, 1, m_front_index_buffer_used - 6, 0,
                     0);
  }

  if (have_lighten) {
    m_pipeline_config_info.colorBlendAttachment.colorWriteMask = GetColorMaskSettings(lighten_channel[0], lighten_channel[1], lighten_channel[2], false);
    m_ogl.fragment_push_constant = math::Vector4f{
        (m_color[0] - 128) / 256.f, (m_color[1] - 128) / 256.f, (m_color[2] - 128) / 256.f, 0};
    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
    PrepareVulkanDraw(m_ogl.darken_graphics_pipeline_layout);
    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, 6, 1, m_front_index_buffer_used - 6, 0,
                     0);
  }

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;

  prof.add_draw_call();
  prof.add_tri(2);
  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
}


void ShadowVulkan2::InitializeInputAttributes() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(ShadowVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  VkVertexInputAttributeDescription attributeDescription;
  attributeDescription.binding = 0;
  attributeDescription.location = 0;
  attributeDescription.format =
      VK_FORMAT_R32G32B32_SFLOAT;  // Is there a way to normalize floats
  attributeDescription.offset = offsetof(ShadowVertex, pos);

  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescription);
}

void ShadowVulkan2::PrepareVulkanDraw(std::unique_ptr<GraphicsPipelineLayout>& graphics_pipeline_layout){
  graphics_pipeline_layout->updateGraphicsPipeline(
      m_vulkan_info.render_command_buffer, m_pipeline_config_info);
  graphics_pipeline_layout->bind(m_vulkan_info.render_command_buffer);

  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_ogl.vertex_push_constant),
                     &m_ogl.vertex_push_constant);
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(m_ogl.fragment_push_constant),
                     &m_ogl.fragment_push_constant);
}

VkColorComponentFlags ShadowVulkan2::GetColorMaskSettings(bool red_enabled, bool green_enabled, bool blue_enabled, bool alpha_enabled){
  VkColorComponentFlags flags = 0;
  flags |= (red_enabled) ? VK_COLOR_COMPONENT_R_BIT : 0;
  flags |= (green_enabled) ? VK_COLOR_COMPONENT_G_BIT : 0;
  flags |= (blue_enabled) ? VK_COLOR_COMPONENT_B_BIT : 0;
  flags |= (alpha_enabled) ? VK_COLOR_COMPONENT_A_BIT : 0;
  return flags;
}
