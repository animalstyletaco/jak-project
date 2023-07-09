#pragma once

#include "SwapChain.h"
#include "DescriptorLayout.h"

#include "third-party/imgui/imgui_impl_vulkan.h"
#include "third-party/imgui/imgui_impl_sdl.h"

class ImguiVulkanHelper {
 public:
  ImguiVulkanHelper(std::unique_ptr<SwapChain>& swapChain);
  ~ImguiVulkanHelper();

  void InitializeNewFrame();
  void Render(uint32_t width, uint32_t height, std::unique_ptr<SwapChain>& swapChain);
  void Shutdown();

  private:
  void recreateGraphicsPipeline(ImGui_ImplVulkan_Data* bd,
                                VkSampleCountFlagBits msaaCount);

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<DescriptorPool> m_descriptor_pool;
  uint64_t m_current_image_index = 0;

  VkSampleCountFlagBits currentMsaa = VK_SAMPLE_COUNT_1_BIT;
  VkPipeline m_pipeline = VK_NULL_HANDLE;

   bool _isActive = true;
};
