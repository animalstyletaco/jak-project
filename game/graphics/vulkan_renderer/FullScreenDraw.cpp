#include "FullScreenDraw.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FullScreenDrawVulkan::FullScreenDrawVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                           VulkanInitializationInfo& vulkan_info)
    : m_device(device), m_vulkan_info(vulkan_info), m_pipeline_layout(device) {

  create_command_buffers();
  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);
  initialize_input_binding_descriptions();
  init_shaders();

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  m_fragment_uniform_buffer =
      std::make_unique<UniformVulkanBuffer>(m_device, sizeof(math::Vector4f), 1, 1);

  std::array<Vertex, 4> vertices = {
      Vertex{-1, -1},
      Vertex{-1, 1},
      Vertex{1, -1},
      Vertex{1, 1},
  };

  VkDeviceSize device_size = sizeof(Vertex) * 4;
  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, device_size, 1, 1);
  m_vertex_buffer->writeToGpuBuffer(vertices.data(), device_size, 0);

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
          .build();

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(1);
  m_fragment_descriptor_writer->build(m_descriptor_sets[0]);

  m_fragment_buffer_descriptor_info = m_fragment_uniform_buffer->descriptorInfo();
  m_fragment_descriptor_writer->writeBuffer(0, &m_fragment_buffer_descriptor_info, 1);
  create_pipeline_layout();
}

void FullScreenDrawVulkan::initialize_input_binding_descriptions() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  VkVertexInputAttributeDescription attributeDescription{};
  // TODO: This value needs to be normalized
  attributeDescription.binding = 0;
  attributeDescription.location = 0;
  attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescription.offset = 0;
  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescription);
}

void FullScreenDrawVulkan::create_command_buffers() {
  commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_device->getCommandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

  if (vkAllocateCommandBuffers(m_device->getLogicalDevice(), &allocInfo, commandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void FullScreenDrawVulkan::init_shaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::SOLID_COLOR];

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

void FullScreenDrawVulkan::create_pipeline_layout() {
  // If push constants are needed put them here

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

void FullScreenDrawVulkan::draw(const math::Vector4f& color,
                                SharedVulkanRenderState* render_state,
                                ScopedProfilerNode& prof) {
  m_fragment_uniform_buffer->SetUniform4f("fragment_color", color[0], color[1], color[2], color[3]);
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();
  
  prof.add_tri(2);
  prof.add_draw_call();

  m_fragment_buffer_descriptor_info = m_fragment_uniform_buffer->descriptorInfo();

  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[0] = m_fragment_descriptor_writer->writeBufferDescriptorSet(
      0, &m_fragment_buffer_descriptor_info, 1);

  m_fragment_descriptor_writer->overwrite(m_descriptor_sets[0]);

  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_layout.createGraphicsPipeline(m_pipeline_config_info);

  // TODO: Check for swap chain error
  auto result = m_vulkan_info.swap_chain->acquireNextImage(&currentImageIndex);

  VkCommandBuffer commandBuffer = commandBuffers[currentImageIndex];

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }

  m_vulkan_info.swap_chain->beginSwapChainRenderPass(commandBuffer, currentImageIndex);
  m_pipeline_layout.bind(commandBuffer);

  m_vulkan_info.swap_chain->drawCommandBuffer(
      commandBuffer, m_vertex_buffer, m_pipeline_config_info.pipelineLayout, m_descriptor_sets);
  // glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  m_vulkan_info.swap_chain->endSwapChainRenderPass(commandBuffer);
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }

  if (m_vulkan_info.swap_chain->submitCommandBuffers(&commandBuffer, &currentImageIndex) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }
  vkQueueWaitIdle(m_device->graphicsQueue());
}

FullScreenDrawVulkan::~FullScreenDrawVulkan() {
  vkFreeCommandBuffers(m_device->getLogicalDevice(), m_device->getCommandPool(),
                       static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
  commandBuffers.clear();
}
