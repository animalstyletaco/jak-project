#pragma once

#include "game/graphics/general_renderer/ocean/OceanNear.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/OceanTexture.h"

class OceanNearVulkan : public BaseOceanNear, public BucketVulkanRenderer {
 public:
  OceanNearVulkan(const std::string& name,
            int my_id,
            std::unique_ptr<GraphicsDeviceVulkan>& device,
            VulkanInitializationInfo& vulkan_info);
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override;
  void init_textures(TexturePoolVulkan& pool) override;

 private:
  OceanVulkanTexture m_texture_renderer;
  CommonOceanVulkanRenderer m_common_ocean_renderer;
  std::unique_ptr<CommonOceanVertexUniformBuffer> m_ocean_vertex_uniform_buffer;
  std::unique_ptr<CommonOceanFragmentUniformBuffer> m_ocean_fragment_uniform_buffer;
};
