#pragma once

#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsDeviceVulkan.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"

class SwapChain {
 public:
  static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

  SwapChain(std::unique_ptr<GraphicsDeviceVulkan>& deviceRef, VkExtent2D windowExtent, bool vsyncEnabled);
  SwapChain(std::unique_ptr<GraphicsDeviceVulkan>& deviceRef,
               VkExtent2D windowExtent, bool vsyncEnabled,
               std::shared_ptr<SwapChain> previous);

  ~SwapChain();

  SwapChain(const SwapChain&) = delete;
  SwapChain& operator=(const SwapChain&) = delete;

  void init(bool vsyncEnabled);

  VkFramebuffer getFrameBuffer(int index) { return swapChainFramebuffers[index]; }
  VkRenderPass getRenderPass() { return renderPass; }
  VkImage getImage(int index) { return swapChainImages[index]; }
  VkImageView getImageView(int index) { return swapChainImageViews[index]; }
  size_t imageCount() { return swapChainImages.size(); }
  VkFormat getSwapChainImageFormat() { return swapChainImageFormat; }
  void setSwapChainExtent(VkExtent2D extents) { swapChainExtent = extents; };
  void setSwapChainOffsetExtent(VkOffset2D offset) { offsetSwapChainExtent = offset; };
  VkExtent2D getSwapChainExtent() { return swapChainExtent; }
  VkOffset2D getOffsetSwapChainExtent() { return offsetSwapChainExtent; }
  uint32_t width() { return swapChainExtent.width; }
  uint32_t height() { return swapChainExtent.height; }
  std::unique_ptr<GraphicsDeviceVulkan>& getLogicalDevice() { return device; };

  void drawCommandBuffer(VkCommandBuffer commandBuffer,
                       std::unique_ptr<VertexBuffer>& vertex_buffer,
                       VkPipelineLayout& pipeline_layout,
                       std::vector<VkDescriptorSet>& descriptors);

  void setupForDrawIndexedCommand(VkCommandBuffer commandBuffer,
                                  VertexBuffer* vertex_buffer,
                                  IndexBuffer* index_buffer,
                                  VkPipelineLayout& pipeline_layout,
                                  std::vector<VkDescriptorSet>& descriptors,
                                  uint32_t dynamicDescriptorCount = 0,
                                  uint32_t* dynamicDescriptorOffsets = nullptr);

  void drawIndexedCommandBuffer(VkCommandBuffer commandBuffer,
                                std::unique_ptr<VertexBuffer>& vertex_buffer,
                                std::unique_ptr<IndexBuffer>& index_buffer,
                                VkPipelineLayout& pipeline_layout,
                                std::vector<VkDescriptorSet>& descriptors,
                                uint32_t dynamicDescriptorCount = 0,
                                uint32_t* dynamicDescriptorOffsets = nullptr);

  void drawIndexedCommandBuffer(VkCommandBuffer commandBuffer,
                                VertexBuffer* vertex_buffer,
                                IndexBuffer* index_buffer,
                                VkPipelineLayout& pipeline_layout,
                                std::vector<VkDescriptorSet>& descriptors,
                                uint32_t dynamicDescriptorCount = 0,
                                uint32_t* dynamicDescriptorOffsets = nullptr);

  void multiDrawIndexedCommandBuffer(VkCommandBuffer commandBuffer,
                              VertexBuffer* vertex_buffer,
                              IndexBuffer* index_buffer,
                              VkPipelineLayout& pipeline_layout,
                              std::vector<VkDescriptorSet>& descriptors,
                              MultiDrawVulkanBuffer* multiDrawBuffer);

  void setViewportScissor(VkCommandBuffer commandBuffer);
  void clearFramebufferImage(uint32_t currentImageIndex);
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

  VkSampleCountFlagBits get_render_pass_sample_count() { return m_render_pass_sample; }
  VulkanTexture& GetColorAttachmentImageAtIndex(uint32_t index) {
    return colorImages.at(index);
  }
  VulkanTexture& GetDepthAttachmentImageAtIndex(uint32_t index) {
    return depthImages.at(index);
  }
  VkImage GetSwapChainImageAtIndex(uint32_t index) { return swapChainImages.at(index); }

 private:
  void createSwapChain(bool vsyncEnabled);
  void createImageViews();
  void createColorResources();
  void createDepthResources();
  void createRenderPass();
  void createFramebuffers(VkRenderPass);
  void createSyncObjects();

  // Helper functions
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes,
                                         VkPresentModeKHR desiredPresentMode);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

  VkFormat swapChainImageFormat;
  VkFormat swapChainDepthFormat;
  VkExtent2D swapChainExtent = {640, 480};
  VkOffset2D offsetSwapChainExtent;

  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkRenderPass noClearRenderPass = VK_NULL_HANDLE;

  std::vector<VkImage> swapChainImages;
  std::vector<VkImageView> swapChainImageViews;

  //Typically don't like using vector for VulkanTexture since std::vector calls
  // all elements copy constructors when appending/removing elements from the container
  //This is ok since it will only be set once during initialization
  std::vector<VulkanTexture> colorImages;
  std::vector<VulkanTexture> depthImages;

  std::unique_ptr<GraphicsDeviceVulkan>& device;
  VkExtent2D windowExtent;

  VkSwapchainKHR swapChain = VK_NULL_HANDLE;
  std::shared_ptr<SwapChain> oldSwapChain;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  std::vector<VkFence> imagesInFlight;

  VkSampleCountFlagBits m_render_pass_sample;

  size_t currentFrame = 0;
};
