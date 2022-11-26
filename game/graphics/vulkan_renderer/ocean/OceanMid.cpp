#include "OceanMid.h"

OceanMidVulkan::OceanMidVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info) : m_common_ocean_renderer(device, vulkan_info) {
}

//TODO: Put ocean interface declarations here
