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
};

class GraphicsPipelineLayout {
 public:
  GraphicsPipelineLayout(std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~GraphicsPipelineLayout();

  void bind(VkCommandBuffer commandBuffer);
  void createGraphicsPipeline(PipelineConfigInfo& configInfo);
  void destroyPipeline();

  static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkPipeline m_graphics_pipeline = VK_NULL_HANDLE;
};
