#pragma once

#include "game/graphics/general_renderer/sprite/sprite_common.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.h"

class Sprite3dVertexUniformBuffer : public UniformVulkanBuffer {
 public:
  Sprite3dVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                              VkDeviceSize minOffsetAlignment);
};

class Sprite3dFragmentUniformBuffer : public UniformVulkanBuffer {
 public:
  Sprite3dFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                VkDeviceSize minOffsetAlignment);
};
