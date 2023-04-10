#include "FramebufferHelper.h"

#include <cassert>
#include <array>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FramebufferVulkan::FramebufferVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VkFormat format)
    : color_texture(device),
      depth_texture(device),
      mipmap_texture(device),
      sampler_helper(device), m_device(device), m_format(format) {
  VkSamplerCreateInfo& samplerInfo = sampler_helper.GetSamplerCreateInfo();
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.minLod = 0.0f;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

void FramebufferVulkan::setViewportScissor(VkCommandBuffer commandBuffer) {
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extents.width);
  viewport.height = static_cast<float>(extents.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{{0, 0}, extents};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void FramebufferVulkan::beginRenderPass(VkCommandBuffer commandBuffer,
                                        std::vector<VkClearValue> clearValues) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = render_pass;
  renderPassInfo.framebuffer = frame_buffer;

  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = extents;

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void FramebufferVulkan::beginRenderPass(VkCommandBuffer commandBuffer) {
  std::vector<VkClearValue> clearValues;
  clearValues.resize(2);

  clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
  clearValues[1].depthStencil = {1.0f, 0};

  beginRenderPass(commandBuffer, clearValues);
}

FramebufferVulkan::~FramebufferVulkan() {
  if (frame_buffer) {
    vkDestroyFramebuffer(m_device->getLogicalDevice(), frame_buffer, nullptr);
  }
  if (render_pass) {
    vkDestroyRenderPass(m_device->getLogicalDevice(), render_pass, nullptr);
  }
}

FramebufferVulkanHelper::FramebufferVulkanHelper(unsigned w,
                                                 unsigned h,
                                                 VkFormat format,
                                                 std::unique_ptr<GraphicsDeviceVulkan>& device, int num_levels)
    : m_device(device), m_format(format) {
  extents = {w, h};

  m_framebuffers.resize(num_levels, {m_device, format});

  uint32_t iterator = 0;
  for (uint32_t i = 0; i < m_framebuffers.size(); i++) {
    VkExtent3D textureExtents = {extents.width >> i, extents.height >> i, 1};
    m_framebuffers[i].extents.width = textureExtents.width;
    m_framebuffers[i].extents.height = textureExtents.height;

    uint32_t mipmapLevel = m_framebuffers[i].GetMipmapLevel(i);
    m_framebuffers[i].initializeFramebufferAtLevel(mipmapLevel);
  }
}

uint32_t FramebufferVulkan::GetMipmapLevel(int level) {
  // Check needed to avoid validation error. See
  // https://vulkan-tutorial.com/Generating_Mipmaps#page_Image-creation for more info
  uint32_t maxMinmapLevels =
      static_cast<uint32_t>(
          std::floor(std::log2(std::max(extents.width, extents.height)))) +
      1;
  return (level + 1 > maxMinmapLevels) ? maxMinmapLevels : level + 1;
}

void FramebufferVulkan::initializeFramebufferAtLevel(int mipmapLevel) {
  VkExtent3D textureExtents{extents.width, extents.height, 1};
  mipmap_texture.createImage(textureExtents, mipmapLevel, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                             m_format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  mipmap_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, m_format,
                                 VK_IMAGE_ASPECT_COLOR_BIT, mipmapLevel);

  color_texture.createImage(textureExtents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT, m_format,
                            VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

  color_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, m_format,
                                VK_IMAGE_ASPECT_COLOR_BIT, 1);

  depth_texture.createImage(textureExtents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                            GetSupportedDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  depth_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, GetSupportedDepthFormat(),
                                            VK_IMAGE_ASPECT_DEPTH_BIT, 1);

  auto& samplerInfo = sampler_helper.GetSamplerCreateInfo();
  samplerInfo.maxLod = static_cast<float>(mipmapLevel);

  sampler_helper.CreateSampler();

  createRenderPass();
  createFramebuffer();
}

void FramebufferVulkan::createRenderPass() {
  if (render_pass) {
    vkDestroyRenderPass(m_device->getLogicalDevice(), render_pass, nullptr);
    render_pass = VK_NULL_HANDLE;
  }

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = m_format;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = GetSupportedDepthFormat();
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

		// Use subpass dependencies for layout transitions
  std::array<VkSubpassDependency, 2> dependencies;

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = dependencies.size();
  renderPassInfo.pDependencies = dependencies.data();

  if (vkCreateRenderPass(m_device->getLogicalDevice(), &renderPassInfo, nullptr, &render_pass) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}

void FramebufferVulkan::createFramebuffer() {
  if (frame_buffer) {
    vkDestroyFramebuffer(m_device->getLogicalDevice(), frame_buffer, nullptr);
  }

  std::array<VkImageView, 2> attachments = {color_texture.getImageView(),
                                            depth_texture.getImageView()};

  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = render_pass;
  framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  framebufferInfo.pAttachments = attachments.data();
  framebufferInfo.width = extents.width;
  framebufferInfo.height = extents.height;
  framebufferInfo.layers = 1;

  if (vkCreateFramebuffer(m_device->getLogicalDevice(), &framebufferInfo, nullptr,
                          &frame_buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create framebuffer!");
  }
}

void FramebufferVulkanHelper::beginRenderPass(VkCommandBuffer commandBuffer, int index) {
  m_framebuffers[index].beginRenderPass(commandBuffer);
}

void FramebufferVulkanHelper::beginRenderPass(VkCommandBuffer commandBuffer, std::vector<VkClearValue>& clearValues, int index) {
  m_framebuffers[index].beginRenderPass(commandBuffer, clearValues);
}


VkFormat FramebufferVulkan::GetSupportedDepthFormat() {
  return m_device->findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}


