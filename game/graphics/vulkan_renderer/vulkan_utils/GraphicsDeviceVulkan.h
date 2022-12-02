#pragma once

#include <array>
#include <optional>

#include "common/log/log.h"

#include "third-party/glad/include/vulkan/vulkan.h"

#define GLFW_INCLUDE_VULKAN
#include "third-party/glfw/include/GLFW/glfw3.h"

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> presentFamily;
  bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

class GraphicsDeviceVulkan {
 public:
#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

  GraphicsDeviceVulkan(GLFWwindow* window);
  ~GraphicsDeviceVulkan();

  // Not copyable or movable
  GraphicsDeviceVulkan(const GraphicsDeviceVulkan&) = delete;
  GraphicsDeviceVulkan& operator=(const GraphicsDeviceVulkan&) = delete;
  GraphicsDeviceVulkan(GraphicsDeviceVulkan&&) = delete;
  GraphicsDeviceVulkan& operator=(GraphicsDeviceVulkan&&) = delete;

  VkSampleCountFlagBits getMsaaCount() { return m_msaa_samples; }
  VkCommandPool getCommandPool() { return m_command_pool; }
  VkDevice getLogicalDevice() { return m_device; }
  VkPhysicalDevice getPhysicalDevice() { return m_physical_device; }
  VkSurfaceKHR surface() { return m_surface; }
  VkQueue graphicsQueue() { return m_graphics_queue; }
  VkQueue presentQueue() { return m_present_queue; }
  VkInstance getInstance() { return m_instance; }

  SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(m_physical_device); }
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
  QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(m_physical_device); }
  VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                               VkImageTiling tiling,
                               VkFormatFeatureFlags features);
  VkSampleCountFlagBits GetMaxUsableSampleCount();

  VkCommandBuffer allocateCommandBuffers(VkCommandBufferUsageFlags flags);
  void submitCommandsBufferToQueue(std::vector<VkCommandBuffer> commandBuffer);
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer commandBuffer);
  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
  void copyBufferToImage(VkBuffer buffer,
                         VkImage image,
                         uint32_t width,
                         uint32_t height,
                         int32_t x_offset,
                         int32_t y_offset,
                         uint32_t layerCount);

  void copyImageToBuffer(VkImage image,
                         uint32_t width,
                         uint32_t height,
                         int32_t x_offset,
                         int32_t y_offset,
                         uint32_t layer_count,
                         VkBuffer buffer);

  void GenerateMipmaps(VkImage image,
                       VkFormat imageFormat,
                       int32_t texWidth,
                       int32_t texHeight,
                       uint32_t mipLevels);

  void transitionImageLayout(VkImage image,
                             VkFormat format,
                             VkImageLayout oldLayout,
                             VkImageLayout newLayout);

  VkPhysicalDeviceProperties properties;

 private:
  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createCommandPool();

  // helper functions
  bool isDeviceSuitable(VkPhysicalDevice device);
  std::vector<const char*> getRequiredExtensions();
  bool checkValidationLayerSupport();
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
  void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
  void hasGflwRequiredInstanceExtensions();
  bool checkDeviceExtensionSupport(VkPhysicalDevice device);
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

  VkSampleCountFlagBits m_msaa_samples = VK_SAMPLE_COUNT_1_BIT;

  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debug_messenger;
  VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
  VkCommandPool m_command_pool;

  VkDevice m_device = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkQueue m_graphics_queue = VK_NULL_HANDLE;
  VkQueue m_present_queue = VK_NULL_HANDLE;

  GLFWwindow* m_window = nullptr;

  const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
  const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
};
