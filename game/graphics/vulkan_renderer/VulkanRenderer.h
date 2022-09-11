#pragma once

#include <array>
#include <memory>

#include "common/dma/dma_chain_read.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/CollideMeshRenderer.h"
#include "game/graphics/vulkan_renderer/Profiler.h"
#include "game/graphics/vulkan_renderer/Shader.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/tools/subtitles/subtitle_editor.h"

struct RenderOptions {
  bool draw_render_debug_window = false;
  bool draw_profiler_window = false;
  bool draw_small_profiler_window = false;
  bool draw_subtitle_editor_window = false;

  // internal rendering settings - The OpenGLRenderer will internally use this resolution/format.
  int msaa_samples = 4;
  int game_res_w = 640;
  int game_res_h = 480;

  // size of the window's framebuffer (framebuffer 0)
  // The renderer needs to know this to do an optimization to render directly to the window's
  // framebuffer when possible.
  int window_framebuffer_height = 0;
  int window_framebuffer_width = 0;

  // the part of the window that we should draw to. The rest is black. This value is determined by
  // logic inside of the game - it needs to know the desired aspect ratio.
  int draw_region_height = 0;
  int draw_region_width = 0;

  // windows-specific tweaks to the size of the drawing area in borderless.
  bool borderless_windows_hacks = false;

  bool save_screenshot = false;
  std::string screenshot_path;

  float pmode_alp_register = 0.f;

  // when enabled, does a `glFinish()` after each major rendering pass. This blocks until the GPU
  // is done working, making it easier to profile GPU utilization.
  bool gpu_sync = false;
};

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> presentFamily;

  bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

/*!
 * Main Vulkan renderer.
 * This handles the glClear and all game rendering, but not actual setup, synchronization or imgui
 * stuff.
 *
 * It is simply a collection of bucket renderers, and a few special case ones.
 */
class VulkanRenderer {
 public:
  VulkanRenderer(std::shared_ptr<TexturePool> texture_pool, std::shared_ptr<Loader> loader);
  ~VulkanRenderer();

  // rendering interface: takes the dma chain from the game, and some size/debug settings from
  // the graphics system.
  void render(DmaFollower dma, const RenderOptions& settings);

  VkSampleCountFlagBits GetMaxUsableSampleCount();

  VkInstance GetInstance() const { return instance; };
  VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; };
  VkRenderPass GetRendererPass() const { return renderPass; };

  VkDevice GetLogicalDevice() const { return device; };
  VkQueue GetPresentQueue() const { return presentQueue; };
  VkDescriptorPool GetDescriptorPool() const { return descriptorPool; };
  uint32_t GetQueueFamily() const { return 0; };

 private:
  void setup_frame(const RenderOptions& settings);
  void dispatch_buckets(DmaFollower dma, ScopedProfilerNode& prof, bool sync_after_buckets);
  void do_pcrtc_effects(float alp, SharedRenderState* render_state, ScopedProfilerNode& prof);
  void init_bucket_renderers();
  void draw_renderer_selection_window();
  void finish_screenshot(const std::string& output_name, int px, int py, int x, int y);
  template <typename T, class... Args>
  T* init_bucket_renderer(const std::string& name,
                          BucketCategory cat,
                          BucketId id,
                          Args&&... args) {
    auto renderer = std::make_unique<T>(name, id, std::forward<Args>(args)...);
    T* ret = renderer.get();
    m_bucket_renderers.at((int)id) = std::move(renderer);
    m_bucket_categories.at((int)id) = cat;
    return ret;
  }

  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSwapChain();
  void createImageViews();
  void createRenderPass();
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  void createCommandPool();
  void createColorResources();
  void createDepthResources();
  void createFramebuffers();
  void createTextureImage();
  void createTextureImageView();
  void createTextureSampler();
  void loadModel();
  void createVertexBuffer();
  void createIndexBuffer();
  void createUniformBuffers();
  void createDescriptorPool();
  void createDescriptorSets();
  void createCommandBuffers();
  void createSyncObjects();

  void drawFrame();
  void updateUniformBuffer(uint32_t currentImage);
  void recreateSwapChain();
  void cleanupSwapChain();
  void cleanup();
  SwapChainSupportDetails QuerySwapChainSupport();
  void populateDebugMessengerCreateInfo(
      VkDebugUtilsMessengerCreateInfoEXT& createInfo);
  void CopyBufferToImage(VkBuffer buffer,
                         VkImage image,
                         uint32_t width,
                         uint32_t height,
                         uint32_t x_offset = 0,
                         uint32_t y_offset = 0);

  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memoryProperties;
  VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
  VkDevice device;

  VkQueue graphicsQueue;
  VkQueue presentQueue;

  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkRenderPass renderPass;
  VkDescriptorSetLayout descriptorSetLayout;
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;

  VkCommandPool commandPool;

  VkImage colorImage;
  VkDeviceMemory colorImageMemory;
  VkImageView colorImageView;

  VkImage depthImage;
  VkDeviceMemory depthImageMemory;
  VkImageView depthImageView;

  uint32_t mipLevels;
  VkImage textureImage;
  VkDeviceMemory textureImageMemory;
  VkImageView textureImageView;
  VkSampler textureSampler;

//  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;

  std::vector<VkBuffer> uniformBuffers;
  std::vector<VkDeviceMemory> uniformBuffersMemory;

  VkDescriptorPool descriptorPool;
  std::vector<VkDescriptorSet> descriptorSets;

  std::vector<VkCommandBuffer> commandBuffers;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  uint32_t currentFrame = 0;

  bool framebufferResized = false;

  SharedRenderState m_render_state;
  Profiler m_profiler;
  SmallProfiler m_small_profiler;
  SubtitleEditor m_subtitle_editor;

  std::array<std::unique_ptr<BucketRenderer>, (int)BucketId::MAX_BUCKETS> m_bucket_renderers;
  std::array<BucketCategory, (int)BucketId::MAX_BUCKETS> m_bucket_categories;

  std::array<float, (int)BucketCategory::MAX_CATEGORIES> m_category_times;
  FullScreenDraw m_blackout_renderer;
  CollideMeshRenderer m_collide_renderer;

  float m_last_pmode_alp = 1.;
  bool m_enable_fast_blackout_loads = true;
};
