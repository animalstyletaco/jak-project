#include "GraphicsDeviceVulkan.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <cassert>
#include <mutex>

#include "third-party/SDL/include/SDL_vulkan.h"

namespace vulkan_device {
static std::mutex device_mutex;
static std::mutex physical_device_mutex;
static std::mutex queue_mutex;

static bool is_vulkan_loaded = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData) {
  if (strcmp(pCallbackData->pMessageIdName, "Loader Message")) {
    lg::error("{}: {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
  } else {
    lg::info("{}: {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
  }
  return VK_FALSE;
}
}  // namespace vulkan_device

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

std::string vulkan_utils::error_string(VkResult errorCode) {
  switch (errorCode) {
#define STR(r) \
  case VK_##r: \
    return #r
    STR(NOT_READY);
    STR(TIMEOUT);
    STR(EVENT_SET);
    STR(EVENT_RESET);
    STR(INCOMPLETE);
    STR(ERROR_OUT_OF_HOST_MEMORY);
    STR(ERROR_OUT_OF_DEVICE_MEMORY);
    STR(ERROR_INITIALIZATION_FAILED);
    STR(ERROR_DEVICE_LOST);
    STR(ERROR_MEMORY_MAP_FAILED);
    STR(ERROR_LAYER_NOT_PRESENT);
    STR(ERROR_EXTENSION_NOT_PRESENT);
    STR(ERROR_FEATURE_NOT_PRESENT);
    STR(ERROR_INCOMPATIBLE_DRIVER);
    STR(ERROR_TOO_MANY_OBJECTS);
    STR(ERROR_FORMAT_NOT_SUPPORTED);
    STR(ERROR_SURFACE_LOST_KHR);
    STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
    STR(SUBOPTIMAL_KHR);
    STR(ERROR_OUT_OF_DATE_KHR);
    STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
    STR(ERROR_VALIDATION_FAILED_EXT);
    STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
      return "UNKNOWN_ERROR";
  }
}

GraphicsDeviceVulkan::GraphicsDeviceVulkan(SDL_Window* window) : m_window(window) {
  if (!vulkan_device::is_vulkan_loaded) {
    gladLoaderLoadVulkan(nullptr, nullptr, nullptr);  // Initial load to get vulkan function loaded
  }

  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createCommandPool();
}

GraphicsDeviceVulkan::~GraphicsDeviceVulkan() {
  vkDestroyCommandPool(m_device, m_command_pool, nullptr);
  vkDestroyDevice(m_device, nullptr);

  if (enableValidationLayers) {
    DestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
  }

  vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
  vkDestroyInstance(m_instance, nullptr);
  gladLoaderUnloadVulkan();
}

void GraphicsDeviceVulkan::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    lg::error("validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "OpenGOAL";
  appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 2, 0);
  appInfo.pEngineName = "OpenGOAL";
  appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 2, 0);
  appInfo.apiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0);

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;

    createInfo.pNext = nullptr;
  }

  if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
    lg::error("failed to create instance!");
  }
}

void GraphicsDeviceVulkan::setupDebugMessenger() {
  if (!enableValidationLayers)
    return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(createInfo);

  if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debug_messenger) !=
      VK_SUCCESS) {
    lg::error("failed to set up debug messenger!");
  }
}

void GraphicsDeviceVulkan::populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = vulkan_device::debugCallback;
}

void GraphicsDeviceVulkan::createSurface() {
  if (SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface) == SDL_FALSE) {
    throw new std::runtime_error("failed to create window surface!");
  }
}

void GraphicsDeviceVulkan::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    lg::error("failed to find GPUs with Vulkan support!");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

  for (const auto& device : devices) {
    if (isDeviceSuitable(device)) {
      m_physical_device = device;
      break;
    }
  }

  if (m_physical_device == VK_NULL_HANDLE) {
    lg::error("failed to find a suitable GPU!");
  }

  vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_physical_memory_properties);
  vkGetPhysicalDeviceProperties(m_physical_device, &m_physical_device_properties);
}

