#include "vulkan_utils.h"

#include <cassert>
#include <array>
#include <cstdio>

#include "common/util/Assert.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

FramebufferVulkanTexturePair::FramebufferVulkanTexturePair(unsigned w,
                                                           unsigned h,
                                                           VkFormat format,
                                                           std::unique_ptr<GraphicsDeviceVulkan>& device, int num_levels)
    : m_device(device), m_sampler_helper{device} {
  extents = {w, h};

  VkSamplerCreateInfo& samplerInfo = m_sampler_helper.GetSamplerCreateInfo();
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  m_sampler_helper.CreateSampler();

  m_textures.resize(num_levels, m_device);
  for (uint32_t i = 0; i < num_levels; i++) {
    VkExtent3D textureExtents = {extents.width >> i, extents.height >> i, 1};
    //Check needed to avoid validation error. See https://vulkan-tutorial.com/Generating_Mipmaps#page_Image-creation for more info
    uint32_t maxMinmapLevels =
        static_cast<uint32_t>(std::floor(std::log2(std::max(textureExtents.width, textureExtents.height)))) + 1;
    uint32_t minmapLevel = (i + 1 > maxMinmapLevels) ? maxMinmapLevels : i + 1;
    m_textures[i].createImage(textureExtents, minmapLevel, VK_IMAGE_TYPE_2D, format,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  
    m_textures[i].createImageView(VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}




