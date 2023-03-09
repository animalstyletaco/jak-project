#include "ImguiVulkanHelper.h"
#include "common/util/FileUtil.h"

ImguiVulkanHelper::ImguiVulkanHelper(std::unique_ptr<SwapChain>& swapChain) : m_device(swapChain->getLogicalDevice()){

  // May be overkill for descriptor pool
  std::vector<VkDescriptorPoolSize> pool_sizes = {
    {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100}
  };

  uint32_t max_sets = 0;
  for (auto& pool_size : pool_sizes) {
    max_sets += pool_size.descriptorCount;
  }
  m_descriptor_pool = std::make_unique<DescriptorPool>(swapChain->getLogicalDevice(), max_sets, 0, pool_sizes);

    // set up the renderer
  ImGui_ImplVulkan_InitInfo imgui_vulkan_info = {};
  imgui_vulkan_info.Instance = swapChain->getLogicalDevice()->getInstance();
  imgui_vulkan_info.PhysicalDevice = swapChain->getLogicalDevice()->getPhysicalDevice();
  imgui_vulkan_info.Device = swapChain->getLogicalDevice()->getLogicalDevice();
  imgui_vulkan_info.QueueFamily =
      swapChain->getLogicalDevice()->findPhysicalQueueFamilies().presentFamily.value();
  imgui_vulkan_info.Queue = swapChain->getLogicalDevice()->presentQueue();
  imgui_vulkan_info.PipelineCache = NULL;
  imgui_vulkan_info.DescriptorPool = m_descriptor_pool->getDescriptorPool();
  imgui_vulkan_info.Subpass = 0;
  imgui_vulkan_info.MinImageCount = 2;  // Minimum image count need for initialization
  imgui_vulkan_info.ImageCount = 2;
  imgui_vulkan_info.MSAASamples = currentMsaa;  // No need to do multi sampling for debug menu
  imgui_vulkan_info.Allocator = NULL;
  imgui_vulkan_info.CheckVkResultFn = NULL;

  ImGui_ImplVulkan_Init(&imgui_vulkan_info, swapChain->getRenderPass());

      // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple
  // fonts and use ImGui::PushFont()/PopFont() to select them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the
  // font among multiple.
  // - If the file cannot be loaded, the function will return NULL. Please handle those errors in
  // your application (e.g. use an assertion, or display an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when
  // calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
  // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality
  // font rendering.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to
  // write a double backslash \\ !
  // io.Fonts->AddFontDefault();
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL,
  // io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != NULL);

  // Upload Fonts
  {
    // Use any command queue
    //VkCommandPool commandPool = m_swap_chain->getLogicalDevice()->getCommandPool();
    //vkResetCommandPool(m_swap_chain->getLogicalDevice()->getLogicalDevice(), commandPool, 0);
    VkCommandBuffer commandBuffer = swapChain->getLogicalDevice()->beginSingleTimeCommands();

    ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);

    swapChain->getLogicalDevice()->endSingleTimeCommands(commandBuffer);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }
}

void ImguiVulkanHelper::InitializeNewFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImguiVulkanHelper::Render(uint32_t width, uint32_t height, std::unique_ptr<SwapChain>& swapChain) {
  ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData;
  bd->RenderPass = swapChain->getRenderPass();

  if (swapChain->getLogicalDevice()->getMsaaCount() != currentMsaa) {
    recreateGraphicsPipeline(bd, swapChain->getLogicalDevice()->getMsaaCount());
    currentMsaa = swapChain->getLogicalDevice()->getMsaaCount();
  }

  ImGui::Render();
  VkCommandBuffer commandBuffer = swapChain->getLogicalDevice()->beginSingleTimeCommands();
  swapChain->beginSwapChainRenderPass(commandBuffer, m_current_image_index++ % swapChain->imageCount());
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer, m_pipeline);
  swapChain->endSwapChainRenderPass(commandBuffer);
  swapChain->getLogicalDevice()->endSingleTimeCommands(commandBuffer);
}

void ImguiVulkanHelper::recreateGraphicsPipeline(ImGui_ImplVulkan_Data* bd, VkSampleCountFlagBits msaaCount) {
  VkPipelineShaderStageCreateInfo stage[2] = {};
  stage[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stage[0].module = bd->ShaderModuleVert;
  stage[0].pName = "main";
  stage[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stage[1].module = bd->ShaderModuleFrag;
  stage[1].pName = "main";

  VkVertexInputBindingDescription binding_desc[1] = {};
  binding_desc[0].stride = sizeof(ImDrawVert);
  binding_desc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribute_desc[3] = {};
  attribute_desc[0].location = 0;
  attribute_desc[0].binding = binding_desc[0].binding;
  attribute_desc[0].format = VK_FORMAT_R32G32_SFLOAT;
  attribute_desc[0].offset = IM_OFFSETOF(ImDrawVert, pos);
  attribute_desc[1].location = 1;
  attribute_desc[1].binding = binding_desc[0].binding;
  attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
  attribute_desc[1].offset = IM_OFFSETOF(ImDrawVert, uv);
  attribute_desc[2].location = 2;
  attribute_desc[2].binding = binding_desc[0].binding;
  attribute_desc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  attribute_desc[2].offset = IM_OFFSETOF(ImDrawVert, col);

  VkPipelineVertexInputStateCreateInfo vertex_info = {};
  vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_info.vertexBindingDescriptionCount = 1;
  vertex_info.pVertexBindingDescriptions = binding_desc;
  vertex_info.vertexAttributeDescriptionCount = 3;
  vertex_info.pVertexAttributeDescriptions = attribute_desc;

  VkPipelineInputAssemblyStateCreateInfo ia_info = {};
  ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_info = {};
  viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_info.viewportCount = 1;
  viewport_info.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster_info = {};
  raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster_info.polygonMode = VK_POLYGON_MODE_FILL;
  raster_info.cullMode = VK_CULL_MODE_NONE;
  raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster_info.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms_info = {};
  ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms_info.rasterizationSamples = msaaCount;

  VkPipelineColorBlendAttachmentState color_attachment[1] = {};
  color_attachment[0].blendEnable = VK_TRUE;
  color_attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
  color_attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
  color_attachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_info = {};
  depth_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

  VkPipelineColorBlendStateCreateInfo blend_info = {};
  blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend_info.attachmentCount = 1;
  blend_info.pAttachments = color_attachment;

  VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = (uint32_t)IM_ARRAYSIZE(dynamic_states);
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.flags = bd->PipelineCreateFlags;
  info.stageCount = 2;
  info.pStages = stage;
  info.pVertexInputState = &vertex_info;
  info.pInputAssemblyState = &ia_info;
  info.pViewportState = &viewport_info;
  info.pRasterizationState = &raster_info;
  info.pMultisampleState = &ms_info;
  info.pDepthStencilState = &depth_info;
  info.pColorBlendState = &blend_info;
  info.pDynamicState = &dynamic_state;
  info.layout = bd->PipelineLayout;
  info.renderPass = bd->RenderPass;
  info.subpass = 0;
  VkResult err = vkCreateGraphicsPipelines(m_device->getLogicalDevice(), nullptr, 1, &info, nullptr,
                                           &m_pipeline);
}

void ImguiVulkanHelper::Shutdown() {
  _isActive = false;
  if (m_pipeline) {
    vkDestroyPipeline(m_device->getLogicalDevice(), m_pipeline, nullptr);
  }
}

ImguiVulkanHelper::~ImguiVulkanHelper() {
  if (_isActive) {
    Shutdown();
  }
}
