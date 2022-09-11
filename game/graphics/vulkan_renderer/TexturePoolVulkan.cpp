#include "TexturePoolVulkan.h"

#include <algorithm>
#include <regex>

#include "common/log/log.h"
#include "common/util/Assert.h"
#include "common/util/Timer.h"

#include "game/graphics/pipelines/vulkan.h"
#include "game/graphics/texture/jak1_tpage_dir.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

namespace {
const char empty_string[] = "";
const char* goal_string(u32 ptr, const u8* memory_base) {
  if (ptr == 0) {
    return empty_string;
  }
  return (const char*)(memory_base + ptr + 4);
}

}  // namespace

void TextureInfo::DestroyTexture() {
  if (!device) {
    return;
  }
  if (texture_view) {
    vkDestroyImageView(device, texture_view, nullptr);
  }
  if (texture) {
    vkDestroyImage(device, texture, nullptr);
  }
  if (texture_device_memory) {
    vkFreeMemory(device, texture_device_memory, nullptr);
  }

  device_size = 0;
};

uint32_t TextureInfo::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void TextureInfo::CreateImage(unsigned width,
                              unsigned height,
                              unsigned mipLevels,
                              VkImageType image_type,
                              VkSampleCountFlagBits sample_count,
                              VkFormat format) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  imageInfo.samples = sample_count;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &texture) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, texture, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &texture_device_memory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(device, texture, texture_device_memory, 0);
}

template <class T>
void TextureInfo::UpdateTexture(VkDeviceSize memory_offset, T* value, uint64_t element_count) {
  uint64_t memory_size = element_count * sizeof(*value);

  void* data = NULL;
  vkMapMemory(device, texture_device_memory, 0, device_size, 0, &data);
  ::memset(data, value, memory_size);
  vkUnmapMemory(device, texture_device_memory, nullptr);
}

std::string GoalTexturePage::print() const {
  return fmt::format("Tpage id {} textures {} seg0 {} {} seg1 {} {} seg2 {} {}\n", id, length,
                     segment[0].size, segment[0].dest, segment[1].size, segment[1].dest,
                     segment[2].size, segment[2].dest);
}