void GraphicsDeviceVulkan::createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(m_physical_device);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                            indices.presentFamily.value()};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

  createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
  physicalDeviceDescriptorIndexingFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
  physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
  physicalDeviceDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;

  createInfo.pNext = &physicalDeviceDescriptorIndexingFeatures;
  createInfo.pEnabledFeatures = &m_physical_device_features;

  createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  if (vkCreateDevice(m_physical_device, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
    lg::error("failed to create logical device!");
  }

  if (!vulkan_device::is_vulkan_loaded) {
    if (!gladLoaderLoadVulkan(
            m_instance, m_physical_device,
            m_device)) {  // update loader with new instance, physical device, and logical device
      lg::error("GL init fail");
    }
    vulkan_device::is_vulkan_loaded = true;
  }

  vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphics_queue);
  vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_present_queue);
}

void GraphicsDeviceVulkan::createCommandPool() {
  QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physical_device);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

  if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_command_pool) != VK_SUCCESS) {
    lg::error("failed to create graphics command pool!");
  }
}

VkCommandBuffer GraphicsDeviceVulkan::allocateCommandBuffers(VkCommandBufferUsageFlags flags) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = m_command_pool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  allocateCommandBuffers(&allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = flags;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

VkCommandBuffer GraphicsDeviceVulkan::beginSingleTimeCommands() {
  return allocateCommandBuffers(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void GraphicsDeviceVulkan::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);
  submitCommandsBufferToQueue({commandBuffer});
}

void GraphicsDeviceVulkan::submitCommandsBufferToQueue(std::vector<VkCommandBuffer> commandBuffer) {
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = commandBuffer.size();
  submitInfo.pCommandBuffers = commandBuffer.data();

  vkQueueSubmit(m_graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_graphics_queue);

  freeCommandBuffers(m_command_pool, 1, commandBuffer.data());
}

void GraphicsDeviceVulkan::copyBuffer(VkBuffer srcBuffer,
                                      VkBuffer dstBuffer,
                                      VkDeviceSize size,
                                      VkDeviceSize srcOffset,
                                      VkDeviceSize dstOffset) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  copyRegion.srcOffset = srcOffset;
  copyRegion.dstOffset = dstOffset;

  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

uint32_t GraphicsDeviceVulkan::findMemoryType(uint32_t typeFilter,
                                              VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(m_physical_device, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  lg::error("failed to find suitable memory type!");
  return UINT32_MAX;
}

void GraphicsDeviceVulkan::transitionImageLayout(VkImage image,
                                                 VkImageLayout oldLayout,
                                                 VkImageLayout newLayout,
                                                 unsigned baseMipLevel,
                                                 unsigned levelCount) {
  if (oldLayout == newLayout) {
    return;
  }

  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = baseMipLevel;
  barrier.subresourceRange.levelCount = levelCount;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  endSingleTimeCommands(commandBuffer);
}

void GraphicsDeviceVulkan::copyBufferToImage(VkBuffer buffer,
                                             VkImage image,
                                             uint32_t width,
                                             uint32_t height,
                                             int32_t x_offset,
                                             int32_t y_offset,
                                             uint32_t layer_count) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = layer_count;
  region.imageOffset = {x_offset, y_offset, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);

  endSingleTimeCommands(commandBuffer);
}

void GraphicsDeviceVulkan::copyImageToBuffer(VkImage image,
                                             uint32_t width,
                                             uint32_t height,
                                             int32_t x_offset,
                                             int32_t y_offset,
                                             uint32_t layer_count,
                                             VkBuffer buffer) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = layer_count;
  region.imageOffset = {x_offset, y_offset, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                         &region);

  endSingleTimeCommands(commandBuffer);
}

