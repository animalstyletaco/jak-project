#pragma once
#include <unordered_map>

#include "GraphicsDeviceVulkan.h"

class DescriptorLayout {
 public:
  class Builder {
   public:
    Builder(std::shared_ptr<GraphicsDeviceVulkan> device) : m_device{device} {}

    Builder& addBinding(uint32_t binding,
                        VkDescriptorType descriptorType,
                        VkShaderStageFlags stageFlags,
                        uint32_t count = 1);
    std::unique_ptr<DescriptorLayout> build() const;

   private:
    std::shared_ptr<GraphicsDeviceVulkan> m_device;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_bindings{};
  };

  DescriptorLayout(std::shared_ptr<GraphicsDeviceVulkan> device,
                   std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings,
                   VkDescriptorSetLayoutCreateFlags flags = 0);
  ~DescriptorLayout();
  DescriptorLayout(const DescriptorLayout&) = delete;
  DescriptorLayout& operator=(const DescriptorLayout&) = delete;

  VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptor_set_layout; }

 private:
  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VkDescriptorSetLayout m_descriptor_set_layout;
  std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_bindings;

  friend class DescriptorWriter;
};

class DescriptorPool {
 public:
  class Builder {
   public:
    Builder(std::shared_ptr<GraphicsDeviceVulkan> device) : m_device{device} {}

    Builder& addPoolSize(VkDescriptorType descriptorType, uint32_t count);
    Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
    Builder& setMaxSets(uint32_t count);
    std::unique_ptr<DescriptorPool> build() const;

   private:
    std::shared_ptr<GraphicsDeviceVulkan> m_device;
    std::vector<VkDescriptorPoolSize> m_pool_sizes{};
    uint32_t maxSets = 1000;
    VkDescriptorPoolCreateFlags poolFlags = 0;
  };

  DescriptorPool(std::shared_ptr<GraphicsDeviceVulkan> lveDevice,
                 uint32_t maxSets,
                 VkDescriptorPoolCreateFlags poolFlags,
                 const std::vector<VkDescriptorPoolSize>& poolSizes);
  ~DescriptorPool();
  DescriptorPool(const DescriptorPool&) = delete;
  DescriptorPool& operator=(const DescriptorPool&) = delete;

  bool allocateDescriptor(const VkDescriptorSetLayout* descriptorSetLayout,
                          VkDescriptorSet* descriptor,
                          uint32_t descriptorSetCount = 1) const;

  void freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const;
  VkDescriptorPool getDescriptorPool() { return m_descriptor_pool; }
  std::shared_ptr<GraphicsDeviceVulkan> device() { return m_device; }

  void resetPool();

 private:
  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  VkDescriptorPool m_descriptor_pool;

  friend class DescriptorWriter;
};

class DescriptorWriter {
 public:
  DescriptorWriter(std::unique_ptr<DescriptorLayout>& setLayout,
                   std::unique_ptr<DescriptorPool>& pool);

  VkWriteDescriptorSet writeBufferDescriptorSet(uint32_t binding,
                                                VkDescriptorBufferInfo* bufferInfo,
                                                uint32_t bufferInfoCount = 1) const;
  VkWriteDescriptorSet writeImageDescriptorSet(uint32_t binding,
                                               VkDescriptorImageInfo* imageInfo,
                                               uint32_t imageInfoCount = 1) const;
  VkWriteDescriptorSet writeBufferViewDescriptorSet(uint32_t binding,
                                                    VkBufferView* bufferView,
                                                    uint32_t bufferViewCount) const;

  DescriptorWriter& writeBuffer(uint32_t binding,
                                VkDescriptorBufferInfo* bufferInfo,
                                uint32_t bufferInfoCount = 1);
  DescriptorWriter& writeImage(uint32_t binding,
                               VkDescriptorImageInfo* imageInfo,
                               uint32_t imageInfoCount = 1);
  DescriptorWriter& writeBufferView(uint32_t binding,
                                    VkBufferView* bufferView,
                                    uint32_t bufferViewCount = 1);

  bool build(VkDescriptorSet& set);
  bool build(std::vector<VkDescriptorSet>& set);

  void overwrite(VkDescriptorSet& set);
  void overwrite(std::vector<VkDescriptorSet>& sets);

  bool allocateDescriptor(VkDescriptorSet& set);

  std::vector<VkWriteDescriptorSet>& getWriteDescriptorSets() { return m_writes; };

 private:
  std::unique_ptr<DescriptorLayout>& m_set_layout;
  std::unique_ptr<DescriptorPool>& m_pool;
  std::vector<VkWriteDescriptorSet> m_writes;
};
