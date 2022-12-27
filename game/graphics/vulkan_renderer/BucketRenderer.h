#pragma once

#include <memory>
#include <string>

#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/Profiler.h"
#include "game/graphics/vulkan_renderer/Shader.h"
#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/loader/Loader.h"
#include "game/graphics/texture/VulkanTexturePool.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/DescriptorLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/SwapChain.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"

class EyeVulkanRenderer;
/*!
 * The main renderer will contain a single SharedRenderState that's passed to all bucket renderers.
 * This allows bucket renders to share textures and shaders.
 */
struct SharedVulkanRenderState : public BaseSharedRenderState {
  explicit SharedVulkanRenderState(GameVersion version,
                                   std::unique_ptr<GraphicsDeviceVulkan>& device)
      : BaseSharedRenderState(version) {}
  VkFramebuffer render_fb = VK_NULL_HANDLE;
  EyeVulkanRenderer* eye_renderer = nullptr;
};

struct VulkanInitializationInfo {
  VulkanInitializationInfo(std::unique_ptr<GraphicsDeviceVulkan>& device, GameVersion version) : shaders(device, version){};

  std::unique_ptr<DescriptorPool> descriptor_pool;
  std::unique_ptr<SwapChain> swap_chain;
  VulkanShaderLibrary shaders;
  std::shared_ptr<VulkanTexturePool> texture_pool;
  std::shared_ptr<VulkanLoader> loader;
  VkCommandBuffer render_command_buffer;
};

/*!
 * Interface for bucket renders. Each bucket will have its own BucketRenderer.
 */
class BucketVulkanRenderer {
 public:
  BucketVulkanRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                       VulkanInitializationInfo& vulkan_info)
      : m_device(device),
        m_vulkan_info(vulkan_info) {
    GraphicsPipelineLayout graphicsPipelineLayout{m_device};
    graphicsPipelineLayout.defaultPipelineConfigInfo(m_pipeline_config_info);
  }

  virtual void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) = 0;
  virtual void init_textures(VulkanTexturePool& pool){};
  virtual void init_shaders(VulkanShaderLibrary& pool){};
  virtual void create_pipeline_layout(){};

 protected:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;

  VulkanInitializationInfo& m_vulkan_info;

  std::vector<GraphicsPipelineLayout> m_pipeline_layouts;
  PipelineConfigInfo m_pipeline_config_info;

  VkDescriptorBufferInfo m_vertex_buffer_descriptor_info;
  VkDescriptorBufferInfo m_fragment_buffer_descriptor_info;

  std::unique_ptr<DescriptorLayout> m_vertex_descriptor_layout;
  std::unique_ptr<DescriptorLayout> m_fragment_descriptor_layout;

  std::unique_ptr<DescriptorWriter> m_vertex_descriptor_writer;
  std::unique_ptr<DescriptorWriter> m_fragment_descriptor_writer;

  std::vector<VkDescriptorSet> m_descriptor_sets;
};

class RenderVulkanMux : public BucketVulkanRenderer, public BaseRenderMux {
 public:
  RenderVulkanMux(const std::string& name,
                  int my_id,
                  std::unique_ptr<GraphicsDeviceVulkan>& device,
                  VulkanInitializationInfo& vulkan_info,
                  std::vector<std::shared_ptr<BucketVulkanRenderer>> renderers,
                  std::vector<std::shared_ptr<BaseBucketRenderer>> bucket_renderers);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_textures(VulkanTexturePool& tp) override;

  private:
   std::vector<std::shared_ptr<BucketVulkanRenderer>> m_graphics_renderers;
   std::vector<std::shared_ptr<BaseBucketRenderer>> m_bucket_renderers;
};

/*!
 * Renderer that makes sure the bucket is empty and ignores it.
 */
class EmptyBucketVulkanRenderer : public BucketVulkanRenderer, public BaseEmptyBucketRenderer {
 public:
  EmptyBucketVulkanRenderer(const std::string& name,
                            int my_id,
                            std::unique_ptr<GraphicsDeviceVulkan>& device,
                            VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
};

class SkipVulkanRenderer : public BucketVulkanRenderer, public BaseSkipRenderer {
 public:
  SkipVulkanRenderer(const std::string& name,
                     int my_id,
                     std::unique_ptr<GraphicsDeviceVulkan>& device,
                     VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma,
              SharedVulkanRenderState* render_state,
              ScopedProfilerNode& prof) override;
};
