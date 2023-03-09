#include "FramebufferHelper.h"

#include <cassert>
#include <array>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FramebufferVulkanHelper::FramebufferVulkanHelper(unsigned w,
                                                 unsigned h,
                                                 VkFormat format,
                                                 std::unique_ptr<GraphicsDeviceVulkan>& device, int num_levels)
    : m_device(device), m_sampler_helper{device} {
  extents = {w, h};

  VkSamplerCreateInfo& samplerInfo = m_sampler_helper.GetSamplerCreateInfo();
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  m_sampler_helper.CreateSampler();

  m_color_textures.resize(num_levels, m_device);
  m_depth_textures.resize(num_levels, m_device);
  m_frame_buffers.resize(num_levels, VK_NULL_HANDLE);

  for (uint32_t i = 0; i < num_levels; i++) {
    VkExtent3D textureExtents = {extents.width >> i, extents.height >> i, 1};
    //Check needed to avoid validation error. See https://vulkan-tutorial.com/Generating_Mipmaps#page_Image-creation for more info
    uint32_t maxMinmapLevels =
        static_cast<uint32_t>(std::floor(std::log2(std::max(textureExtents.width, textureExtents.height)))) + 1;
    uint32_t minmapLevel = (i + 1 > maxMinmapLevels) ? maxMinmapLevels : i + 1;
    m_color_textures[i].createImage(textureExtents, minmapLevel, VK_IMAGE_TYPE_2D, format,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  
    m_color_textures[i].createImageView(VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    m_depth_textures[i].createImage(textureExtents, minmapLevel, VK_IMAGE_TYPE_2D,
                                    GetSupportedDepthFormat(),
                                    VK_IMAGE_TILING_OPTIMAL,
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_depth_textures[i].createImageView(VK_IMAGE_VIEW_TYPE_2D, GetSupportedDepthFormat(),
                                        VK_IMAGE_ASPECT_DEPTH_BIT, minmapLevel);
  }
}

void FramebufferVulkanHelper::createRenderPass(VkFormat format) {
  if (m_render_pass) {
    vkDestroyRenderPass(m_device->getLogicalDevice(), m_render_pass, nullptr);
    m_render_pass = VK_NULL_HANDLE;
  }

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = format;
  colorAttachment.samples = m_device->getMsaaCount();
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = GetSupportedDepthFormat();
  depthAttachment.samples = m_device->getMsaaCount();
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription colorAttachmentResolve{};
  colorAttachmentResolve.format = format;
  colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentResolveRef{};
  colorAttachmentResolveRef.attachment = 2;
  colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency dependency = {};
  dependency.dstSubpass = 0;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.srcAccessMask = 0;
  dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

  std::array<VkAttachmentDescription, 3> attachments = {colorAttachment, depthAttachment,
                                                        colorAttachmentResolve};
  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(m_device->getLogicalDevice(), &renderPassInfo, nullptr, &m_render_pass) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}

void FramebufferVulkanHelper::createFramebuffer() {
  for (uint32_t i = 0; i < m_color_textures.size(); i++) {
    if (m_frame_buffers[i]) {
      vkDestroyFramebuffer(m_device->getLogicalDevice(), m_frame_buffers[i], nullptr);
    }

    std::array<VkImageView, 2> attachments{m_color_textures[i].getImageView(),
                                           m_depth_textures[i].getImageView()};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_render_pass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extents.width;
    framebufferInfo.height = extents.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_device->getLogicalDevice(), &framebufferInfo, nullptr,
                            &m_frame_buffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
  }
}

VkFormat FramebufferVulkanHelper::GetSupportedDepthFormat() {
  return m_device->findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}


