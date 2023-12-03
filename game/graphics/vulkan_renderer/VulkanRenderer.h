#pragma once

#include <array>
#include <memory>

#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/Profiler.h"
#include "game/graphics/general_renderer/renderer_utils/RenderOptions.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/CollideMeshRenderer.h"
#include "game/graphics/vulkan_renderer/FullScreenDraw.h"
#include "game/graphics/vulkan_renderer/Shader.h"
#include "game/graphics/vulkan_renderer/TextureAnimator.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"
#include "game/graphics/vulkan_renderer/foreground/Merc2.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
#include "game/tools/subtitle_editor/subtitle_editor.h"

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
                 std::shared_ptr<GraphicsDeviceVulkan> device);
  virtual ~VulkanRenderer();

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

 protected:
  void setup_frame(const RenderOptions& settings);
  virtual void dispatch_buckets(DmaFollower dma,
                                ScopedProfilerNode& prof,
                                bool sync_after_buckets) = 0;
  void do_pcrtc_effects(float alp, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof);
  virtual void init_bucket_renderers() = 0;
  virtual void draw_renderer_selection_window();
  void imgui_draw_selection_window();
  void finish_screenshot(const std::string& output_name,
                         int px,
                         int py,
                         int x,
                         int y,
                         bool quick_screenshot);
  template <typename T, typename U, class... Args>
  T* init_bucket_renderer(const std::string& name,
                          BucketCategory cat,
                          U id,
                          std::shared_ptr<GraphicsDeviceVulkan> device,
                          VulkanInitializationInfo& vulkan_info,
                          Args&&... args) {
    auto renderer =
        std::make_shared<T>(name, (int)id, device, vulkan_info, std::forward<Args>(args)...);
    T* ret = renderer.get();
    m_bucket_renderers.at((int)id) = renderer;
    m_graphics_bucket_renderers.at((int)id) = renderer;
    m_bucket_categories.at((int)id) = cat;
    return ret;
  }

  VulkanTextureAnimator* m_texture_animator = nullptr;
  const std::vector<VulkanTexture*>* anim_slot_array() {
    return m_texture_animator ? m_texture_animator->slots() : nullptr;
  }

  uint32_t currentFrame = 0;

  Profiler m_profiler;
  SmallProfiler m_small_profiler;
  SubtitleEditor m_subtitle_editor;

  std::vector<std::shared_ptr<BaseBucketRenderer>> m_bucket_renderers;
  std::vector<std::shared_ptr<BucketVulkanRenderer>> m_graphics_bucket_renderers;  // FIXME: Hack
  std::vector<BucketCategory> m_bucket_categories;

  std::array<float, (int)BucketCategory::MAX_CATEGORIES> m_category_times;

  float m_last_pmode_alp = 1.;
  bool m_enable_fast_blackout_loads = true;

  void createCommandBuffers();
  void freeCommandBuffers();
  void recreateSwapChain(bool vsyncEnabled);

  std::shared_ptr<GraphicsDeviceVulkan> m_device;
  std::vector<VkCommandBuffer> commandBuffers;

  VulkanInitializationInfo m_vulkan_info;
  std::unique_ptr<FullScreenDrawVulkan> m_blackout_renderer;

  std::unique_ptr<CollideMeshVulkanRenderer> m_collide_renderer;

  uint32_t currentImageIndex;
  bool isFrameStarted = false;

  VkExtent2D m_extents = {640, 480};
  virtual SharedVulkanRenderState& GetSharedVulkanRenderState() = 0;
};

class VulkanRendererJak1 : public VulkanRenderer {
 public:
  VulkanRendererJak1(std::shared_ptr<VulkanTexturePool> texture_pool,
                     std::shared_ptr<VulkanLoader> loader,
                     std::shared_ptr<GraphicsDeviceVulkan> device)
      : VulkanRenderer(texture_pool, loader, device), m_render_state(device) {
    m_merc2 = std::make_shared<MercVulkan2Jak1>(device, m_vulkan_info);
    m_generic2 = std::make_shared<GenericVulkan2Jak1>(device, m_vulkan_info);
    init_bucket_renderers();
  };
  ~VulkanRendererJak1() = default;

 protected:
  SharedVulkanRenderState& GetSharedVulkanRenderState() override { return m_render_state; };
  void dispatch_buckets(DmaFollower dma,
                        ScopedProfilerNode& prof,
                        bool sync_after_buckets) override;
  void init_bucket_renderers() override;

  SharedVulkanRenderStateJak1 m_render_state;

  std::shared_ptr<MercVulkan2Jak1> m_merc2;
  std::shared_ptr<GenericVulkan2Jak1> m_generic2;
};

class VulkanRendererJak2 : public VulkanRenderer {
 public:
  VulkanRendererJak2(std::shared_ptr<VulkanTexturePool> texture_pool,
                     std::shared_ptr<VulkanLoader> loader,
                     std::shared_ptr<GraphicsDeviceVulkan> device)
      : VulkanRenderer(texture_pool, loader, device), m_render_state(device) {
    m_merc2 = std::make_shared<MercVulkan2>(device, m_vulkan_info);
    m_generic2 = std::make_shared<GenericVulkan2Jak2>(device, m_vulkan_info);
    init_bucket_renderers();
  };
  ~VulkanRendererJak2() = default;

 protected:
  void dispatch_buckets(DmaFollower dma,
                        ScopedProfilerNode& prof,
                        bool sync_after_buckets) override;
  void init_bucket_renderers() override;
  void draw_renderer_selection_window() override;

  SharedVulkanRenderState& GetSharedVulkanRenderState() override { return m_render_state; };
  SharedVulkanRenderStateJak2 m_render_state;

  std::shared_ptr<EyeVulkanRenderer> m_jak2_eye_renderer;

  std::shared_ptr<MercVulkan2> m_merc2;
  std::shared_ptr<GenericVulkan2Jak2> m_generic2;
};
