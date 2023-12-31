#include "DescriptorLayout.h"

#include <cassert>
#include <stdexcept>

// *************** Descriptor Set Layout Builder *********************

DescriptorLayout::Builder& DescriptorLayout::Builder::addBinding(uint32_t binding,
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
    std::shared_ptr<GraphicsDeviceVulkan> device,
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings,
    VkDescriptorSetLayoutCreateFlags descriptor_layout_create_flag)
    : m_device{device}, m_bindings{bindings} {
  std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
  for (auto& [binding_index, binding] : m_bindings) {
    setLayoutBindings.push_back(binding);
  }

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
  descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutInfo.flags = descriptor_layout_create_flag;
  descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
  descriptorSetLayoutInfo.pBindings = setLayoutBindings.data();

  VK_CHECK_RESULT(
      vkCreateDescriptorSetLayout(m_device->getLogicalDevice(), &descriptorSetLayoutInfo, nullptr,
                                  &m_descriptor_set_layout),
      "failed to create descriptor set layout!");
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

DescriptorPool::Builder& DescriptorPool::Builder::setPoolFlags(VkDescriptorPoolCreateFlags flags) {
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

DescriptorPool::DescriptorPool(std::shared_ptr<GraphicsDeviceVulkan> device,
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

  VK_CHECK_RESULT(vkCreateDescriptorPool(m_device->getLogicalDevice(), &descriptorPoolInfo, nullptr,
                             &m_descriptor_pool), "failed to create descriptor pool!");
}

DescriptorPool::~DescriptorPool() {
  vkDestroyDescriptorPool(m_device->getLogicalDevice(), m_descriptor_pool, nullptr);
}

bool DescriptorPool::allocateDescriptor(const VkDescriptorSetLayout* descriptorSetLayout,
                                        VkDescriptorSet* descriptors,
                                        uint32_t descriptorSetCount) const {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_descriptor_pool;
  allocInfo.pSetLayouts = descriptorSetLayout;
  allocInfo.descriptorSetCount = descriptorSetCount;

  m_device->allocateDescriptorSets(&allocInfo, descriptors);
}

void DescriptorPool::freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const {
  m_device->freeDescriptorSets(m_descriptor_pool, static_cast<uint32_t>(descriptors.size()),
                               descriptors.data());
}

void DescriptorPool::resetPool() {
  m_device->resetDescriptorPool(m_descriptor_pool);
}

// *************** Descriptor Writer *********************

DescriptorWriter::DescriptorWriter(std::unique_ptr<DescriptorLayout>& setLayout,
                                   std::unique_ptr<DescriptorPool>& pool)
    : m_set_layout{setLayout}, m_pool{pool} {}

VkWriteDescriptorSet DescriptorWriter::writeBufferDescriptorSet(uint32_t binding,
                                                                VkDescriptorBufferInfo* bufferInfo,
                                                                uint32_t bufferInfoCount) const {
  assert(m_set_layout->m_bindings.count(binding) == 1 &&
         "Layout does not contain specified binding");

  auto& bindingDescription = m_set_layout->m_bindings[binding];

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType = bindingDescription.descriptorType;
  write.dstBinding = binding;
  write.pBufferInfo = bufferInfo;
  write.descriptorCount = bufferInfoCount;

  return write;
}

VkWriteDescriptorSet DescriptorWriter::writeImageDescriptorSet(uint32_t binding,
                                                               VkDescriptorImageInfo* imageInfo,
                                                               uint32_t imageInfoCount) const {
  assert(m_set_layout->m_bindings.count(binding) == 1 &&
         "Layout does not contain specified binding");

  auto& bindingDescription = m_set_layout->m_bindings[binding];

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType = bindingDescription.descriptorType;
  write.dstBinding = binding;
  write.pImageInfo = imageInfo;
  write.descriptorCount = imageInfoCount;

  return write;
}

VkWriteDescriptorSet DescriptorWriter::writeBufferViewDescriptorSet(
    uint32_t binding,
    VkBufferView* bufferViews,
    uint32_t bufferViewCount) const {
  assert(m_set_layout->m_bindings.count(binding) == 1 &&
         "Layout does not contain specified binding");

  auto& bindingDescription = m_set_layout->m_bindings[binding];

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType = bindingDescription.descriptorType;
  write.dstBinding = binding;
  write.pTexelBufferView = bufferViews;
  write.descriptorCount = bufferViewCount;

  return write;
}

DescriptorWriter& DescriptorWriter::writeBuffer(uint32_t binding,
                                                VkDescriptorBufferInfo* bufferInfo,
                                                uint32_t bufferInfoCount) {
  m_writes.push_back(writeBufferDescriptorSet(binding, bufferInfo, bufferInfoCount));
  return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding,
                                               VkDescriptorImageInfo* imageInfo,
                                               uint32_t imageInfoCount) {
  m_writes.push_back(writeImageDescriptorSet(binding, imageInfo, imageInfoCount));
  return *this;
}

DescriptorWriter& DescriptorWriter::writeBufferView(uint32_t binding,
                                                    VkBufferView* bufferViews,
                                                    uint32_t bufferViewCount) {
  m_writes.push_back(writeBufferViewDescriptorSet(binding, bufferViews, bufferViewCount));
  return *this;
}

bool DescriptorWriter::allocateDescriptor(VkDescriptorSet& set) {
  auto layout = m_set_layout->getDescriptorSetLayout();
  return m_pool->allocateDescriptor(&layout, &set);
}

bool DescriptorWriter::build(std::vector<VkDescriptorSet>& sets) {
  bool success = true;
  for (auto& set : sets) {
    success &= build(set);
  }
  return success;
}

bool DescriptorWriter::build(VkDescriptorSet& set) {
  if(!allocateDescriptor(set)){
    return false;
  }
  overwrite(set);
  return true;
}

void DescriptorWriter::overwrite(std::vector<VkDescriptorSet>& sets) {
  for (auto& set : sets) {
    overwrite(set);
  }
}

void DescriptorWriter::overwrite(VkDescriptorSet& set) {
  for (auto& write : m_writes) {
    write.dstSet = set;
  }
  m_pool->m_device->updateDescriptorSets(m_writes.size(), m_writes.data(), 0, nullptr);
}
