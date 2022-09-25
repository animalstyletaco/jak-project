#pragma once

#include <string>
#include <vector>

#include "GraphicsDeviceVulkan.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

#include "game/graphics/vulkan_renderer/Shader.h"

struct PipelineConfigInfo {
  PipelineConfigInfo() = default;
  PipelineConfigInfo(const PipelineConfigInfo&) = delete;
  PipelineConfigInfo& operator=(const PipelineConfigInfo&) = delete;

  std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
  VkPipelineViewportStateCreateInfo viewportInfo;
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
  VkPipelineRasterizationStateCreateInfo rasterizationInfo;
  VkPipelineMultisampleStateCreateInfo multisampleInfo;
  VkPipelineColorBlendAttachmentState colorBlendAttachment;
  VkPipelineColorBlendStateCreateInfo colorBlendInfo;
  VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
  std::vector<VkDynamicState> dynamicStateEnables;
  VkPipelineDynamicStateCreateInfo dynamicStateInfo;
  VkPipelineLayout pipelineLayout = nullptr;
  VkRenderPass renderPass = nullptr;
  uint32_t subpass = 0;
};

class GraphicsPipelineLayout {
 public:
  GraphicsPipelineLayout(std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~GraphicsPipelineLayout();

  GraphicsPipelineLayout(const GraphicsPipelineLayout&) = delete;
  GraphicsPipelineLayout& operator=(const GraphicsPipelineLayout&) = delete;

  void bind(VkCommandBuffer commandBuffer);
  void createGraphicsPipeline(Shader& shader,
                              const PipelineConfigInfo& configInfo);

  static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkPipeline m_graphics_pipeline = VK_NULL_HANDLE;
};