std::vector<const char*> GraphicsDeviceVulkan::getRequiredExtensions() {
  const char* sdlExtensions[2] = {
      NULL};  // Two extensions are always required for SDL vulkan instances
  uint32_t sdlExtensionCount = sizeof(sdlExtensions) / sizeof(sdlExtensions[0]);
  if (SDL_Vulkan_GetInstanceExtensions(m_window, &sdlExtensionCount, sdlExtensions) == SDL_FALSE) {
    return {};
  }

  std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
  extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

bool GraphicsDeviceVulkan::checkValidationLayerSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char* layerName : validationLayers) {
    bool layerFound = false;

    for (const auto& layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

SwapChainSupportDetails GraphicsDeviceVulkan::querySwapChainSupport(
    VkPhysicalDevice physicalDevice) {
  SwapChainSupportDetails details;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_surface, &details.capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, nullptr);

  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount,
                                         details.formats.data());
  }

  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &presentModeCount, nullptr);

  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &presentModeCount,
                                              details.presentModes.data());
  }

  return details;
}

bool GraphicsDeviceVulkan::isDeviceSuitable(VkPhysicalDevice device) {
  QueueFamilyIndices indices = findQueueFamilies(device);

  bool extensionsSupported = checkDeviceExtensionSupport(device);

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
  }

  vkGetPhysicalDeviceFeatures(device, &m_physical_device_features);

  return indices.isComplete() && extensionsSupported && swapChainAdequate &&
         m_physical_device_features.samplerAnisotropy;
}

bool GraphicsDeviceVulkan::checkDeviceExtensionSupport(VkPhysicalDevice device) {
  uint32_t extensionCount;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

  std::vector<VkExtensionProperties> gpuAvailableExtensions(extensionCount);

  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       gpuAvailableExtensions.data());

  std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

  for (const auto& extension : gpuAvailableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

VkFormat GraphicsDeviceVulkan::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                                   VkImageTiling tiling,
                                                   VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throw std::runtime_error("failed to find supported format!");
}

QueueFamilyIndices GraphicsDeviceVulkan::findQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices{};

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

  int i = 0;
  for (const auto& queueFamily : queueFamilies) {
    if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      indices.graphicsFamily = i;
    }

    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

    if (presentSupport) {
      indices.presentFamily = i;
    }

    if (indices.isComplete()) {
      break;
    }

    i++;
  }

  return indices;
}

void GraphicsDeviceVulkan::GenerateMipmaps(VkImage image,
                                           VkFormat imageFormat,
                                           int32_t texWidth,
                                           int32_t texHeight,
                                           uint32_t mipLevels) {
  // Check if image format supports linear blitting
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(m_physical_device, imageFormat, &formatProperties);

  if (!(formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    throw std::runtime_error("texture image format does not support linear blitting!");
  }

  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  int32_t mipWidth = texWidth;
  int32_t mipHeight = texHeight;

  for (uint32_t i = 1; i < mipLevels; i++) {
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    if (mipWidth > 1)
      mipWidth /= 2;
    if (mipHeight > 1)
      mipHeight /= 2;
  }

  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  endSingleTimeCommands(commandBuffer);
}

VkSampleCountFlagBits GraphicsDeviceVulkan::GetMaxUsableSampleCount() {
  VkPhysicalDeviceProperties physicalDeviceProperties;
  vkGetPhysicalDeviceProperties(m_physical_device, &physicalDeviceProperties);

  VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                              physicalDeviceProperties.limits.framebufferDepthSampleCounts;
  if (counts & VK_SAMPLE_COUNT_64_BIT) {
    return VK_SAMPLE_COUNT_64_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_32_BIT) {
    return VK_SAMPLE_COUNT_32_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_16_BIT) {
    return VK_SAMPLE_COUNT_16_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_8_BIT) {
    return VK_SAMPLE_COUNT_8_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_4_BIT) {
    return VK_SAMPLE_COUNT_4_BIT;
  }
  if (counts & VK_SAMPLE_COUNT_2_BIT) {
    return VK_SAMPLE_COUNT_2_BIT;
  }

  return VK_SAMPLE_COUNT_1_BIT;
}

uint32_t GraphicsDeviceVulkan::getMinimumBufferOffsetAlignment(uint32_t originalOffset) {
  return getMinimumMemoryNeedFor(
      originalOffset, m_physical_device_properties.limits.minUniformBufferOffsetAlignment);
}

uint32_t GraphicsDeviceVulkan::getNonCoherentAtomSizeMultiple(uint32_t originalOffset) {
  return getMinimumMemoryNeedFor(originalOffset,
                                 m_physical_device_properties.limits.nonCoherentAtomSize);
}

uint32_t GraphicsDeviceVulkan::getMinimumMemoryNeedFor(uint32_t memorySize,
                                                       uint32_t deviceAttributeMemorySize) {
  if (memorySize % deviceAttributeMemorySize == 0) {
    return memorySize;
  }

  uint32_t wrap_around_count = (memorySize / deviceAttributeMemorySize) + 1;
  return wrap_around_count * deviceAttributeMemorySize;
}

VkFormatProperties GraphicsDeviceVulkan::getPhysicalDeviceFormatProperties(VkFormat format) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::physical_device_mutex);
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &formatProperties);
  return formatProperties;
}