TextureInfo TexturePool::upload_to_gpu(const u8* data, u16 w, u16 h) {
  TextureInfo textureInfo;
  textureInfo.device_size = w * h * 4;

  vulkan_utils::CreateImage(m_device, w, h, 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureInfo.texture,
                            textureInfo.texture_device_memory);

  void* mapped_data = NULL;
  vkMapMemory(m_device, textureInfo.texture_device_memory, 0, textureInfo.device_size, 0, &mapped_data);
  ::memcpy(mapped_data, data, textureInfo.device_size);
  vkUnmapMemory(m_device, textureInfo.texture_device_memory);

  // TODO: Get Mipmap Level here

  unsigned mipLevels = 1;
  textureInfo.texture_view = vulkan_utils::CreateImageView(m_device, textureInfo.texture, VK_FORMAT_A8B8G8R8_SINT_PACK32,
                                                  VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
  // Max Anisotropy is set in vulkan renderer sampler info;

  return textureInfo;
}

GpuTexture* TexturePool::give_texture(const TextureInput& in) {
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

GpuTexture* TexturePool::give_texture_and_load_to_vram(const TextureInput& in, u32 vram_slot) {
  auto tex = give_texture(in);
  move_existing_to_vram(tex, vram_slot);
  return tex;
}

void TexturePool::move_existing_to_vram(GpuTexture* tex, u32 slot_addr) {
  ASSERT(!tex->is_placeholder);
  ASSERT(!tex->gpu_textures.empty());
  auto& slot = m_textures[slot_addr];
  if (std::find(tex->slots.begin(), tex->slots.end(), slot_addr) == tex->slots.end()) {
    tex->slots.push_back(slot_addr);
  }
  if (slot.source) {
    if (slot.source == tex) {
      // we already have it, no need to do anything
    } else {
      slot.source->remove_slot(slot_addr);
      slot.source = tex;
      slot.gpu_texture = tex->gpu_textures.front().image;
    }
  } else {
    slot.source = tex;
    slot.gpu_texture = tex->gpu_textures.front().image;
  }
}

void TexturePool::refresh_links(GpuTexture& texture) {
  VkImage tex_to_use =
      texture.is_placeholder ? m_placeholder_texture_id : texture.gpu_textures.front().image;

  for (auto slot : texture.slots) {
    auto& t = m_textures[slot];
    ASSERT(t.source == &texture);
    t.gpu_texture = tex_to_use;
  }

  for (auto slot : texture.mt4hh_slots) {
    for (auto& tex : m_mt4hh_textures) {
      if (tex.slot == slot) {
        tex.ref.gpu_texture = tex_to_use;
      }
    }
  }
}

void TexturePool::unload_texture(PcTextureId tex_id, const TextureInfo& gpu_id) {
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
                         [&](const auto& a) { return a.image == gpu_id.texture; });
  ASSERT(it != tex->gpu_textures.end());

  tex->gpu_textures.erase(it);
  if (tex->gpu_textures.empty()) {
    tex->is_placeholder = true;
  }
  refresh_links(*tex);
}

void GpuTexture::remove_slot(u32 slot) {
  auto it = std::find(slots.begin(), slots.end(), slot);
  ASSERT(it != slots.end());
  slots.erase(it);
}

void GpuTexture::add_slot(u32 slot) {
  ASSERT(std::find(slots.begin(), slots.end(), slot) == slots.end());
  slots.push_back(slot);
}

/*!
 * Handle a GOAL texture-page object being uploaded to VRAM.
 * The strategy:
 * - upload the texture-age to a fake 4MB VRAM, like the GOAL code would have done.
 * - "download" each texture in a reasonable format for the PC Port (currently RGBA8888)
 * - add this to the PC pool.
 *
 * The textures are scrambled around in a confusing way.
 *
 * NOTE: the actual conversion is currently done here, but this might be too slow.
 * We could store textures in the right format to begin with, or spread the conversion out over
 * multiple frames.
 */
void TexturePool::handle_upload_now(const u8* tpage, int mode, const u8* memory_base, u32 s7_ptr) {
  std::unique_lock<std::mutex> lk(m_mutex);
  // extract the texture-page object. This is just a description of the page data.
  GoalTexturePage texture_page;
  memcpy(&texture_page, tpage, sizeof(GoalTexturePage));

  bool has_segment[3] = {true, true, true};

  if (mode == -1) {
  } else if (mode == 2) {
    has_segment[0] = false;
    has_segment[1] = false;
  } else if (mode == -2) {
    has_segment[2] = false;
  } else if (mode == 0) {
    has_segment[1] = false;
    has_segment[2] = false;
  } else {
    // no reason to skip this, other than
    lg::error("TexturePool skipping upload now with mode {}.", mode);
    return;
  }

  // loop over all texture in the tpage and download them.
  for (int tex_idx = 0; tex_idx < texture_page.length; tex_idx++) {
    GoalTexture tex;
    if (texture_page.try_copy_texture_description(&tex, tex_idx, memory_base, tpage, s7_ptr)) {
      // each texture may have multiple mip levels.
      for (int mip_idx = 0; mip_idx < tex.num_mips; mip_idx++) {
        if (has_segment[tex.segment_of_mip(mip_idx)]) {
          PcTextureId current_id(texture_page.id, tex_idx);
          if (!m_id_to_name.lookup_existing(current_id)) {
            auto name = std::string(goal_string(texture_page.name_ptr, memory_base)) +
                        goal_string(tex.name_ptr, memory_base);
            *m_id_to_name.lookup_or_insert(current_id).first = name;
            m_name_to_id[name] = current_id;
          }

          auto& slot = m_textures[tex.dest[mip_idx]];

          if (slot.source) {
            if (slot.source->tex_id == current_id) {
              // we already have it, no need to do anything
            } else {
              slot.source->remove_slot(tex.dest[mip_idx]);
              slot.source = get_gpu_texture_for_slot(current_id, tex.dest[mip_idx]);
              ASSERT(slot.gpu_texture);
            }
          } else {
            slot.source = get_gpu_texture_for_slot(current_id, tex.dest[mip_idx]);
            ASSERT(slot.gpu_texture);
          }
        }
      }
    } else {
      // texture was #f, skip it.
    }
  }
}

void TexturePool::relocate(u32 destination, u32 source, u32 format) {
  std::unique_lock<std::mutex> lk(m_mutex);
  GpuTexture* src = lookup_gpu_texture(source);
  ASSERT(src);
  if (format == 44) {
    m_mt4hh_textures.emplace_back();
    m_mt4hh_textures.back().slot = destination;
    m_mt4hh_textures.back().ref.source = src;
    m_mt4hh_textures.back().ref.gpu_texture = src->gpu_textures.at(0).image;
    src->mt4hh_slots.push_back(destination);
  } else {
    move_existing_to_vram(src, destination);
  }
}

GpuTexture* TexturePool::get_gpu_texture_for_slot(PcTextureId id, u32 slot) {
  auto it = m_loaded_textures.lookup_or_insert(id);
  if (!it.second) {
    GpuTexture& placeholder = *it.first;
    placeholder.tex_id = id;
    placeholder.is_placeholder = true;
    placeholder.slots.push_back(slot);

    // auto r = m_loaded_textures.insert({name, placeholder});
    m_textures[slot].gpu_texture = m_placeholder_texture_id;
    return it.first;
  } else {
    auto result = it.first;
    result->add_slot(slot);
    m_textures[slot].gpu_texture =
        result->is_placeholder ? m_placeholder_texture_id : result->gpu_textures.at(0).image;
    return result;
  }
}

VkImage TexturePool::lookup_mt4hh(u32 location) {
  for (auto& t : m_mt4hh_textures) {
    if (t.slot == location) {
      if (t.ref.source) {
        return t.ref.gpu_texture;
      }
    }
  }
  return VK_NULL_HANDLE;
}

TexturePool::TexturePool()
    : m_loaded_textures(get_jak1_tpage_dir()), m_id_to_name(get_jak1_tpage_dir()) {
  m_placeholder_data.resize(16 * 16);
}

void TexturePool::Initialize(VkDevice device) {
  m_device = device;
  u32 c0 = 0xa0303030;
  u32 c1 = 0xa0e0e0e0;
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      m_placeholder_data[i * 16 + j] = (((i / 4) & 1) ^ ((j / 4) & 1)) ? c1 : c0;
    }
  }
  m_texture_info = upload_to_gpu((const u8*)(m_placeholder_data.data()), 16, 16);
  m_placeholder_texture_id = m_texture_info.texture;
}

