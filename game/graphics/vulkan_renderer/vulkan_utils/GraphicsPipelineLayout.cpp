#include "GraphicsPipelineLayout.h"

#include <cmath>
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
  if (_graphicsPipeline) {
    vkDestroyPipeline(_device->getLogicalDevice(), _graphicsPipeline, nullptr);
    _graphicsPipeline = nullptr;
  }
}

namespace vk_settings {
bool areVkInputBindingDescriptionsEqual(const VkVertexInputBindingDescription& lhs,
                                        const VkVertexInputBindingDescription& rhs) {
  bool status = true;
  status &= lhs.binding == lhs.binding;
  status &= lhs.stride == lhs.stride;
  status &= lhs.inputRate == lhs.inputRate;
  return status;
}

bool areVkInputAttributeDescriptionsEqual(const VkVertexInputAttributeDescription& lhs,
                                          const VkVertexInputAttributeDescription& rhs) {
  bool status = true;
  status &= lhs.location == rhs.location;
  status &= lhs.binding == rhs.binding;
  status &= lhs.format == rhs.format;
  status &= lhs.offset == rhs.offset;
  return status;
}

bool areVkPipelineShaderStageCreateInfosEqual(const VkPipelineShaderStageCreateInfo& lhs,
                                              const VkPipelineShaderStageCreateInfo& rhs) {
  bool status = true;
  status &= lhs.sType == rhs.sType;
  status &= lhs.flags == rhs.flags;
  status &= lhs.stage == rhs.stage;

  return status;
}

bool areVkPipelineViewportStateCreateInfosEqual(const VkPipelineViewportStateCreateInfo& lhs,
                                                const VkPipelineViewportStateCreateInfo& rhs) {
  bool status = true;
  status &= lhs.sType == rhs.sType;
  status &= lhs.flags == rhs.flags;
  if (lhs.viewportCount != rhs.viewportCount) {
    return false;
  }
  for (unsigned int i = 0; i < lhs.viewportCount; ++i) {
    const double delta = 1e-5;

    status &= std::abs(lhs.pViewports[i].x - rhs.pViewports[i].x) < delta;
    status &= std::abs(lhs.pViewports[i].y - rhs.pViewports[i].y) < delta;
    status &= std::abs(lhs.pViewports[i].width - rhs.pViewports[i].width) < delta;
    status &= std::abs(lhs.pViewports[i].height - rhs.pViewports[i].height) <delta;
    status &= std::abs(lhs.pViewports[i].minDepth - rhs.pViewports[i].minDepth) < delta;
    status &= std::abs(lhs.pViewports[i].maxDepth - rhs.pViewports[i].maxDepth) < delta;
  }
  if (lhs.scissorCount != rhs.scissorCount) {
    return false;
  }
  for (unsigned int i = 0; i < lhs.scissorCount; ++i) {
    status &= lhs.pScissors[i].offset.x == rhs.pScissors[i].offset.x;
    status &= lhs.pScissors[i].offset.y == rhs.pScissors[i].offset.y;

    status &= lhs.pScissors[i].extent.width == rhs.pScissors[i].extent.width;
    status &= lhs.pScissors[i].extent.height == rhs.pScissors[i].extent.height;
  }

  return status;
}

bool areVkPipelineInputAssemblyStateCreateInfosEqual(
    const VkPipelineInputAssemblyStateCreateInfo& lhs,
    const VkPipelineInputAssemblyStateCreateInfo& rhs) {
  bool status = true;
  status &= lhs.flags == rhs.flags;
  status &= lhs.topology == rhs.topology;
  status &= lhs.primitiveRestartEnable == rhs.primitiveRestartEnable;
  return status;
}

bool areVkPipelineRasterizationStateCreateInfosEqual(
    const VkPipelineRasterizationStateCreateInfo& lhs,
    const VkPipelineRasterizationStateCreateInfo& rhs) {
  const double delta = 1e-5;

  bool status = true;
  status &= lhs.flags == rhs.flags;
  status &= lhs.depthClampEnable == rhs.depthClampEnable;
  status &= lhs.rasterizerDiscardEnable == rhs.rasterizerDiscardEnable;
  status &= lhs.polygonMode == rhs.polygonMode;
  status &= lhs.cullMode == rhs.cullMode;
  status &= lhs.frontFace == rhs.frontFace;
  status &= lhs.depthBiasEnable == rhs.depthBiasEnable;
  status &= std::abs(lhs.depthBiasConstantFactor) < delta;
  status &= std::abs(lhs.depthBiasClamp - rhs.depthBiasClamp) < delta;
  status &= std::abs(lhs.depthBiasSlopeFactor - rhs.depthBiasConstantFactor) < delta;
  status &= std::abs(lhs.lineWidth - rhs.lineWidth) < delta;

  return status;
}

bool areVkPipelineMultisampleStateCreateInfosEqual(
    const VkPipelineMultisampleStateCreateInfo& lhs,
    const VkPipelineMultisampleStateCreateInfo& rhs) {
  return false;
}

bool areVkPipelineColorBlendAttachmentStatesEqual(const VkPipelineColorBlendAttachmentState& lhs,
                                                  const VkPipelineColorBlendAttachmentState& rhs) {
  bool status = true;
  status &= lhs.blendEnable == rhs.blendEnable;
  status &= lhs.srcColorBlendFactor == rhs.srcColorBlendFactor;
  status &= lhs.dstColorBlendFactor == rhs.dstColorBlendFactor;
  status &= lhs.colorBlendOp == rhs.colorBlendOp;
  status &= lhs.srcAlphaBlendFactor == rhs.srcAlphaBlendFactor;
  status &= lhs.dstAlphaBlendFactor == rhs.dstAlphaBlendFactor;
  status &= lhs.alphaBlendOp == lhs.alphaBlendOp;
  status &= lhs.colorWriteMask == rhs.colorWriteMask;
  return status;
}

bool areVkPipelineColorBlendStateCreateInfosEqual(const VkPipelineColorBlendStateCreateInfo& lhs,
                                                  const VkPipelineColorBlendStateCreateInfo& rhs) {
  return false;
}

bool areVkPipelineDepthStencilStateCreateInfosEqual(
    const VkPipelineDepthStencilStateCreateInfo& lhs,
    const VkPipelineDepthStencilStateCreateInfo& rhs) {
  const double delta = 1e-5;

  bool status = true;
  status &= lhs.flags == rhs.flags;
  status &= lhs.depthTestEnable == rhs.depthTestEnable;
  status &= lhs.depthWriteEnable == rhs.depthWriteEnable;
  status &= lhs.depthCompareOp == rhs.depthCompareOp;
  status &= lhs.depthBoundsTestEnable == rhs.depthBoundsTestEnable;
  status &= lhs.stencilTestEnable == rhs.stencilTestEnable;

  status &= lhs.front.failOp == rhs.front.failOp;
  status &= lhs.front.passOp == rhs.front.passOp;
  status &= lhs.front.depthFailOp == rhs.front.depthFailOp;
  status &= lhs.front.compareOp == rhs.front.compareOp;
  status &= lhs.front.compareMask == rhs.front.compareMask;
  status &= lhs.front.writeMask == rhs.front.writeMask;
  status &= lhs.front.reference == rhs.front.reference;

  status &= lhs.back.failOp == rhs.back.failOp;
  status &= lhs.back.passOp == rhs.back.passOp;
  status &= lhs.back.depthFailOp == rhs.back.depthFailOp;
  status &= lhs.back.compareOp == rhs.back.compareOp;
  status &= lhs.back.compareMask == rhs.back.compareMask;
  status &= lhs.back.writeMask == rhs.back.writeMask;
  status &= lhs.back.reference == rhs.back.reference;

  status &= std::abs(lhs.minDepthBounds - rhs.minDepthBounds) < delta;
  status &= std::abs(lhs.maxDepthBounds - rhs.maxDepthBounds) < delta;

  return false;
}

bool areVkDynamicStatesEqual(const VkDynamicState& lhs, const VkDynamicState& rhs) {
  return false;
}

bool areVkPipelineDynamicStateCreateInfosEqual(const VkPipelineDynamicStateCreateInfo& lhs,
                                               const VkPipelineDynamicStateCreateInfo& rhs) {
  return false;
}

bool isDynamicStateFeatureEnabled(VkDynamicState dynamicState,
                                  const std::vector<VkDynamicState>& dynamicStateEnables) {
  for (const auto& dynamicStateEnable : dynamicStateEnables) {
    if (dynamicStateEnable == dynamicState) {
      return true;
    }
  }
  return false;
}
}  // namespace vk_settings

