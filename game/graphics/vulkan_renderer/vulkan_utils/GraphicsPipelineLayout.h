#pragma once

#include <string>
#include <vector>

#include "GraphicsDeviceVulkan.h"

struct PipelineConfigInfo {
  std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages{};
  VkPipelineViewportStateCreateInfo viewportInfo{};
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
  VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
  VkPipelineMultisampleStateCreateInfo multisampleInfo{};
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
  VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
  std::vector<VkDynamicState> dynamicStateEnables{};
  VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
  VkPipelineLayout pipelineLayout = nullptr;
  VkRenderPass renderPass = nullptr;
  uint32_t subpass = 0;

  bool operator==(const PipelineConfigInfo& rhs);
};

class GraphicsPipelineLayout {
 public:
  GraphicsPipelineLayout(std::shared_ptr<GraphicsDeviceVulkan> device);
  ~GraphicsPipelineLayout();

  void bind(VkCommandBuffer commandBuffer);
  void updateGraphicsPipeline(VkCommandBuffer, PipelineConfigInfo& configInfo);
  PipelineConfigInfo GetCurrentConfiguration() { return _currentPipelineConfig; };
  void destroyPipeline();

  static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);

 private:
  void createGraphicsPipeline(PipelineConfigInfo& configInfo);

  std::shared_ptr<GraphicsDeviceVulkan> _device;
  PipelineConfigInfo _currentPipelineConfig{};
  VkPipeline _graphicsPipeline = VK_NULL_HANDLE;
};
