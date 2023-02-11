#pragma once

#include "GraphicsDeviceVulkan.h"

class VulkanSamplerHelper {
public:
  VulkanSamplerHelper(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device) {
    m_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    m_create_info.maxAnisotropy = m_device->getMaxSamplerAnisotropy();
  };
  VkSamplerCreateInfo& GetSamplerCreateInfo() { return m_create_info; }
  VkSampler GetSampler() { return m_sampler; };
  VkResult CreateSampler() {
    DestroySampler();
    return vkCreateSampler(m_device->getLogicalDevice(), &m_create_info, nullptr, &m_sampler);
  }

  ~VulkanSamplerHelper() { DestroySampler(); }

private:
  void DestroySampler() {
   if (m_sampler) {
      vkDestroySampler(m_device->getLogicalDevice(), m_sampler, nullptr);
   }
  }

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;

  VkSamplerCreateInfo m_create_info{};
  VkSampler m_sampler = VK_NULL_HANDLE;
};
