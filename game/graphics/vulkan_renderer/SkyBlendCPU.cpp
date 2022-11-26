#include "SkyBlendCPU.h"

#include <immintrin.h>

#include "common/util/os.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

SkyBlendCPU::SkyBlendCPU(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info) : m_device(device), m_vulkan_info(vulkan_info) {
  for (int i = 0; i < 2; i++) {
    m_textures[i].texture = std::make_unique<VulkanTexture>(m_device);
    VkDeviceSize texture_data = sizeof(u32) * m_sizes[i] * m_sizes[i];
    VkExtent3D extents{m_sizes[i], m_sizes[i], 1};
    m_textures[i].texture->createImage(extents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                            VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_LINEAR,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    m_textures[i].texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_A8B8G8R8_SINT_PACK32,
                                 VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}

SkyBlendCPU::~SkyBlendCPU() {
}

void SkyBlendCPU::init_textures(TexturePoolVulkan& tex_pool) {
  for (int i = 0; i < 2; i++) {
    m_textures[i].texture->writeToImage(m_texture_data[i].data(), m_texture_data[i].size(), 0);

    TextureInput in;
    in.gpu_texture = (u64)m_textures[i].texture.get();
    in.w = m_sizes[i];
    in.h = m_sizes[i];
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
  auto tex = m_vulkan_info.texture_pool->lookup_gpu_vulkan_texture(slot);
  ASSERT(tex);

  VulkanBuffer imageDataBuffer{
      m_device, tex->getMemorySize(), 1, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
  tex->getImageData(imageDataBuffer.getBuffer(), tex->getWidth(), tex->getHeight(), 0, 0);

  const u8* mappedImageData = reinterpret_cast<const u8*>(imageDataBuffer.getMappedMemory());

  if (mappedImageData) {
    if (m_texture_data[buffer_idx].size() == tex->getMemorySize()) {
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
}
