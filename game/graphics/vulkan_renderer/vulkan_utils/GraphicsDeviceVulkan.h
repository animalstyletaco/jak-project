#pragma once

#include <array>
#include <optional>
#include <vector>
#include <iostream>
#include <cassert>

#include "common/log/log.h"

#include "third-party/glad/include/vulkan/vulkan.h"

#define SDL_INCLUDE_VULKAN
#include "third-party/SDL/include/SDL.h"

namespace vulkan_utils {
std::string error_string(VkResult result);
}

#define VK_CHECK_RESULT(f, s)                                                                 \
  {                                                                                           \
    VkResult res = (f);                                                                       \
    if (res != VK_SUCCESS) {                                                                  \
      std::cout << "Fatal : VkResult is \"" << vulkan_utils::error_string(res) << "\" in "    \
                << __FILE__ << " at line " << __LINE__ << ", Failure Message: " << s << "\n"; \
                                                                                              \
      assert(res == VK_SUCCESS);                                                              \
    }                                                                                         \
  }


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

  GraphicsDeviceVulkan(SDL_Window* window);
  ~GraphicsDeviceVulkan();

  // Not copyable or movable
  GraphicsDeviceVulkan(const GraphicsDeviceVulkan&) = delete;
  GraphicsDeviceVulkan& operator=(const GraphicsDeviceVulkan&) = delete;
  GraphicsDeviceVulkan(GraphicsDeviceVulkan&&) = delete;
  GraphicsDeviceVulkan& operator=(GraphicsDeviceVulkan&&) = delete;

  VkSampleCountFlagBits getMsaaCount() {
    return m_msaa_samples;
  }
  void setMsaaCount(VkSampleCountFlagBits msaa_count) {
    m_msaa_samples = msaa_count;
  }
  VkCommandPool getCommandPool() {
    return m_command_pool;
  }
  VkDevice getLogicalDevice() {
    return m_device;
  }
  VkPhysicalDevice getPhysicalDevice() {
    return m_physical_device;
  }
  VkSurfaceKHR surface() {
    return m_surface;
  }
  VkQueue graphicsQueue() {
    return m_graphics_queue;
  }
  VkQueue presentQueue() {
    return m_present_queue;
  }
  VkInstance getInstance() {
    return m_instance;
  }
  VkPhysicalDeviceFeatures getPhysicalDeviceFeatures() {
    return m_physical_device_features;
  }
  VkPhysicalDeviceProperties getPhysicalDeviceProperties() {
    return m_physical_device_properties;
  }
  VkPhysicalDeviceLimits getPhysicalDeviceLimits() {
    return m_physical_device_properties.limits;
  }

  uint32_t getMinimumBufferOffsetAlignment() {
    return m_physical_device_properties.limits.minUniformBufferOffsetAlignment;
  }
  float getMaxSamplerAnisotropy() {
    return m_physical_device_properties.limits.maxSamplerAnisotropy;
  }

  SwapChainSupportDetails getSwapChainSupport() {
    return querySwapChainSupport(m_physical_device);
  }
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
  QueueFamilyIndices findPhysicalQueueFamilies() {
    return findQueueFamilies(m_physical_device);
  }
  VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                               VkImageTiling tiling,
                               VkFormatFeatureFlags features);
  uint32_t getNonCoherentAtomSizeMultiple(uint32_t originalOffset);
  uint32_t getMinimumBufferOffsetAlignment(uint32_t originalOffset);
  VkSampleCountFlagBits GetMaxUsableSampleCount();

  VkCommandBuffer allocateCommandBuffers(VkCommandBufferUsageFlags flags);
  void submitCommandsBufferToQueue(std::vector<VkCommandBuffer> commandBuffer);
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer commandBuffer);
  void copyBuffer(VkBuffer srcBuffer,
                  VkBuffer dstBuffer,
                  VkDeviceSize size,
                  VkDeviceSize srcOffset = 0,
                  VkDeviceSize dstOffset = 0);
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
                             VkImageLayout oldLayout,
                             VkImageLayout newLayout,
                             unsigned baseMipLevel = 0,
                             unsigned levelCount = 1);

  VkFormatProperties getPhysicalDeviceFormatProperties(VkFormat format);
  void createRenderPass(const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*);
  void destroyRenderPass(VkRenderPass&, const VkAllocationCallbacks*);
  void createFramebuffer(const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*);
  void destroyFramebuffer(VkFramebuffer&, const VkAllocationCallbacks*);

  void allocateCommandBuffers(const VkCommandBufferAllocateInfo* commandBufferCreateInfo,
                              VkCommandBuffer* commandBuffers);
  void freeCommandBuffers(VkCommandPool commandPool,
                          unsigned commandBufferCount,
                          const VkCommandBuffer* commandBuffers);

  void allocateMemory(const VkMemoryAllocateInfo* alloc_info,
                      const VkAllocationCallbacks* callbacks,
                      VkDeviceMemory* device_memory);
  void mapMemory(VkDeviceMemory device_memory,
                 VkDeviceSize offset,
                 VkDeviceSize size,
                 VkMemoryMapFlags flags,
                 void** mappedMemory);
  void unmapMemory(VkDeviceMemory);
  void freeMemory(VkDeviceMemory& device_memory, const VkAllocationCallbacks* callbacks);

  void createBuffer(const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*);
  void bindBufferMemory(VkBuffer image, VkDeviceMemory device_memory, unsigned flags = 0);
  void destroyBuffer(VkBuffer&, const VkAllocationCallbacks*);

  void createBufferView(const VkBufferViewCreateInfo*, const VkAllocationCallbacks*, VkBufferView*);
  void destroyBufferView(VkBufferView&, const VkAllocationCallbacks*);

  void createImage(const VkImageCreateInfo*,
                   const VkAllocationCallbacks*, VkImage*);
  void bindImageMemory(VkImage image, VkDeviceMemory device_memory, unsigned flags = 0);
  void destroyImage(VkImage&, const VkAllocationCallbacks*);

  void createImageView(const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);
  void destroyImageView(VkImageView&, const VkAllocationCallbacks*);

  void createSampler(const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*);
  void destroySampler(VkSampler&, const VkAllocationCallbacks*);

  void createPipelineLayout(const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*);
  void destroyPipelineLayout(VkPipelineLayout&, const VkAllocationCallbacks*);

  void createGraphicsPipelines(VkPipelineCache pipelineCache,
                               VkDeviceSize graphicsPipelineCreateCount,
                               const VkGraphicsPipelineCreateInfo* createInfo,
                               const VkAllocationCallbacks* callbacks,
                               VkPipeline* pipelines);
  void destroyPipeline(VkPipeline&, const VkAllocationCallbacks*);

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
  uint32_t getMinimumMemoryNeedFor(uint32_t memorySize, uint32_t deviceAttributeMemorySize);

  VkSampleCountFlagBits m_msaa_samples = VK_SAMPLE_COUNT_1_BIT;

  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debug_messenger;
  VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
  VkCommandPool m_command_pool;

  VkDevice m_device = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkQueue m_graphics_queue = VK_NULL_HANDLE;
  VkQueue m_present_queue = VK_NULL_HANDLE;

  SDL_Window* m_window = nullptr;

  const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
  const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                                     VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                                                     VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};

  VkPhysicalDeviceFeatures m_physical_device_features{};
  VkPhysicalDeviceProperties m_physical_device_properties{};
  VkPhysicalDeviceMemoryProperties m_physical_memory_properties{};
};
