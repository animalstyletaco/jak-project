#include "SwapChain.h"

#include <stdexcept>

SwapChain::SwapChain(std::shared_ptr<GraphicsDeviceVulkan> deviceRef,
                     VkExtent2D extent,
                     bool vsyncEnabled)
    : device{deviceRef},
      windowExtent{extent}, swapChainExtent{extent},
      m_render_pass_sample{deviceRef->getMsaaCount()} {
  init(vsyncEnabled);
}

SwapChain::SwapChain(std::shared_ptr<GraphicsDeviceVulkan> deviceRef,
                     VkExtent2D extent,
                     bool vsyncEnabled,
                     std::shared_ptr<SwapChain> previous)
    : device{deviceRef},
      windowExtent{extent},
      swapChainExtent{extent},
      oldSwapChain{previous},
      m_render_pass_sample{deviceRef->getMsaaCount()} {
  init(vsyncEnabled);
  oldSwapChain = nullptr;
}

void SwapChain::init(bool vsyncEnabled) {
  m_render_pass_sample = device->getMsaaCount();
  createSwapChain(vsyncEnabled);
  createImageViews();
  createRenderPass();
  createColorResources();
  createDepthResources();
  createFramebuffers(renderPass);
  createSyncObjects();
}

SwapChain::~SwapChain() {
  if (swapChain != nullptr) {
    vkDestroySwapchainKHR(device->getLogicalDevice(), swapChain, nullptr);
    swapChain = nullptr;
  }

  swapChainImages.clear();
  for (auto framebuffer : swapChainFramebuffers) {
    device->destroyFramebuffer(framebuffer, nullptr);
  }

  device->destroyRenderPass(renderPass, nullptr);

  // cleanup synchronization objects
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    device->destroySemaphore(renderFinishedSemaphores[i], nullptr);
    device->destroySemaphore(imageAvailableSemaphores[i], nullptr);
    device->destroyFence(inFlightFences[i], nullptr);
  }
}

VkResult SwapChain::acquireNextImage(uint32_t* imageIndex) {
  device->waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

  VkResult result = vkAcquireNextImageKHR(
      device->getLogicalDevice(), swapChain, std::numeric_limits<uint64_t>::max(),
      imageAvailableSemaphores[currentFrame],  // must be a not signaled semaphore
      VK_NULL_HANDLE, imageIndex);

  return result;
}

VkResult SwapChain::submitCommandBuffers(const VkCommandBuffer* buffers, uint32_t* imageIndex) {
  if (imagesInFlight[*imageIndex] != VK_NULL_HANDLE) {
    device->waitForFences(1, &imagesInFlight[*imageIndex], VK_TRUE, UINT64_MAX);
  }
  imagesInFlight[*imageIndex] = inFlightFences[currentFrame];

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = buffers;

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  device->resetFences(1, &inFlightFences[currentFrame]);
  device->submitGraphicsQueue(1, &submitInfo, inFlightFences[currentFrame]);

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = imageIndex;

  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

  return vkQueuePresentKHR(device->presentQueue(), &presentInfo);
}

