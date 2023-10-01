#include "GraphicsPipelineLayout.h"
#include <cassert>
#include <stdexcept>
#include <algorithm>

GraphicsPipelineLayout::GraphicsPipelineLayout(std::shared_ptr<GraphicsDeviceVulkan> device)
    : _device{device} {
}

GraphicsPipelineLayout::~GraphicsPipelineLayout() {
  destroyPipeline();
}

void GraphicsPipelineLayout::destroyPipeline() {
  if (m_graphics_pipeline) {
    vkDestroyPipeline(m_device->getLogicalDevice(), m_graphics_pipeline, nullptr);
    m_graphics_pipeline = nullptr;
  }
}

bool GraphicsPipelineLayout::IsDynamicStateFeatureEnabled(VkDynamicState dynamicState) {
  auto iter = std::find_first_of(m_current_pipeline_config.dynamicStateEnables.begin(),
                                 m_current_pipeline_config.dynamicStateEnables.end(), dynamicState);
  return (iter != m_current_pipeline_config.dynamicStateEnabled.end());
}

void GraphicsPipelineLayout::updateGraphicsPipeline(VkCommandBuffer commandBuffer, PipelineConfigInfo& pipelineConfig) {
  if (m_graphics_pipeline) {
    createGraphicsPipeline(pipelineConfig);
    return;
  }

  if (pipelineConfig == m_current_pipeline_config) {
    return;
  }

  const float floatEplision = std::numeric_limits<float>::eplision();

  //TODO: Check to see if extensions are enabled or if Vulkan 1.3 is enabled
  //Viewport and scissor are handled outside of this class
  bool areBlendConstantsEqual = true;
  const unsigned blendConstantCount = sizeof(m_current_pipeline_info.colorBlendAttachment.blendConstants);
  for (unsigned i = 0; i < blendConstantCount; i++){
    areBlendConstantEqual &= (std::abs(m_current_pipeline_info.colorBlendAttachment.blendConstants[i] -
                              pipelineConfig.colorBlendAttachment.blendConstants[i]) < floatEplision);
  }

  if (IsDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_BLEND_CONSTANTS) && !areBlendConstantsEqual) {
    vkCmdSetBlendConstants(commandBuffer, pipelineConfig.colorBlendAttachment.blendConstant);
  }


  bool areMinDepthBoundEqual = std::abs(currentPipelineConfig.depthStencilInfo.minDepthInfo -
               pipelineConfig.depthStencilInfo.minDepthInfo) < floatEplision;
  bool areMaxDepthBoundEqual =
      std::abs(currentPipelineConfig.depthStencilInfo.maxDepthInfo -
               pipelineConfig.depthStencilInfo.maxDepthInfo) < floatEplision;
  if (IsDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_DEPTH_BOUNDS) &&
      (areMinDepthBoundEqual && areMaxDepthBoundEqual)) {
    vkCmdSetDepthBounds(commandBuffer, pipelineConfig.depthStencilInfo.minDepthBound,
                        pipelineConfig.depthStencilInfo.maxDepthBound);
  }

  if (IsDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)){
    if ((pipelineConfig.depthStencil.front.compareOp ==
         pipelineConfig.depthStencil.front.compareOp) &&
        (pipelineConfig.depthStencil.front.compareMask ==
         pipelineConfig.depthStencil.front.compareMask)) {
      vkCmdSetCompareMask(commandBuffer, pipelineConfig.depthStencil.front.compareOp,
                          pipelineConfig.depthStencil.front.compareMask);
    }
    if ((pipelineConfig.depthStencil.back.compareOp ==
        pipelineConfig.depthStencil.back.compareOp) &&
        (pipelineConfig.depthStencil.back.compareMask ==
         pipelineConfig.depthStencil.back.compareMask)) {
        vkCmdSetCompareMask(commandBuffer, pipelineConfig.depthStencil.back.compareOp,
                            pipelineConfig.depthStencil.back.compareMask);
      }
  }

  if (IsDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) {
    if ((pipelineConfig.depthStencil.front.compareOp ==
         pipelineConfig.depthStencil.front.compareOp) &&
        (pipelineConfig.depthStencil.front.compareMask ==
         pipelineConfig.depthStencil.front.compareMask)) {
      vkCmdSetStencilWriteMask(commandBuffer, pipelineConfig.depthStencil.front.compareOp,
                               pipelineConfig.depthStencil.front.stencilMask);
    }
    if ((pipelineConfig.depthStencil.back.compareOp ==
         pipelineConfig.depthStencil.back.compareOp) &&
        (pipelineConfig.depthStencil.back.compareMask ==
         pipelineConfig.depthStencil.back.compareMask)) {
      vkCmdSetStencilWriteMask(commandBuffer, pipelineConfig.depthStencil.back.compareOp,
                               pipelineConfig.depthStencil.back.stencilMask);
    }
  }

  if (IsDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_STENCIL_REFERENCE)) {
    if ((pipelineConfig.depthStencil.front.compareOp ==
         pipelineConfig.depthStencil.front.compareOp) &&
        (pipelineConfig.depthStencil.front.compareMask ==
         pipelineConfig.depthStencil.front.compareMask)) {
      vkCmdSetStencilReference(commandBuffer, pipelineConfig.depthStencil.front.compareOp,
                               pipelineConfig.depthStencil.front.stencilMask);
    }
    if ((pipelineConfig.depthStencil.back.compareOp ==
         pipelineConfig.depthStencil.back.compareOp) &&
        (pipelineConfig.depthStencil.back.compareMask ==
         pipelineConfig.depthStencil.back.compareMask)) {
      vkCmdSetStencilReference(commandBuffer, pipelineConfig.depthStencil.back.compareOp,
                               pipelineConfig.depthStencil.back.stencilMask);
    }
  }
  m_current_pipeline_config = pipelineConfig;
}

