#pragma once

#include "SwapChain.h"
#include "DescriptorLayout.h"

#include "third-party/imgui/imgui_impl_vulkan.h"
#include "third-party/imgui/imgui_impl_glfw.h"

class ImguiVulkanHelper {
 public:
  ImguiVulkanHelper(std::unique_ptr<SwapChain>& swap_chain);
  ~ImguiVulkanHelper();

  void InitializeNewFrame();
  void Render();
  void Shutdown();

  private:
   std::unique_ptr<SwapChain>& m_swap_chain;
   std::unique_ptr<DescriptorPool> m_descriptor_pool;
   uint64_t m_current_image_index = 0;

   bool _isActive = true;
};