void SwapChain::createSwapChain(bool vsyncEnabled) {
  if (swapChain) {
    vkDestroySwapchainKHR(device->getLogicalDevice(), swapChain, nullptr);
  }

  SwapChainSupportDetails swapChainSupport = device->getSwapChainSupport();

  VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode = chooseSwapPresentMode(
      swapChainSupport.presentModes,
      (vsyncEnabled) ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = device->surface();

  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = swapChainExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  QueueFamilyIndices indices = device->findPhysicalQueueFamilies();
  if (!indices.isComplete()) {
    lg::error("Failed to initialize physical device");
    return;
  }
  uint32_t queueFamilyIndices[] = {indices.graphicsAndComputeFamily.value(), indices.presentFamily.value()};

  if (indices.graphicsAndComputeFamily.value() != indices.presentFamily.value()) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;      // Optional
    createInfo.pQueueFamilyIndices = nullptr;  // Optional
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  createInfo.oldSwapchain = oldSwapChain == nullptr ? VK_NULL_HANDLE : oldSwapChain->swapChain;

  VK_CHECK_RESULT(
      vkCreateSwapchainKHR(device->getLogicalDevice(), &createInfo, nullptr, &swapChain),
      "failed to create swap chain");

  // we only specified a minimum number of images in the swap chain, so the implementation is
  // allowed to create a swap chain with more. That's why we'll first query the final number of
  // images with vkGetSwapchainImagesKHR, then resize the container and finally call it again to
  // retrieve the handles.
  VK_CHECK_RESULT(
      vkGetSwapchainImagesKHR(device->getLogicalDevice(), swapChain, &imageCount, nullptr),
      "failed to get swapchin image count");
  swapChainImages.resize(imageCount);
  VK_CHECK_RESULT(vkGetSwapchainImagesKHR(device->getLogicalDevice(), swapChain,
                                                      &imageCount, swapChainImages.data()),
                              "failed to allocate swap chain images");

  swapChainImageFormat = surfaceFormat.format;
}

void SwapChain::createImageViews() {
  for (auto& swapChainImageView : swapChainImageViews) {
    device->destroyImageView(swapChainImageView, nullptr);
  }
  swapChainImageViews.clear();

  swapChainImageViews.resize(imageCount());
  for (int i = 0; i < imageCount(); i++) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = swapChainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapChainImageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    device->createImageView(&viewInfo, nullptr, &swapChainImageViews[i]);
  }
}

void SwapChain::createRenderPass() {
  device->destroyRenderPass(renderPass, nullptr);  

  VkSampleCountFlagBits sampleCount = device->getMsaaCount();
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = getSwapChainImageFormat();
  colorAttachment.samples = sampleCount;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription noClearColorAttachment = colorAttachment;
  noClearColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  noClearColorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription colorAttachmentResolve{};
  if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
    colorAttachmentResolve.format = getSwapChainImageFormat();
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = findDepthFormat();
  depthAttachment.samples = sampleCount;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription noClearDepthAttachment = depthAttachment;
  noClearDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  noClearDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentResolveRef{};
  VkAttachmentReference depthAttachmentRef{};

  if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
    colorAttachmentResolveRef.attachment = 1;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    depthAttachmentRef.attachment = 2;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  } else {
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pResolveAttachments =
      (sampleCount != VK_SAMPLE_COUNT_1_BIT) ? &colorAttachmentResolveRef : nullptr;
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
  std::vector<VkAttachmentDescription> noClearAttachments;
  if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
    attachments = {colorAttachment, colorAttachmentResolve, depthAttachment};
    noClearAttachments = {noClearColorAttachment, colorAttachmentResolve, noClearDepthAttachment};
  } else {
    attachments = {colorAttachment, depthAttachment};
    noClearAttachments = {noClearColorAttachment, noClearDepthAttachment};
  }

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = dependencies.size();
  renderPassInfo.pDependencies = dependencies.data();

  device->createRenderPass(&renderPassInfo, nullptr, &renderPass);

  VkRenderPassCreateInfo noClearRenderPassInfo = renderPassInfo;
  noClearRenderPassInfo.pAttachments = noClearAttachments.data();

  device->createRenderPass(&noClearRenderPassInfo, nullptr, &noClearRenderPass);
}

void SwapChain::createFramebuffers(VkRenderPass selectedRenderPass) {
  for (auto& framebuffer : swapChainFramebuffers) {
    device->destroyFramebuffer(framebuffer, nullptr);
  }
  swapChainFramebuffers.clear();
  VkSampleCountFlagBits sampleCount = device->getMsaaCount();

  swapChainFramebuffers.resize(imageCount());
  for (size_t i = 0; i < imageCount(); i++) {
    std::vector<VkImageView> attachments;
    if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
      attachments = {colorImages[i].getImageView(), swapChainImageViews[i],
                     depthImages[i].getImageView()};
    } else {
      attachments = {colorImages[i].getImageView(), depthImages[i].getImageView()};
    }

    VkExtent2D swapChainExtent = getSwapChainExtent();
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = selectedRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    device->createFramebuffer(&framebufferInfo, nullptr, &swapChainFramebuffers[i]);
  }
}

