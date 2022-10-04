#include "DescriptorLayout.h"
#include <cassert>
#include <stdexcept>

// *************** Descriptor Set Layout Builder *********************

DescriptorLayout::Builder& DescriptorLayout::Builder::addBinding(
    uint32_t binding,
    VkDescriptorType descriptorType,
    VkShaderStageFlags stageFlags,
    uint32_t count) {
  assert(m_bindings.count(binding) == 0 && "Binding already in use");
  VkDescriptorSetLayoutBinding layoutBinding{};
  layoutBinding.binding = binding;
  layoutBinding.descriptorType = descriptorType;
  layoutBinding.descriptorCount = count;
  layoutBinding.stageFlags = stageFlags;
  m_bindings[binding] = layoutBinding;
  return *this;
}

std::unique_ptr<DescriptorLayout> DescriptorLayout::Builder::build() const {
  return std::make_unique<DescriptorLayout>(m_device, m_bindings);
}

// *************** Descriptor Set Layout *********************

DescriptorLayout::DescriptorLayout(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
    : m_device{device}, m_bindings{bindings} {
  std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
  for (auto kv : m_bindings) {
    setLayoutBindings.push_back(kv.second);
  }

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
  descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
  descriptorSetLayoutInfo.pBindings = setLayoutBindings.data();

  if (vkCreateDescriptorSetLayout(m_device->getLogicalDevice(), &descriptorSetLayoutInfo, nullptr,
                                  &m_descriptor_set_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

DescriptorLayout::~DescriptorLayout() {
  vkDestroyDescriptorSetLayout(m_device->getLogicalDevice(), m_descriptor_set_layout, nullptr);
}

// *************** Descriptor Pool Builder *********************

DescriptorPool::Builder& DescriptorPool::Builder::addPoolSize(VkDescriptorType descriptorType,
                                                              uint32_t count) {
  m_pool_sizes.push_back({descriptorType, count});
  return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::setPoolFlags(
    VkDescriptorPoolCreateFlags flags) {
  poolFlags = flags;
  return *this;
}
DescriptorPool::Builder& DescriptorPool::Builder::setMaxSets(uint32_t count) {
  maxSets = count;
  return *this;
}

std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const {
  return std::make_unique<DescriptorPool>(m_device, maxSets, poolFlags, m_pool_sizes);
}

// *************** Descriptor Pool *********************

DescriptorPool::DescriptorPool(std::unique_ptr<GraphicsDeviceVulkan>& device,
                               uint32_t maxSets,
                               VkDescriptorPoolCreateFlags poolFlags,
                               const std::vector<VkDescriptorPoolSize>& poolSizes)
    : m_device{device} {
  VkDescriptorPoolCreateInfo descriptorPoolInfo{};
  descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  descriptorPoolInfo.pPoolSizes = poolSizes.data();
  descriptorPoolInfo.maxSets = maxSets;
  descriptorPoolInfo.flags = poolFlags;

  if (vkCreateDescriptorPool(m_device->getLogicalDevice(), &descriptorPoolInfo, nullptr, &m_descriptor_pool) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

DescriptorPool::~DescriptorPool() {
  vkDestroyDescriptorPool(m_device->getLogicalDevice(), m_descriptor_pool, nullptr);
}

bool DescriptorPool::allocateDescriptor(const VkDescriptorSetLayout descriptorSetLayout,
                                        VkDescriptorSet& descriptor) const {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_descriptor_pool;
  allocInfo.pSetLayouts = &descriptorSetLayout;
  allocInfo.descriptorSetCount = 1;

  // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
  // a new pool whenever an old pool fills up. But this is beyond our current scope
  if (vkAllocateDescriptorSets(m_device->getLogicalDevice(), &allocInfo, &descriptor) != VK_SUCCESS) {
    return false;
  }
  return true;
}

void DescriptorPool::freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const {
  vkFreeDescriptorSets(m_device->getLogicalDevice(), m_descriptor_pool,
                       static_cast<uint32_t>(descriptors.size()), descriptors.data());
}

void DescriptorPool::resetPool() {
  vkResetDescriptorPool(m_device->getLogicalDevice(), m_descriptor_pool, 0);
}

// *************** Descriptor Writer *********************

DescriptorWriter::DescriptorWriter(std::unique_ptr<DescriptorLayout>& setLayout, std::unique_ptr<DescriptorPool>& pool)
    : m_set_layout{setLayout}, m_pool{pool} {}

DescriptorWriter& DescriptorWriter::writeBuffer(uint32_t binding,
                                                VkDescriptorBufferInfo* bufferInfo) {
  assert(m_set_layout->m_bindings.count(binding) == 1 && "Layout does not contain specified binding");

  auto& bindingDescription = m_set_layout->m_bindings[binding];

  assert(bindingDescription.descriptorCount == 1 &&
         "Binding single descriptor info, but binding expects multiple");

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType = bindingDescription.descriptorType;
  write.dstBinding = binding;
  write.pBufferInfo = bufferInfo;
  write.descriptorCount = 1;

  m_writes.push_back(write);
  return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding,
                                               VkDescriptorImageInfo* imageInfo) {
  assert(m_set_layout->m_bindings.count(binding) == 1 && "Layout does not contain specified binding");

  auto& bindingDescription = m_set_layout->m_bindings[binding];

  assert(bindingDescription.descriptorCount == 1 &&
         "Binding single descriptor info, but binding expects multiple");

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType = bindingDescription.descriptorType;
  write.dstBinding = binding;
  write.pImageInfo = imageInfo;
  write.descriptorCount = 1;

  m_writes.push_back(write);
  return *this;
}

bool DescriptorWriter::build(VkDescriptorSet& set) {
  bool success = m_pool->allocateDescriptor(m_set_layout->getDescriptorSetLayout(), set);
  if (!success) {
    return false;
  }
  overwrite(set);
  return true;
}

void DescriptorWriter::overwrite(VkDescriptorSet& set) {
  for (auto& write : m_writes) {
    write.dstSet = set;
  }
  vkUpdateDescriptorSets(m_pool->m_device->getLogicalDevice(), m_writes.size(), m_writes.data(), 0, nullptr);
}