void GraphicsDeviceVulkan::createSwapChain(const VkSwapchainCreateInfoKHR* createInfo, const VkAllocationCallbacks* callbacks, VkSwapchainKHR* swapChain) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(
      vkCreateSwapchainKHR(m_device, createInfo, callbacks, swapChain),
      "failed to create swap chain");
}

// we only specified a minimum number of images in the swap chain, so the implementation is
// allowed to create a swap chain with more. That's why we'll first query the final number of
// images with vkGetSwapchainImagesKHR, then resize the container and finally call it again to
// retrieve the handles.
void GraphicsDeviceVulkan::getSwapChainImageCount(VkSwapchainKHR swapchain,
                                                 unsigned* imageCount) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkGetSwapchainImagesKHR(m_device, swapchain, imageCount, nullptr),
      "failed to get swapchain image count");
}

void GraphicsDeviceVulkan::getSwapChainImages(VkSwapchainKHR swapchain, unsigned* imageCount, VkImage* swapChainImages) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkGetSwapchainImagesKHR(m_device, swapchain, imageCount,
                                          swapChainImages),
                  "failed to get swap chain images");
}

void GraphicsDeviceVulkan::destroySwapChain(VkSwapchainKHR& swapchain,
                                            const VkAllocationCallbacks* callbacks) {
  if (!swapchain) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroySwapchainKHR(m_device, swapchain, callbacks);
  swapchain = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createRenderPass(const VkRenderPassCreateInfo* createInfo,
                                            const VkAllocationCallbacks* callbacks,
                                            VkRenderPass* renderPass) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateRenderPass(m_device, createInfo, callbacks, renderPass),
                  "Failed to create render pass");
}

void GraphicsDeviceVulkan::destroyRenderPass(VkRenderPass& renderPass, const VkAllocationCallbacks* callbacks) {
  if (!renderPass) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyRenderPass(m_device, renderPass, callbacks);
  renderPass = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createFramebuffer(const VkFramebufferCreateInfo* createInfo,
                                             const VkAllocationCallbacks* callbacks,
                                             VkFramebuffer* framebuffer) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateFramebuffer(m_device, createInfo, callbacks, framebuffer),
                  "Failed to create framebuffers");
}

void GraphicsDeviceVulkan::destroyFramebuffer(VkFramebuffer& framebuffer, const VkAllocationCallbacks* callbacks) {
  if (!framebuffer) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyFramebuffer(m_device, framebuffer, callbacks);
  framebuffer = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::queueSubmit(VkQueue queue,
                                       unsigned submitCount,
                                       const VkSubmitInfo* submitInfo,
                                       VkFence fence) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::queue_mutex);
  VK_CHECK_RESULT(vkQueueSubmit(queue, submitCount, submitInfo, fence),
                  "failed to submit draw command buffer!");
}

void GraphicsDeviceVulkan::submitGraphicsQueue(unsigned submitCount,
                                               const VkSubmitInfo* submitInfo,
                                               VkFence fence) {
  queueSubmit(m_graphics_queue, submitCount, submitInfo, fence);
}
void GraphicsDeviceVulkan::submitPresentQueue(unsigned submitCount,
                                              const VkSubmitInfo* submitInfo,
                                              VkFence fence) {
  queueSubmit(m_present_queue, submitCount, submitInfo, fence);
}

