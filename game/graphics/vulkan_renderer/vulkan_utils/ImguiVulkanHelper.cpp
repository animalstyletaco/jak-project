#include "ImguiVulkanHelper.h"
#include "common/util/FileUtil.h"

ImguiVulkanHelper::ImguiVulkanHelper(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device) {

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
  m_descriptor_pool = std::make_unique<DescriptorPool>(m_device, max_sets, 0, pool_sizes);
  RecreateSwapChain();

    // set up the renderer
  ImGui_ImplVulkan_InitInfo imgui_vulkan_info = {};
  imgui_vulkan_info.Instance = m_swap_chain->getLogicalDevice()->getInstance();
  imgui_vulkan_info.PhysicalDevice = m_swap_chain->getLogicalDevice()->getPhysicalDevice();
  imgui_vulkan_info.Device = m_swap_chain->getLogicalDevice()->getLogicalDevice();
  imgui_vulkan_info.QueueFamily = m_swap_chain->getLogicalDevice()->findPhysicalQueueFamilies().presentFamily.value();
  imgui_vulkan_info.Queue = m_swap_chain->getLogicalDevice()->presentQueue();
  imgui_vulkan_info.PipelineCache = NULL;
  imgui_vulkan_info.DescriptorPool = m_descriptor_pool->getDescriptorPool();
  imgui_vulkan_info.Subpass = 0;
  imgui_vulkan_info.MinImageCount = 2;  // Minimum image count need for initialization
  imgui_vulkan_info.ImageCount = 2;
  imgui_vulkan_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT; //No need to do multi sampling for debug menu
  imgui_vulkan_info.Allocator = NULL;
  imgui_vulkan_info.CheckVkResultFn = NULL;

  ImGui_ImplVulkan_Init(&imgui_vulkan_info, m_swap_chain->getRenderPass());

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
    VkCommandBuffer commandBuffer = m_swap_chain->getLogicalDevice()->beginSingleTimeCommands();

    ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);

    m_swap_chain->getLogicalDevice()->endSingleTimeCommands(commandBuffer);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }
}

void ImguiVulkanHelper::RecreateSwapChain() {
  while (m_extents.width == 0 || m_extents.height == 0) {
    glfwWaitEvents();
  }
  vkDeviceWaitIdle(m_device->getLogicalDevice());

  if (m_swap_chain == nullptr) {
    m_swap_chain = std::make_unique<SwapChain>(m_device, m_extents);
  } else {
    std::shared_ptr<SwapChain> oldSwapChain = std::move(m_swap_chain);
    m_swap_chain = std::make_unique<SwapChain>(m_device, m_extents, oldSwapChain);

    if (!oldSwapChain->compareSwapFormats(*m_swap_chain.get())) {
      throw std::runtime_error("Swap chain image(or depth) format has changed!");
    }
  }
}

void ImguiVulkanHelper::InitializeNewFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImguiVulkanHelper::Render(uint32_t width, uint32_t height) {
  if (m_extents.width != width || m_extents.height != height) {
    m_extents = {width, height};
    RecreateSwapChain();

    ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData;
    bd->RenderPass = m_swap_chain->getRenderPass();
  }

  ImGui::Render();
  VkCommandBuffer commandBuffer = m_swap_chain->getLogicalDevice()->beginSingleTimeCommands();
  m_swap_chain->beginSwapChainRenderPass(commandBuffer, m_current_image_index++ % m_swap_chain->imageCount());
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer, NULL);
  m_swap_chain->endSwapChainRenderPass(commandBuffer);
  m_swap_chain->getLogicalDevice()->endSingleTimeCommands(commandBuffer);
}

void ImguiVulkanHelper::Shutdown() {
  _isActive = false;
}

ImguiVulkanHelper::~ImguiVulkanHelper() {
  if (_isActive) {
    Shutdown();
  }
}
