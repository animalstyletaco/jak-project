#include "TexturePoolVulkan.h"

#include <algorithm>
#include <regex>

#include "common/log/log.h"
#include "common/util/Assert.h"
#include "common/util/Timer.h"

#include "game/graphics/texture/jak1_tpage_dir.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

TexturePoolVulkan::TexturePoolVulkan(GameVersion version,
                                     std::unique_ptr<GraphicsDeviceVulkan>& device)
    : BaseTexturePool(version), m_device(device), m_placeholder_texture(device) {
  m_placeholder_data.resize(16 * 16);
  u32 c0 = 0xa0303030;
  u32 c1 = 0xa0e0e0e0;
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      m_placeholder_data[i * 16 + j] = (((i / 4) & 1) ^ ((j / 4) & 1)) ? c1 : c0;
    }
  }
  upload_to_gpu((const u8*)(m_placeholder_data.data()), 16, 16, m_placeholder_texture);
}

void TexturePoolVulkan::upload_to_gpu(const u8* data, u16 w, u16 h, VulkanTexture& texture) {
  // TODO: Get Mipmap Level here
  unsigned mipLevels = 1;

  VkExtent3D extents{w, h, 1};
  texture.createImage(extents, mipLevels, VK_IMAGE_TYPE_2D, m_device->getMsaaCount(),
                      VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_LINEAR,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT);

  texture.writeToImage((u8*)data);

  texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

  // Max Anisotropy is set in vulkan renderer sampler info;
}

GpuTexture* TexturePoolVulkan::give_texture(const TextureInput& in) {
  // const auto& it = m_loaded_textures.find(in.name);
  const auto existing = m_loaded_textures.lookup_or_insert(in.id);
  if (!existing.second) {
    // nothing references this texture yet.
    existing.first->tex_id = in.id;
    existing.first->w = in.w;
    existing.first->h = in.h;
    existing.first->is_common = in.common;
    existing.first->gpu_textures = {{in.gpu_texture, in.src_data}};
    existing.first->is_placeholder = false;
    *m_id_to_name.lookup_or_insert(in.id).first =
        fmt::format("{}/{}", in.debug_page_name, in.debug_name);
    return existing.first;
  } else {
    if (!existing.first->is_placeholder) {
      // two sources for texture. this is fine.
      ASSERT(!existing.first->gpu_textures.empty());
    } else {
      ASSERT(existing.first->gpu_textures.empty());
    }
    existing.first->is_placeholder = false;
    existing.first->w = in.w;
    existing.first->h = in.h;
    existing.first->gpu_textures.push_back({in.gpu_texture, in.src_data});
    existing.first->is_common = in.common;
    refresh_links(*existing.first);
    return existing.first;
  }
}

GpuTexture* TexturePoolVulkan::give_texture_and_load_to_vram(const TextureInput& in, u32 vram_slot) {
  auto tex = give_texture(in);
  move_existing_to_vram(tex, vram_slot);
  return tex;
}

void TexturePoolVulkan::refresh_links(GpuTexture& texture) {
  VulkanTexture* tex_to_use =
      texture.is_placeholder ? &m_placeholder_texture : (VulkanTexture*)texture.gpu_textures.front().imageId;

  for (auto slot : texture.slots) {
    auto& t = m_textures[slot];
    ASSERT(t.source == &texture);
    t.gpu_texture = (u64)tex_to_use;
  }

  for (auto slot : texture.mt4hh_slots) {
    for (auto& tex : m_mt4hh_textures) {
      if (tex.slot == slot) {
        tex.ref.gpu_texture = (u64)tex_to_use;
      }
    }
  }
}

void TexturePoolVulkan::unload_texture(PcTextureId tex_id, VulkanTexture& gpu_id) {
  auto* tex = m_loaded_textures.lookup_existing(tex_id);
  ASSERT(tex);
  if (tex->is_common) {
    ASSERT(false);
    return;
  }
  if (tex->is_placeholder) {
    fmt::print("trying to unload something that was already placholdered: {} {}\n",
               get_debug_texture_name(tex_id), tex->gpu_textures.size());
  }
  ASSERT(!tex->is_placeholder);
  auto it = std::find_if(tex->gpu_textures.begin(), tex->gpu_textures.end(),
                         [&](const auto& a) { return a.imageId == (u64)&gpu_id; });
  ASSERT(it != tex->gpu_textures.end());

  tex->gpu_textures.erase(it);
  if (tex->gpu_textures.empty()) {
    tex->is_placeholder = true;
  }
  refresh_links(*tex);
}

GpuTexture* TexturePoolVulkan::get_gpu_texture_for_slot(PcTextureId id, u32 slot) {
  auto it = m_loaded_textures.lookup_or_insert(id);
  if (!it.second) {
    GpuTexture& placeholder = *it.first;
    placeholder.tex_id = id;
    placeholder.is_placeholder = true;
    placeholder.slots.push_back(slot);

    // auto r = m_loaded_textures.insert({name, placeholder});
    m_textures[slot].gpu_texture = (u64)&m_placeholder_texture;
    return it.first;
  } else {
    auto result = it.first;
    result->add_slot(slot);
    m_textures[slot].gpu_texture =
        result->is_placeholder ? (u64)&m_placeholder_texture : result->gpu_textures.at(0).imageId;
    return result;
  }
}

VulkanTexture* TexturePoolVulkan::lookup_vulkan_texture(u32 location) {
  auto tex = BaseTexturePool::lookup(location);
  return (tex) ? m_vulkan_textures[tex.value()] : nullptr;
}

/*!
 * Look up a game texture by VRAM address. Will be nullptr if the game hasn't loaded anything to
 * this address.
 *
 * You should probably not use this to lookup textures that could be uploaded with
 * handle_upload_now.
 */
VulkanTexture* TexturePoolVulkan::lookup_gpu_vulkan_texture(u32 location) {
  return m_vulkan_textures[location];
}
VulkanTexture* TexturePoolVulkan::lookup_mt4hh_texture(u32 location) {
  auto tex = BaseTexturePool::lookup_mt4hh(location);
  return (tex) ? m_vulkan_textures[tex.value()] : nullptr;
};