void GraphicsDeviceVulkan::createFences(const VkFenceCreateInfo* fenceInfo,
                                        const VkAllocationCallbacks* callbacks,
                                        VkFence* fences) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateFence(m_device, fenceInfo, callbacks, fences),
                  "Failed to wait for fences");
}

void GraphicsDeviceVulkan::waitForFences(unsigned fenceCount,
                                         VkFence* fences,
                                         VkBool32 waitForAllFences,
                                         unsigned long timeout) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkWaitForFences(m_device, fenceCount, fences, waitForAllFences, timeout),
                  "Failed to wait for fences");
}

void GraphicsDeviceVulkan::resetFences(unsigned fenceCount, VkFence* fences) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkResetFences(m_device, fenceCount, fences), "Failed to reset fences");
}

void GraphicsDeviceVulkan::destroyFence(VkFence fence,
                                        const VkAllocationCallbacks* callbacks) {
  if (!fence) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyFence(m_device, fence, callbacks);
}

void GraphicsDeviceVulkan::createSemaphore(const VkSemaphoreCreateInfo* createInfo,
                                           const VkAllocationCallbacks* callbacks,
                                           VkSemaphore* semaphores) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, createInfo, callbacks, semaphores),
                  "Failed to create semaphores");
}

void GraphicsDeviceVulkan::destroySemaphore(VkSemaphore semaphore,
                                            const VkAllocationCallbacks* callbacks) {
  if (!semaphore) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroySemaphore(m_device, semaphore, callbacks);
}

void GraphicsDeviceVulkan::allocateCommandBuffers(
    const VkCommandBufferAllocateInfo* commandBufferCreateInfo,
    VkCommandBuffer* commandBuffers) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkAllocateCommandBuffers(m_device, commandBufferCreateInfo, commandBuffers),
                  "Failed to create command buffers");
}

void GraphicsDeviceVulkan::freeCommandBuffers(
    VkCommandPool commandPool, unsigned commandBufferCount, const VkCommandBuffer* commandBuffers) {
  if (!commandBuffers) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkFreeCommandBuffers(m_device, commandPool, commandBufferCount, commandBuffers);
}

void GraphicsDeviceVulkan::allocateMemory(const VkMemoryAllocateInfo* alloc_info,
                                          const VkAllocationCallbacks* callbacks,
                                          VkDeviceMemory* device_memory) {
  std::lock_guard<std::mutex> lock_gaurd(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkAllocateMemory(m_device, alloc_info, callbacks, device_memory),
                  "failed to allocate image memory!");
}

void GraphicsDeviceVulkan::mapMemory(VkDeviceMemory device_memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** mappedMemory) {
  std::lock_guard<std::mutex> lock_gaurd(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkMapMemory(m_device, device_memory, offset, size, 0, mappedMemory),
                  "Failed to map memory");
}

void GraphicsDeviceVulkan::unmapMemory(VkDeviceMemory device_memory) {
  if (!device_memory) {
    return;
  }
  std::lock_guard<std::mutex> lock_gaurd(vulkan_device::device_mutex);
  vkUnmapMemory(m_device, device_memory);
}

void GraphicsDeviceVulkan::freeMemory(VkDeviceMemory& device_memory,
                                      const VkAllocationCallbacks* callbacks) {
  if (!device_memory) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkFreeMemory(m_device, device_memory, callbacks);
  device_memory = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createBuffer(const VkBufferCreateInfo* buffer_create_info,
                                        const VkAllocationCallbacks* callbacks,
                                        VkBuffer* buffer) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateBuffer(m_device, buffer_create_info, callbacks, buffer), "Failed to create buffer");
}

VkMemoryRequirements GraphicsDeviceVulkan::getBufferMemoryRequirements(VkBuffer buffer) {
  VkMemoryRequirements memory_requirements{};
  {
    std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
    vkGetBufferMemoryRequirements(m_device, buffer, &memory_requirements);
  }
  return memory_requirements;
}