void TexturePool::draw_debug_window() {
  int id = 0;
  int total_vram_bytes = 0;
  int total_textures = 0;
  int total_displayed_textures = 0;
  int total_uploaded_textures = 0;
  ImGui::InputText("texture search", m_regex_input, sizeof(m_regex_input));
  std::regex regex(m_regex_input[0] ? m_regex_input : ".*");

  for (size_t i = 0; i < m_textures.size(); i++) {
    auto& record = m_textures[i];
    total_textures++;
    if (record.source) {
      if (std::regex_search(get_debug_texture_name(record.source->tex_id), regex)) {
        ImGui::PushID(id++);
        draw_debug_for_tex(get_debug_texture_name(record.source->tex_id), record.source, i);
        ImGui::PopID();
        total_displayed_textures++;
      }
      if (!record.source->gpu_textures.empty()) {
        total_vram_bytes +=
            record.source->w * record.source->h * 4;  // todo, if we support other formats
      }

      total_uploaded_textures++;
    }
  }

  // todo mt4hh
  ImGui::Text("Total Textures: %d Uploaded: %d Shown: %d VRAM: %.3f MB", total_textures,
              total_uploaded_textures, total_displayed_textures,
              (float)total_vram_bytes / (1024 * 1024));
}

void TexturePool::draw_debug_for_tex(const std::string& name, GpuTexture* tex, u32 slot) {
  if (tex->is_placeholder) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8, 0.3, 0.3, 1.0));
  } else if (tex->gpu_textures.size() == 1) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3, 0.8, 0.3, 1.0));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8, 0.8, 0.3, 1.0));
  }
  if (ImGui::TreeNode(fmt::format("{} {}", name, slot).c_str())) {
    ImGui::Text("P: %s sz: %d x %d", get_debug_texture_name(tex->tex_id).c_str(), tex->w, tex->h);
    if (!tex->is_placeholder) {
      ImGui::Image((void*)tex->gpu_textures.at(0).image, ImVec2(tex->w, tex->h));
    } else {
      ImGui::Text("PLACEHOLDER");
    }

    ImGui::TreePop();
    ImGui::Separator();
  }
  ImGui::PopStyleColor();
}

PcTextureId TexturePool::allocate_pc_port_texture() {
  ASSERT(m_next_pc_texture_to_allocate < EXTRA_PC_PORT_TEXTURE_COUNT);
  return PcTextureId(get_jak1_tpage_dir().size() - 1, m_next_pc_texture_to_allocate++);
}

std::string TexturePool::get_debug_texture_name(PcTextureId id) {
  auto it = m_id_to_name.lookup_existing(id);
  if (it) {
    return *it;
  } else {
    return "???";
  }
}
