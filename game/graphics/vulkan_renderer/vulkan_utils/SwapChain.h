#pragma once

#include "game/graphics/vulkan_renderer/vulkan_utils.h"

class SwapChain {
 public:
  static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

  SwapChain(std::unique_ptr<GraphicsDeviceVulkan>& deviceRef, VkExtent2D windowExtent);
  SwapChain(std::unique_ptr<GraphicsDeviceVulkan>& deviceRef,
               VkExtent2D windowExtent,
               std::shared_ptr<SwapChain> previous);

  ~SwapChain();

  SwapChain(const SwapChain&) = delete;
  SwapChain& operator=(const SwapChain&) = delete;

  VkFramebuffer getFrameBuffer(int index) { return swapChainFramebuffers[index]; }
  VkRenderPass getRenderPass() { return renderPass; }
  VkImage getImage(int index) { return swapChainImages[index].GetImage(); }
  VkImageView getImageView(int index) { return swapChainImages[index].GetImageView(); }
  size_t imageCount() { return swapChainSourceImages.size(); }
  VkFormat getSwapChainImageFormat() { return swapChainImageFormat; }
  void setSwapChainExtent(VkExtent2D extents) { swapChainExtent = extents; };
  void setSwapChainOffsetExtent(VkOffset2D offset) { offsetSwapChainExtent = offset; };
  VkExtent2D getSwapChainExtent() { return swapChainExtent; }
  VkOffset2D getOffsetSwapChainExtent() { return offsetSwapChainExtent; }
  uint32_t width() { return swapChainExtent.width; }
  uint32_t height() { return swapChainExtent.height; }

  void recordCommandBuffer(VkCommandBuffer commandBuffer,
                           std::unique_ptr<VertexBuffer>& vertex_buffer,
                           std::unique_ptr<IndexBuffer>& index_buffer,
                           VkPipelineLayout& pipeline_layout,
                           std::vector<VkDescriptorSet>& descriptors,
                           uint32_t imageIndex);
  
  void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, uint32_t currentImageIndex);
  void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

  float extentAspectRatio() {
    return static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
  }
  VkFormat findDepthFormat();

  VkResult acquireNextImage(uint32_t* imageIndex);
  VkResult submitCommandBuffers(const VkCommandBuffer* buffers, uint32_t* imageIndex);

  bool compareSwapFormats(const SwapChain& swapChain) const {
    return swapChain.swapChainDepthFormat == swapChainDepthFormat &&
           swapChain.swapChainImageFormat == swapChainImageFormat;
  }

 private:
  void init();
  void createSwapChain();
  void createImageViews();
  void createDepthResources();
  void createRenderPass();
  void createFramebuffers();
  void createSyncObjects();

  // Helper functions
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR>& availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

  VkFormat swapChainImageFormat;
  VkFormat swapChainDepthFormat;
  VkExtent2D swapChainExtent = {640, 480};
  VkOffset2D offsetSwapChainExtent;

  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkRenderPass renderPass;

  std::vector<TextureInfo> depthImages;
  std::vector<TextureInfo> swapChainImages;
  std::vector<VkImage> swapChainSourceImages; //FIXME: Bad name

  std::unique_ptr<GraphicsDeviceVulkan>& device;
  VkExtent2D windowExtent;

  VkSwapchainKHR swapChain;
  std::shared_ptr<SwapChain> oldSwapChain;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  std::vector<VkFence> imagesInFlight;
  size_t currentFrame = 0;
};