void GraphicsDeviceVulkan::bindBufferMemory(VkBuffer buffer,
                                            VkDeviceMemory device_memory,
                                            unsigned flags) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkBindBufferMemory(m_device, buffer, device_memory, flags),
                  "Failed to bind buffer memory");
}

void GraphicsDeviceVulkan::destroyBuffer(VkBuffer& buffer, const VkAllocationCallbacks* callbacks) {
  if (!buffer) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyBuffer(m_device, buffer, callbacks);
  buffer = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createBufferView(const VkBufferViewCreateInfo* createInfo,
                                            const VkAllocationCallbacks* callbacks,
                                            VkBufferView* bufferView) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateBufferView(m_device, createInfo, callbacks, bufferView),
                  "Failed to create buffer view");
}

void GraphicsDeviceVulkan::destroyBufferView(VkBufferView& bufferView, const VkAllocationCallbacks* callbacks) {
  if (!bufferView) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyBufferView(m_device, bufferView, callbacks);
  bufferView = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createImage(const VkImageCreateInfo* image_create_info,
                                       const VkAllocationCallbacks* callbacks,
                                       VkImage* image) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateImage(m_device, image_create_info, callbacks, image),
                  "failed to create texture image!");
}

VkMemoryRequirements GraphicsDeviceVulkan::getImageMemoryRequirements(VkImage image) {
  VkMemoryRequirements memory_requirements{};
  {
    std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
    vkGetImageMemoryRequirements(m_device, image, &memory_requirements);
  }
  return memory_requirements;
}

void GraphicsDeviceVulkan::bindImageMemory(VkImage image,
                                           VkDeviceMemory device_memory,
                                           unsigned flags) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkBindImageMemory(m_device, image, device_memory, flags),
                  "Failed to bind image memory");
}

void GraphicsDeviceVulkan::destroyImage(VkImage& image, const VkAllocationCallbacks* callbacks) {
  if (!image) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyImage(m_device, image, callbacks);
  image = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createImageView(const VkImageViewCreateInfo* image_view_create_info,
                                       const VkAllocationCallbacks* callbacks,
                                       VkImageView* image_view) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateImageView(m_device, image_view_create_info, callbacks, image_view),
                  "failed to create texture image view!");
}

void GraphicsDeviceVulkan::destroyImageView(
    VkImageView& image_view, const VkAllocationCallbacks* callbacks) {
  if (!image_view) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyImageView(m_device, image_view, callbacks);
  image_view = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createSampler(const VkSamplerCreateInfo* createInfo,
                                         const VkAllocationCallbacks* callbacks,
                                         VkSampler* sampler) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateSampler(m_device, createInfo, callbacks, sampler),
                  "Failed to create sampler");
}

void GraphicsDeviceVulkan::destroySampler(VkSampler& sampler, const VkAllocationCallbacks* callbacks) {
  if (!sampler) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroySampler(m_device, sampler, callbacks);
  sampler = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createPipelineLayout(const VkPipelineLayoutCreateInfo* createInfo,
                                                const VkAllocationCallbacks* callbacks,
                                                VkPipelineLayout* pipelineLayout) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreatePipelineLayout(m_device, createInfo, callbacks, pipelineLayout),
                  "Failed to create pipeline layout");
}

void GraphicsDeviceVulkan::destroyPipelineLayout(VkPipelineLayout& pipelineLayout, const VkAllocationCallbacks* callbacks) {
  if (!pipelineLayout) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyPipelineLayout(m_device, pipelineLayout, callbacks);
  pipelineLayout = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createPipelineCache(const VkPipelineCacheCreateInfo* createInfo,
                                               const VkAllocationCallbacks* callbacks,
                                               VkPipelineCache* pipelineCache) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkCreatePipelineCache(m_device, createInfo, callbacks, pipelineCache);
}

