#pragma once

#include "game/common/vu.h"
#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/ocean/CommonOceanRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanMid.h"

class OceanMidVulkan : public BaseOceanMid {
 public:
  OceanMidVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info);
  void run(DmaFollower& dma,
           BaseSharedRenderState* render_state,
           ScopedProfilerNode& prof,
           std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
           std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer);

 private:
  CommonOceanVulkanRenderer m_common_ocean_renderer;
};
