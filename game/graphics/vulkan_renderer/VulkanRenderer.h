#pragma once

#include <array>
#include <memory>

#include "common/dma/dma_chain_read.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/CollideMeshRenderer.h"
#include "game/graphics/vulkan_renderer/Profiler.h"
#include "game/graphics/vulkan_renderer/Shader.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
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

/*!
 * Main Vulkan renderer.
 * This handles the glClear and all game rendering, but not actual setup, synchronization or imgui
 * stuff.
 *
 * It is simply a collection of bucket renderers, and a few special case ones.
 */
class VulkanRenderer {
 public:
  VulkanRenderer(std::shared_ptr<TexturePool> texture_pool, std::shared_ptr<Loader> loader, std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~VulkanRenderer();

  // rendering interface: takes the dma chain from the game, and some size/debug settings from
  // the graphics system.
  void render(DmaFollower dma, const RenderOptions& settings);
  VkInstance GetInstance() { return m_device->getInstance(); }
  VkPhysicalDevice GetPhysicalDevice() { return m_device->getPhysicalDevice(); }
  VkDevice GetLogicalDevice() { return m_device->getLogicalDevice(); }
  VkDescriptorPool GetDescriptorPool() { return m_descriptor_pool->getDescriptorPool(); }
  VkQueue GetPresentQueue() { return m_device->presentQueue(); }
  QueueFamilyIndices GetPhysicalQueueFamilies() { return m_device->findPhysicalQueueFamilies(); }
  VkSampleCountFlagBits GetMaxUsableSampleCount() { return m_device->GetMaxUsableSampleCount(); }

  VkRenderPass getSwapChainRenderPass() const { return m_swap_chain->getRenderPass(); }
  float getAspectRatio() const { return m_swap_chain->extentAspectRatio(); }
  bool isFrameInProgress() const { return isFrameStarted; }

  VkCommandBuffer getCurrentCommandBuffer() const {
    assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
    return commandBuffers[currentFrame];
  }

  int getFrameIndex() const {
    assert(isFrameStarted && "Cannot get frame index when frame not in progress");
    return currentFrame;
  }

  VkCommandBuffer beginFrame();
  void endFrame();

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
                          std::unique_ptr<GraphicsDeviceVulkan>& device,
                          VulkanInitializationInfo& vulkan_info,
                          Args&&... args) {
    auto renderer = std::make_unique<T>(name, id, device, vulkan_info, std::forward<Args>(args)...);
    T* ret = renderer.get();
    m_bucket_renderers.at((int)id) = std::move(renderer);
    m_bucket_categories.at((int)id) = cat;
    return ret;
  }

  uint32_t currentFrame = 0;

  SharedRenderState m_render_state;
  Profiler m_profiler;
  SmallProfiler m_small_profiler;
  SubtitleEditor m_subtitle_editor;

  std::array<std::unique_ptr<BucketRenderer>, (int)BucketId::MAX_BUCKETS> m_bucket_renderers;
  std::array<BucketCategory, (int)BucketId::MAX_BUCKETS> m_bucket_categories;

  std::array<float, (int)BucketCategory::MAX_CATEGORIES> m_category_times;

  float m_last_pmode_alp = 1.;
  bool m_enable_fast_blackout_loads = true;

  void createCommandBuffers();
  void freeCommandBuffers();
  void recreateSwapChain();

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unique_ptr<SwapChain> m_swap_chain;
  std::unique_ptr<DescriptorPool> m_descriptor_pool;
  std::vector<VkCommandBuffer> commandBuffers;
  FullScreenDraw m_blackout_renderer{m_device};
  CollideMeshRenderer m_collide_renderer{m_device};

  uint32_t currentImageIndex;
  bool isFrameStarted = false;

  VulkanInitializationInfo vulkan_info;
  VkExtent2D m_extents = {640, 480};
};