void GraphicsDeviceVulkan::destroyPipelineCache(VkPipelineCache& pipelineCache, const VkAllocationCallbacks* callbacks) {
  if (!pipelineCache) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyPipelineCache(m_device, pipelineCache, callbacks);
  pipelineCache = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::allocateDescriptorSets(const VkDescriptorSetAllocateInfo* alloc_info,
                                                  VkDescriptorSet* descriptor_sets) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkAllocateDescriptorSets(m_device, alloc_info, descriptor_sets),
                  "Failed to create pipeline layout");
}

void GraphicsDeviceVulkan::updateDescriptorSets(unsigned descriptorWriteCount,
                                                const VkWriteDescriptorSet* descriptorWrites,
                                                unsigned descriptorCopyCount,
                                                const VkCopyDescriptorSet* descriptorCopies) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkUpdateDescriptorSets(m_device, descriptorWriteCount, descriptorWrites, descriptorCopyCount,
                         descriptorCopies);
}

void GraphicsDeviceVulkan::freeDescriptorSets(VkDescriptorPool descriptor_pool,
                                              unsigned descriptor_count,
                                              const VkDescriptorSet* descriptor_sets) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkFreeDescriptorSets(m_device, descriptor_pool, descriptor_count, descriptor_sets);
}
     
void GraphicsDeviceVulkan::createDescriptorPool(
    const VkDescriptorPoolCreateInfo* descriptorPoolInfo,
    const VkAllocationCallbacks* callbacks,
    VkDescriptorPool* descriptor_pool) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateDescriptorPool(m_device, descriptorPoolInfo, callbacks, descriptor_pool),
                  "Failed to create descriptor pool");
}

void GraphicsDeviceVulkan::resetDescriptorPool(VkDescriptorPool descriptor_pool) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkResetDescriptorPool(m_device, descriptor_pool, 0);
  descriptor_pool = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::destroyDescriptorPool(VkDescriptorPool descriptor_pool,
                                                 const VkAllocationCallbacks* callbacks) {
  if (!descriptor_pool) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyDescriptorPool(m_device, descriptor_pool, callbacks);
  descriptor_pool = VK_NULL_HANDLE;
}

void GraphicsDeviceVulkan::createDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo* createInfo,
                                                     const VkAllocationCallbacks* callbacks,
                                                     VkDescriptorSetLayout* layouts) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_device, createInfo, callbacks, layouts),
                   "Failed to create descriptor set layout");
}
void GraphicsDeviceVulkan::destroyDescriptorSetLayout(VkDescriptorSetLayout& layout,
                                                      const VkAllocationCallbacks* callbacks) {
  if (!layout) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyDescriptorSetLayout(m_device, layout, callbacks);
  layout = VK_NULL_HANDLE;
}
     
void GraphicsDeviceVulkan::createGraphicsPipelines(VkPipelineCache pipelineCache,
                                                   VkDeviceSize graphicsPipelineCreateCount,
                                                   const VkGraphicsPipelineCreateInfo* createInfo,
                                                   const VkAllocationCallbacks* callbacks,
                                                   VkPipeline* pipelines) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_device, pipelineCache, graphicsPipelineCreateCount,
                                            createInfo, callbacks, pipelines),
                  "Failed to create graphic pipelines");
}

void GraphicsDeviceVulkan::createComputePipelines(VkPipelineCache pipelineCache,
                                                  VkDeviceSize graphicsPipelineCreateCount,
                                                  const VkComputePipelineCreateInfo* createInfo,
                                                  const VkAllocationCallbacks* callbacks,
                                                  VkPipeline* pipelines) {
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  VK_CHECK_RESULT(vkCreateComputePipelines(m_device, pipelineCache, graphicsPipelineCreateCount,
                                            createInfo, callbacks, pipelines),
                  "Failed to create compute pipelines");
}

void GraphicsDeviceVulkan::destroyPipeline(VkPipeline& pipeline, const VkAllocationCallbacks* callbacks) {
  if (!pipeline) {
    return;
  }
  std::lock_guard<std::mutex> lock_guard(vulkan_device::device_mutex);
  vkDestroyPipeline(m_device, pipeline, callbacks);
  pipeline = VK_NULL_HANDLE;
}


