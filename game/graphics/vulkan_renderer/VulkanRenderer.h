#pragma once

#include <array>
#include <memory>

#include "common/dma/dma_chain_read.h"

#include "game/graphics/vulkan_renderer/foreground/Generic2.h"
#include "game/graphics/vulkan_renderer/foreground/Merc2.h"
#include "game/graphics/general_renderer/renderer_utils/RenderOptions.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/CollideMeshRenderer.h"
#include "game/graphics/general_renderer/Profiler.h"
#include "game/graphics/vulkan_renderer/Shader.h"
#include "game/graphics/vulkan_renderer/FullScreenDraw.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
#include "game/tools/subtitles/subtitle_editor.h"


/*!
 * Main Vulkan renderer.
 * This handles the glClear and all game rendering, but not actual setup, synchronization or imgui
 * stuff.
 *
 * It is simply a collection of bucket renderers, and a few special case ones.
 */
class VulkanRenderer {
 public:
  VulkanRenderer(std::shared_ptr<VulkanTexturePool> texture_pool,
                 std::shared_ptr<VulkanLoader> loader,
                 GameVersion version,
                 std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~VulkanRenderer();

  // rendering interface: takes the dma chain from the game, and some size/debug settings from
  // the graphics system.
  void render(DmaFollower dma, const RenderOptions& settings);
  VkSampleCountFlagBits GetMaxUsableSampleCount() { return m_device->GetMaxUsableSampleCount(); }
  std::unique_ptr<SwapChain>& GetSwapChain() { return m_vulkan_info.swap_chain; };

  float getAspectRatio() const { return m_vulkan_info.swap_chain->extentAspectRatio(); }
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
  void dispatch_buckets(DmaFollower dma,
                        ScopedProfilerNode& prof,
                        bool sync_after_buckets);
  void dispatch_buckets_jak1(DmaFollower dma, ScopedProfilerNode& prof, bool sync_after_buckets);
  void dispatch_buckets_jak2(DmaFollower dma, ScopedProfilerNode& prof, bool sync_after_buckets);
  void do_pcrtc_effects(float alp, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);
  void init_bucket_renderers_jak1();
  void init_bucket_renderers_jak2();
  void draw_renderer_selection_window();
  void finish_screenshot(const std::string& output_name, int px, int py, int x, int y, bool quick_screenshot);
  template <typename T, typename U, class... Args>
  T* init_bucket_renderer(const std::string& name,
                          BucketCategory cat,
                          U id,
                          std::unique_ptr<GraphicsDeviceVulkan>& device,
                          VulkanInitializationInfo& vulkan_info,
                          Args&&... args) {
    auto renderer = std::make_shared<T>(name, (int)id, device, vulkan_info, std::forward<Args>(args)...);
    T* ret = renderer.get();
    m_bucket_renderers.at((int)id) = renderer;
    m_graphics_bucket_renderers.at((int)id) = renderer;
    m_bucket_categories.at((int)id) = cat;
    return ret;
  }

  std::unique_ptr<EyeVulkanRenderer> m_jak2_eye_renderer;
  GameVersion m_version;
  uint32_t currentFrame = 0;

  SharedVulkanRenderState m_render_state;
  Profiler m_profiler;
  SmallProfiler m_small_profiler;
  SubtitleEditor m_subtitle_editor;

  std::vector<std::shared_ptr<BaseBucketRenderer>> m_bucket_renderers;
  std::vector<std::shared_ptr<BucketVulkanRenderer>> m_graphics_bucket_renderers; //FIXME: Hack
  std::vector<BucketCategory> m_bucket_categories;

  std::array<float, (int)BucketCategory::MAX_CATEGORIES> m_category_times;

  float m_last_pmode_alp = 1.;
  bool m_enable_fast_blackout_loads = true;

  void createCommandBuffers();
  void freeCommandBuffers();
  void recreateSwapChain(bool vsyncEnabled);

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::vector<VkCommandBuffer> commandBuffers;

  VulkanInitializationInfo m_vulkan_info;
  std::unique_ptr<FullScreenDrawVulkan> m_blackout_renderer;
  std::shared_ptr<MercVulkan2> m_merc2;
  std::shared_ptr<GenericVulkan2> m_generic2;

  std::unique_ptr<CollideMeshVulkanRenderer> m_collide_renderer;

  uint32_t currentImageIndex;
  bool isFrameStarted = false;

  VkExtent2D m_extents = {640, 480};
};