bool PipelineConfigInfo::operator==(const PipelineConfigInfo& rhs) {
  bool status = true;
  if (bindingDescriptions.size() != rhs.bindingDescriptions.size()) {
    return false;
  }
  for (size_t i = 0; i < bindingDescriptions.size(); ++i) {
    if (!vk_settings::areVkInputBindingDescriptionsEqual(bindingDescriptions[i],
                                                         rhs.bindingDescriptions[i])) {
      return false;
    }
  }

  if (attributeDescriptions.size() != rhs.attributeDescriptions.size()) {
    return false;
  }
  for (size_t i = 0; i < bindingDescriptions.size(); ++i) {
    if (!vk_settings::areVkInputAttributeDescriptionsEqual(attributeDescriptions[i],
                                                           rhs.attributeDescriptions[i])) {
      return false;
    }
  }

  if (shaderStages.size() != rhs.shaderStages.size()) {
    return false;
  }
  for (size_t i = 0; i < bindingDescriptions.size(); ++i) {
    if (!vk_settings::areVkPipelineShaderStageCreateInfosEqual(shaderStages[i],
                                                     rhs.shaderStages[i])) {
      return false;
    }
  }

  status &= vk_settings::areVkPipelineViewportStateCreateInfosEqual(viewportInfo, rhs.viewportInfo);
  status &= vk_settings::areVkPipelineInputAssemblyStateCreateInfosEqual(inputAssemblyInfo, rhs.inputAssemblyInfo);
  status &= vk_settings::areVkPipelineRasterizationStateCreateInfosEqual(rasterizationInfo, rhs.rasterizationInfo);
  status &=
      vk_settings::areVkPipelineMultisampleStateCreateInfosEqual(multisampleInfo, rhs.multisampleInfo);
  status &= vk_settings::areVkPipelineColorBlendAttachmentStatesEqual(colorBlendAttachment,
                                                                rhs.colorBlendAttachment);
  status &=
      vk_settings::areVkPipelineColorBlendStateCreateInfosEqual(colorBlendInfo, rhs.colorBlendInfo);
  status &= vk_settings::areVkPipelineDepthStencilStateCreateInfosEqual(depthStencilInfo, rhs.depthStencilInfo);
  //status &= vk_settings::areVkDynamicStatesEqual(dynamicStateEnables, rhs.dynamicStateEnables);
  status &=
      vk_settings::areVkPipelineDynamicStateCreateInfosEqual(dynamicStateInfo, rhs.dynamicStateInfo);
  return status;
}