void SwapChain::createColorResources() {
  colorImages.clear();

  VkExtent2D swapChainExtent = getSwapChainExtent();

  colorImages.resize(imageCount(), device);
  for (auto& colorImage : colorImages) {
    colorImage.createImage(
        {
            swapChainExtent.width,
            swapChainExtent.height,
            1,
        },
        1, VK_IMAGE_TYPE_2D, swapChainImageFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);

    colorImage.createImageView(VK_IMAGE_VIEW_TYPE_2D, swapChainImageFormat,
                               VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}

void SwapChain::createDepthResources() {
  depthImages.clear();

  VkFormat depthFormat = findDepthFormat();
  swapChainDepthFormat = depthFormat;
  VkExtent2D swapChainExtent = getSwapChainExtent();

  depthImages.resize(imageCount(), device);
  for (auto& depthImage : depthImages) {
    depthImage.createImage(
        {
            swapChainExtent.width,
            swapChainExtent.height,
            1,
        },
        1, VK_IMAGE_TYPE_2D, depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    depthImage.createImageView(VK_IMAGE_VIEW_TYPE_2D, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
  }
}

void SwapChain::createSyncObjects() {
  for (unsigned i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    device->destroySemaphore(imageAvailableSemaphores[i], nullptr);
    device->destroySemaphore(renderFinishedSemaphores[i], nullptr);
    device->destroyFence(inFlightFences[i], nullptr);
  }

  imageAvailableSemaphores.clear();
  renderFinishedSemaphores.clear();
  inFlightFences.clear();
  imagesInFlight.clear();

  imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
  imagesInFlight.resize(imageCount(), VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    device->createSemaphore(&semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
    device->createSemaphore(&semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
    device->createFences(&fenceInfo, nullptr, &inFlightFences[i]);
  }
}

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
  for (const auto& availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }

  return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes,
    VkPresentModeKHR presentMode) {
  for (const auto& availablePresentMode : availablePresentModes) {
    if (availablePresentMode == presentMode) {
      lg::info("Present mode: Mailbox");
      return availablePresentMode;
    }
  }

  lg::info("Present mode: V-Sync");
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } 

  VkExtent2D actualExtent = windowExtent;
  actualExtent.width = std::max(capabilities.minImageExtent.width,
                                  std::min(capabilities.maxImageExtent.width, actualExtent.width));
  actualExtent.height =
        std::max(capabilities.minImageExtent.height,
                 std::min(capabilities.maxImageExtent.height, actualExtent.height));

  return actualExtent;
}

VkFormat SwapChain::findDepthFormat() {
  return device->findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
       VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void SwapChain::clearFramebufferImage(uint32_t currentImageIndex) {
  VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = getFrameBuffer(currentImageIndex);

  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = getSwapChainExtent();

  std::vector<VkClearValue> clearValues;
  if (device->getMsaaCount() != VK_SAMPLE_COUNT_1_BIT) {
    clearValues.resize(3);
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[2].depthStencil = {
        0.0f, 1};  // Double check to see if this is correct. Clear value for depth is normally 1
  } else {
    clearValues.resize(2);
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = {
        0.0f, 1};  // Double check to see if this is correct. Clear value for depth is normally 1
  }

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(commandBuffer);
  device->endSingleTimeCommands(commandBuffer);

  createFramebuffers(noClearRenderPass);
}

void SwapChain::beginSwapChainRenderPass(VkCommandBuffer commandBuffer,
                                         uint32_t currentImageIndex,
                                         VkSubpassContents contents) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = noClearRenderPass;
  renderPassInfo.framebuffer = getFrameBuffer(currentImageIndex);

  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = getSwapChainExtent();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, contents);
}

void SwapChain::setViewportScissor(VkCommandBuffer commandBuffer) {
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapChainExtent.width);
  viewport.height = static_cast<float>(swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{{0, 0}, swapChainExtent};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void SwapChain::endSwapChainRenderPass(VkCommandBuffer commandBuffer) {
  vkCmdEndRenderPass(commandBuffer);
}