void GraphicsPipelineLayout::createGraphicsPipeline(PipelineConfigInfo& configInfo) {
  destroyPipeline();

  assert(configInfo.pipelineLayout != VK_NULL_HANDLE &&
         "Cannot create graphics pipeline: no pipelineLayout provided in configInfo");
  assert(configInfo.renderPass != VK_NULL_HANDLE &&
         "Cannot create graphics pipeline: no renderPass provided in configInfo");

  auto& bindingDescriptions = configInfo.bindingDescriptions;
  auto& attributeDescriptions = configInfo.attributeDescriptions;
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
  vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = configInfo.shaderStages.data();
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &configInfo.inputAssemblyInfo;
  pipelineInfo.pViewportState = &configInfo.viewportInfo;

  if (!m_device->getPhysicalDeviceFeatures().depthClamp) {
    configInfo.rasterizationInfo.depthClampEnable = VK_FALSE;
  }
  pipelineInfo.pRasterizationState = &configInfo.rasterizationInfo;

  pipelineInfo.pMultisampleState = &configInfo.multisampleInfo;
  pipelineInfo.pColorBlendState = &configInfo.colorBlendInfo;
  pipelineInfo.pDepthStencilState = &configInfo.depthStencilInfo;
  pipelineInfo.pDynamicState = &configInfo.dynamicStateInfo;

  pipelineInfo.layout = configInfo.pipelineLayout;
  pipelineInfo.renderPass = configInfo.renderPass;
  pipelineInfo.subpass = configInfo.subpass;

  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(_device->getLogicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                &_graphicsPipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline");
  }
  m_current_pipeline_config = configInfo;
}

void GraphicsPipelineLayout::bind(VkCommandBuffer commandBuffer) {
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipeline);
}

void GraphicsPipelineLayout::defaultPipelineConfigInfo(PipelineConfigInfo& configInfo) {
  configInfo.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

  configInfo.viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  configInfo.viewportInfo.viewportCount = 1;
  configInfo.viewportInfo.pViewports = nullptr;
  configInfo.viewportInfo.scissorCount = 1;
  configInfo.viewportInfo.pScissors = nullptr;

  configInfo.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  configInfo.rasterizationInfo.depthClampEnable = VK_FALSE;
  configInfo.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
  configInfo.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  configInfo.rasterizationInfo.lineWidth = 1.0f;
  configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
  configInfo.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  configInfo.rasterizationInfo.depthBiasEnable = VK_FALSE;
  configInfo.rasterizationInfo.depthBiasConstantFactor = 0.0f;  // Optional
  configInfo.rasterizationInfo.depthBiasClamp = 0.0f;           // Optional
  configInfo.rasterizationInfo.depthBiasSlopeFactor = 0.0f;     // Optional

  configInfo.multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  configInfo.multisampleInfo.sampleShadingEnable = VK_FALSE;
  configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  configInfo.multisampleInfo.minSampleShading = 0.0f;           // Optional
  configInfo.multisampleInfo.pSampleMask = nullptr;             // Optional
  configInfo.multisampleInfo.alphaToCoverageEnable = VK_FALSE;  // Optional
  configInfo.multisampleInfo.alphaToOneEnable = VK_FALSE;       // Optional

  configInfo.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  configInfo.colorBlendAttachment.blendEnable = VK_FALSE;
  configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
  configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
  configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
  configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
  configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
  configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional

  configInfo.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;
  configInfo.colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;  // Optional
  configInfo.colorBlendInfo.attachmentCount = 1;
  configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;
  configInfo.colorBlendInfo.blendConstants[0] = 0.0f;  // Optional
  configInfo.colorBlendInfo.blendConstants[1] = 0.0f;  // Optional
  configInfo.colorBlendInfo.blendConstants[2] = 0.0f;  // Optional
  configInfo.colorBlendInfo.blendConstants[3] = 0.0f;  // Optional

  configInfo.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  configInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
  configInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
  configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  configInfo.depthStencilInfo.minDepthBounds = 0.0f;  // Optional
  configInfo.depthStencilInfo.maxDepthBounds = 1.0f;  // Optional
  configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
  configInfo.depthStencilInfo.front = {};  // Optional
  configInfo.depthStencilInfo.back = {};   // Optional

  configInfo.dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
  configInfo.dynamicStateInfo.dynamicStateCount =
      static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
  configInfo.dynamicStateInfo.flags = 0;
}

