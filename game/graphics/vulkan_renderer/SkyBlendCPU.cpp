#include "SkyBlendCPU.h"

#include <immintrin.h>

#include "common/util/os.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

SkyBlendCPU::SkyBlendCPU(std::shared_ptr<GraphicsDeviceVulkan> device,
                         VulkanInitializationInfo& vulkan_info)
    : m_device(device), m_vulkan_info(vulkan_info) {
  for (int i = 0; i < 2; i++) {
    m_textures[i].texture = std::make_unique<VulkanTexture>(m_device);
    VkDeviceSize texture_data = sizeof(u32) * m_sizes[i] * m_sizes[i];
    VkExtent3D extents{m_sizes[i], m_sizes[i], 1};
    m_textures[i].texture->createImage(extents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                                       VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                           VK_IMAGE_USAGE_SAMPLED_BIT);
    m_textures[i].texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}

SkyBlendCPU::~SkyBlendCPU() {}

void SkyBlendCPU::init_textures(VulkanTexturePool& tex_pool) {
  for (int i = 0; i < 2; i++) {
    m_textures[i].texture->writeToImage(m_texture_data[i].data(), m_texture_data[i].size(), 0);

    VulkanTextureInput in;
    in.texture = m_textures[i].texture.get();
    in.debug_name = fmt::format("PC-SKY-CPU-{}", i);
    in.id = tex_pool.allocate_pc_port_texture();
    u32 tbp = SKY_TEXTURE_VRAM_ADDRS[i];
    m_textures[i].tex = tex_pool.give_texture_and_load_to_vram(in, tbp);
    m_textures[i].tbp = tbp;
  }
}

void SkyBlendCPU::setup_gpu_texture(u32 slot,
                                    bool is_first_draw,
                                    u32 coord,
                                    u32 intensity,
                                    int buffer_idx,
                                    SkyBlendStats& stats) {
  // look up the source texture
  auto vulkan_texture = m_vulkan_info.texture_pool->lookup_vulkan_texture(slot);
  ASSERT(vulkan_texture);

  StagingBuffer stagingBuffer{m_device, vulkan_texture->getMemorySize(), 1,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  vulkan_texture->getImageData(stagingBuffer.getBuffer(), vulkan_texture->getWidth(),
                               vulkan_texture->getHeight(), 0, 0);
  stagingBuffer.map();
  const u8* mappedImageData = reinterpret_cast<const u8*>(stagingBuffer.getMappedMemory());

  if (mappedImageData) {
    if (m_texture_data[buffer_idx].size() == vulkan_texture->getMemorySize()) {
      if (is_first_draw) {
        blend_sky_initial_fast(intensity, m_texture_data[buffer_idx].data(), mappedImageData,
                               m_texture_data[buffer_idx].size());
      } else {
        blend_sky_fast(intensity, m_texture_data[buffer_idx].data(), mappedImageData,
                       m_texture_data[buffer_idx].size());
      }
    }

    if (buffer_idx == 0) {
      if (is_first_draw) {
        stats.sky_draws++;
      } else {
        stats.sky_blends++;
      }
    } else {
      if (is_first_draw) {
        stats.cloud_draws++;
      } else {
        stats.cloud_blends++;
      }
    }
    m_textures[buffer_idx].texture->writeToImage(m_texture_data[buffer_idx].data(),
                                                 m_texture_data[buffer_idx].size(), 0);

    m_vulkan_info.texture_pool->move_existing_to_vram(m_textures[buffer_idx].tex,
                                                      m_textures[buffer_idx].tbp);
  }
  stagingBuffer.unmap();
}
