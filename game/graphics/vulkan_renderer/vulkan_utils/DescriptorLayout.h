#pragma once

#include "GraphicsDeviceVulkan.h"

class DescriptorLayout {
 public:
  class Builder {
   public:
    Builder(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device{device} {}

    Builder& addBinding(uint32_t binding,
                        VkDescriptorType descriptorType,
                        VkShaderStageFlags stageFlags,
                        uint32_t count = 1);
    std::unique_ptr<DescriptorLayout> build() const;

   private:
    std::unique_ptr<GraphicsDeviceVulkan>& m_device;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_bindings{};
  };

  DescriptorLayout(std::unique_ptr<GraphicsDeviceVulkan>& device,
                   std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings);
  ~DescriptorLayout();
  DescriptorLayout(const DescriptorLayout&) = delete;
  DescriptorLayout& operator=(const DescriptorLayout&) = delete;

  VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptor_set_layout; }

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkDescriptorSetLayout m_descriptor_set_layout;
  std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_bindings;

  friend class DescriptorWriter;
};

class DescriptorPool {
 public:
  class Builder {
   public:
    Builder(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device{device} {}

    Builder& addPoolSize(VkDescriptorType descriptorType, uint32_t count);
    Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
    Builder& setMaxSets(uint32_t count);
    std::unique_ptr<DescriptorPool> build() const;

   private:
    std::unique_ptr<GraphicsDeviceVulkan>& m_device;
    std::vector<VkDescriptorPoolSize> m_pool_sizes{};
    uint32_t maxSets = 1000;
    VkDescriptorPoolCreateFlags poolFlags = 0;
  };

  DescriptorPool(std::unique_ptr<GraphicsDeviceVulkan>& lveDevice,
                 uint32_t maxSets,
                 VkDescriptorPoolCreateFlags poolFlags,
                 const std::vector<VkDescriptorPoolSize>& poolSizes);
  ~DescriptorPool();
  DescriptorPool(const DescriptorPool&) = delete;
  DescriptorPool& operator=(const DescriptorPool&) = delete;

  bool allocateDescriptor(const VkDescriptorSetLayout descriptorSetLayout,
                          VkDescriptorSet& descriptor) const;

  void freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const;
  VkDescriptorPool getDescriptorPool() { return m_descriptor_pool; }

  void resetPool();

 private:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VkDescriptorPool m_descriptor_pool;

  friend class DescriptorWriter;
};

class DescriptorWriter {
 public:
  DescriptorWriter(DescriptorLayout& setLayout, DescriptorPool& pool);

  DescriptorWriter& writeBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo);
  DescriptorWriter& writeImage(uint32_t binding, VkDescriptorImageInfo* imageInfo);

  bool build(VkDescriptorSet& set);
  void overwrite(VkDescriptorSet& set);

 private:
  DescriptorLayout& m_set_layout;
  DescriptorPool& m_pool;
  std::vector<VkWriteDescriptorSet> m_writes;
};
