#include "FramebufferHelper.h"

#include <array>
#include <cassert>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

namespace framebuffer_vulkan {
VkFormat GetSupportedDepthFormat(std::shared_ptr<GraphicsDeviceVulkan> device) {
  return device->findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
}  // namespace framebuffer_vulkan

FramebufferVulkan::FramebufferVulkan(std::shared_ptr<GraphicsDeviceVulkan> device, VkFormat format)
    : m_device(device),
      m_format(format),
      m_sampler_helper(device),
      m_multisample_texture(device),
      m_color_texture(device),
      m_depth_texture(device) {
  VkSamplerCreateInfo& samplerInfo = m_sampler_helper.GetSamplerCreateInfo();
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
  m_sampler_helper.CreateSampler();
}

void FramebufferVulkan::createFramebuffer() {
  if (framebuffer) {
    vkDestroyFramebuffer(m_device->getLogicalDevice(), framebuffer, nullptr);
  }

  std::vector<VkImageView> attachments;
  if (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) {
    attachments = {m_multisample_texture.getImageView(), m_color_texture.getImageView(),
                   m_depth_texture.getImageView()};
  } else {
    attachments = {m_color_texture.getImageView(), m_depth_texture.getImageView()};
  }

  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = render_pass;
  framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  framebufferInfo.pAttachments = attachments.data();
  framebufferInfo.width = extents.width;
  framebufferInfo.height = extents.height;
  framebufferInfo.layers = 1;

  vulkan_utils::check_results(
      vkCreateFramebuffer(m_device->getLogicalDevice(), &framebufferInfo, nullptr, &framebuffer),
      "failed to create framebuffer!");
}

FramebufferVulkan::~FramebufferVulkan() {
  if (framebuffer) {
    vkDestroyFramebuffer(m_device->getLogicalDevice(), framebuffer, nullptr);
  }
}

FramebufferVulkanHelper::FramebufferVulkanHelper(unsigned w,
                                                 unsigned h,
                                                 VkFormat format,
                                                 std::shared_ptr<GraphicsDeviceVulkan> device,
                                                 VkSampleCountFlagBits samples,
                                                 int mipmapLevel)
    : m_device(device),
      m_format(format),
      m_framebuffer(device, format),
      m_mipmap_level(mipmapLevel) {
  m_framebuffer.extents = extents = {w, h};
  m_framebuffer.initializeFramebufferAtLevel(samples, m_mipmap_level);
}

void FramebufferVulkanHelper::setViewportScissor(VkCommandBuffer commandBuffer) {
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

void FramebufferVulkan::initializeFramebufferAtLevel(VkSampleCountFlagBits samples,
                                                     unsigned mipmapLevel) {
  m_current_msaa = samples;
  VkExtent3D textureExtents{extents.width, extents.height, 1};
  if (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) {
    m_multisample_texture.createImage(
        textureExtents, mipmapLevel, VK_IMAGE_TYPE_2D, samples, m_format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    m_multisample_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, m_format,
                                          VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }

  m_color_texture.createImage(textureExtents, mipmapLevel, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                              m_format, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

  m_color_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, m_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

  m_depth_texture.createImage(
      textureExtents, mipmapLevel, VK_IMAGE_TYPE_2D, samples,
      framebuffer_vulkan::GetSupportedDepthFormat(m_device), VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  m_depth_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D,
                                  framebuffer_vulkan::GetSupportedDepthFormat(m_device),
                                  VK_IMAGE_ASPECT_DEPTH_BIT, 1);

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
  colorAttachment.samples = m_current_msaa;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentDescription colorAttachmentResolve{};
  if (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) {
    colorAttachmentResolve.format = m_format;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = framebuffer_vulkan::GetSupportedDepthFormat(m_device);
  depthAttachment.samples = m_current_msaa;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentResolveRef{};
  VkAttachmentReference depthAttachmentRef{};

  if (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) {
    colorAttachmentResolveRef.attachment = 1;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    depthAttachmentRef.attachment = 2;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  } else {
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pResolveAttachments =
      (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) ? &colorAttachmentResolveRef : nullptr;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  std::array<VkSubpassDependency, 2> dependencies = {};

  // Depth attachment
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].dstStageMask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[0].dstAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  dependencies[0].dependencyFlags = 0;
  // Color attachment
  dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].dstSubpass = 0;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].srcAccessMask = 0;
  dependencies[1].dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  dependencies[1].dependencyFlags = 0;

  std::vector<VkAttachmentDescription> attachments;
  if (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) {
    attachments = {colorAttachment, colorAttachmentResolve, depthAttachment};
  } else {
    attachments = {colorAttachment, depthAttachment};
  }

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = dependencies.size();
  renderPassInfo.pDependencies = dependencies.data();

  vulkan_utils::check_results(
      vkCreateRenderPass(m_device->getLogicalDevice(), &renderPassInfo, nullptr, &render_pass),
      "failed to create render pass!");
}

void FramebufferVulkan::beginRenderPass(VkCommandBuffer commandBuffer,
                                        std::vector<VkClearValue>& clearValues) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = render_pass;
  renderPassInfo.framebuffer = framebuffer;

  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = extents;

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void FramebufferVulkan::beginRenderPass(VkCommandBuffer commandBuffer) {
  std::vector<VkClearValue> clearValues;
  if (m_current_msaa != VK_SAMPLE_COUNT_1_BIT) {
    clearValues.resize(3);
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[2].depthStencil = {
        1.0f, 0};  // Double check to see if this is correct. Clear value for depth is normally 1
  } else {
    clearValues.resize(2);
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = {
        1.0f, 0};  // Double check to see if this is correct. Clear value for depth is normally 1
  }

  beginRenderPass(commandBuffer, clearValues);
}

void FramebufferVulkanHelper::beginRenderPass(VkCommandBuffer commandBuffer, unsigned mipmapLevel) {
  m_framebuffer.beginRenderPass(commandBuffer);
}

void FramebufferVulkanHelper::beginRenderPass(VkCommandBuffer commandBuffer,
                                              std::vector<VkClearValue>& clearValues,
                                              unsigned mipmapLevel) {
  m_framebuffer.beginRenderPass(commandBuffer, clearValues);
}

FramebufferVulkanCopier::FramebufferVulkanCopier(std::shared_ptr<GraphicsDeviceVulkan> device,
                                                 std::unique_ptr<SwapChain>& swapChain)
    : m_device(device),
      m_framebuffer_image(device),
      m_sampler_helper(device),
      m_swap_chain(swapChain) {
  createFramebufferImage();

  VkSamplerCreateInfo& samplerCreateInfo = m_sampler_helper.GetSamplerCreateInfo();
  samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
  samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
  samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  m_sampler_helper.CreateSampler();
}

void FramebufferVulkanCopier::createFramebufferImage() {
  VkExtent3D extents{m_fbo_width, m_fbo_height, 1};
  m_framebuffer_image.createImage(extents, 1, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  m_framebuffer_image.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

FramebufferVulkanCopier::~FramebufferVulkanCopier() {}

void FramebufferVulkanCopier::copy_now(int render_fb_w,
                                       int render_fb_h,
                                       int render_fb_x,
                                       int render_fb_y,
                                       uint32_t swapChainImageIndex) {
  if (m_fbo_width != render_fb_w || m_fbo_height != render_fb_h) {
    m_fbo_width = render_fb_w;
    m_fbo_height = render_fb_h;

    createFramebufferImage();
  }
  VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands();

  VkImage srcImage = m_swap_chain->GetSwapChainImageAtIndex(swapChainImageIndex);
  m_device->transitionImageLayout(srcImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  VkImageBlit imageBlit{};
  imageBlit.srcOffsets[0] = {render_fb_x, render_fb_y, 1};
  imageBlit.srcOffsets[1] = {render_fb_x + render_fb_w, render_fb_y + render_fb_h, 1};

  imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBlit.srcSubresource.mipLevel = 0;
  imageBlit.srcSubresource.baseArrayLayer = 0;
  imageBlit.srcSubresource.layerCount = 1;

  imageBlit.dstOffsets[0] = {0, 0, 1};
  imageBlit.dstOffsets[1] = {m_fbo_width, m_fbo_height, 1};

  imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBlit.dstSubresource.mipLevel = 0;
  imageBlit.dstSubresource.baseArrayLayer = 0;
  imageBlit.dstSubresource.layerCount = 1;

  vkCmdBlitImage(commandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 m_framebuffer_image.getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                 &imageBlit, VK_FILTER_NEAREST);

  m_device->endSingleTimeCommands(commandBuffer);

  m_device->transitionImageLayout(srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