void GraphicsPipelineLayout::updateGraphicsPipeline(VkCommandBuffer commandBuffer, PipelineConfigInfo& pipelineConfig) {
  if (_graphicsPipeline) {
    createGraphicsPipeline(pipelineConfig);
    return;
  }

  if (pipelineConfig == _currentPipelineConfig) {
    return;
  }

  const float floatEplision = std::numeric_limits<float>::epsilon();

  //TODO: Check to see if extensions are enabled or if Vulkan 1.3 is enabled
  //Viewport and scissor are handled outside of this class
  bool areBlendConstantsEqual = true;
  const unsigned blendConstantCount = sizeof(_currentPipelineConfig.colorBlendInfo.blendConstants);
  for (unsigned i = 0; i < blendConstantCount; i++){
    areBlendConstantsEqual &= (std::abs(_currentPipelineConfig.colorBlendInfo.blendConstants[i] -
                              pipelineConfig.colorBlendInfo.blendConstants[i]) < floatEplision);
  }

  if (vk_settings::isDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                                                _currentPipelineConfig.dynamicStateEnables) &&
      !areBlendConstantsEqual) {
    vkCmdSetBlendConstants(commandBuffer, pipelineConfig.colorBlendInfo.blendConstants);
  }


  bool areMinDepthBoundEqual = std::abs(_currentPipelineConfig.depthStencilInfo.minDepthBounds -
               pipelineConfig.depthStencilInfo.minDepthBounds) < floatEplision;
  bool areMaxDepthBoundEqual =
      std::abs(_currentPipelineConfig.depthStencilInfo.maxDepthBounds -
               pipelineConfig.depthStencilInfo.maxDepthBounds) < floatEplision;
  if (vk_settings::isDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_DEPTH_BOUNDS,
                                                _currentPipelineConfig.dynamicStateEnables) &&
      (areMinDepthBoundEqual && areMaxDepthBoundEqual)) {
    vkCmdSetDepthBounds(commandBuffer, pipelineConfig.depthStencilInfo.minDepthBounds,
                        pipelineConfig.depthStencilInfo.maxDepthBounds);
  }

  if (vk_settings::isDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                                _currentPipelineConfig.dynamicStateEnables)) {
    if ((_currentPipelineConfig.depthStencilInfo.front.compareOp ==
         pipelineConfig.depthStencilInfo.front.compareOp) &&
        (_currentPipelineConfig.depthStencilInfo.front.compareMask ==
         pipelineConfig.depthStencilInfo.front.compareMask)) {
      vkCmdSetStencilCompareMask(commandBuffer, pipelineConfig.depthStencilInfo.front.compareOp,
                          pipelineConfig.depthStencilInfo.front.compareMask);
    }
    if ((_currentPipelineConfig.depthStencilInfo.back.compareOp ==
        pipelineConfig.depthStencilInfo.back.compareOp) &&
        (_currentPipelineConfig.depthStencilInfo.back.compareMask ==
         pipelineConfig.depthStencilInfo.back.compareMask)) {
      vkCmdSetStencilCompareMask(commandBuffer, pipelineConfig.depthStencilInfo.back.compareOp,
                            pipelineConfig.depthStencilInfo.back.compareMask);
      }
  }

  if (vk_settings::isDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                _currentPipelineConfig.dynamicStateEnables)) {
    if ((_currentPipelineConfig.depthStencilInfo.front.compareOp ==
         pipelineConfig.depthStencilInfo.front.compareOp) &&
        (_currentPipelineConfig.depthStencilInfo.front.compareMask ==
         pipelineConfig.depthStencilInfo.front.compareMask)) {
      vkCmdSetStencilWriteMask(commandBuffer, pipelineConfig.depthStencilInfo.front.compareOp,
                               pipelineConfig.depthStencilInfo.front.writeMask);
    }
    if ((_currentPipelineConfig.depthStencilInfo.back.compareOp ==
         pipelineConfig.depthStencilInfo.back.compareOp) &&
        (_currentPipelineConfig.depthStencilInfo.back.compareMask ==
         pipelineConfig.depthStencilInfo.back.compareMask)) {
      vkCmdSetStencilWriteMask(commandBuffer, pipelineConfig.depthStencilInfo.back.compareOp,
                               pipelineConfig.depthStencilInfo.back.writeMask);
    }
  }

  if (vk_settings::isDynamicStateFeatureEnabled(VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                                _currentPipelineConfig.dynamicStateEnables)) {
    if ((_currentPipelineConfig.depthStencilInfo.front.compareOp ==
         pipelineConfig.depthStencilInfo.front.compareOp) &&
        (_currentPipelineConfig.depthStencilInfo.front.compareMask ==
         pipelineConfig.depthStencilInfo.front.compareMask)) {
      vkCmdSetStencilReference(commandBuffer, pipelineConfig.depthStencilInfo.front.compareOp,
                               pipelineConfig.depthStencilInfo.front.reference);
    }
    if ((_currentPipelineConfig.depthStencilInfo.back.compareOp ==
         pipelineConfig.depthStencilInfo.back.compareOp) &&
        (_currentPipelineConfig.depthStencilInfo.back.compareMask ==
         pipelineConfig.depthStencilInfo.back.compareMask)) {
      vkCmdSetStencilReference(commandBuffer, pipelineConfig.depthStencilInfo.back.compareOp,
                               pipelineConfig.depthStencilInfo.back.reference);
    }
  }
  _currentPipelineConfig = pipelineConfig;
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

  if (!_device->getPhysicalDeviceFeatures().depthClamp) {
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
  _currentPipelineConfig = configInfo;
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

