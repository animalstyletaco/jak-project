#pragma once

#include "SwapChain.h"
#include "DescriptorLayout.h"

#include "third-party/imgui/imgui_impl_vulkan.h"
#include "third-party/imgui/imgui_impl_glfw.h"

class ImguiVulkanHelper {
 public:
  ImguiVulkanHelper(std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~ImguiVulkanHelper();

  void InitializeNewFrame();
  void Render(uint32_t width, uint32_t height);
  void Shutdown();
  void RecreateSwapChain();

  private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<DescriptorPool> m_descriptor_pool;
  uint64_t m_current_image_index = 0;

  std::shared_ptr<SwapChain> m_swap_chain;
  VkExtent2D m_extents = {640, 480};

   bool _isActive = true;
};
