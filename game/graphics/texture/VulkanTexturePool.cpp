#include "VulkanTexturePool.h"

#include <algorithm>
#include <exception>
#include <regex>

#include "common/log/log.h"
#include "common/util/Assert.h"
#include "common/util/Timer.h"

#include "game/graphics/texture/VulkanTexturePool.h"
#include "game/graphics/texture/jak1_tpage_dir.h"
#include "game/graphics/texture/jak2_tpage_dir.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

void VulkanTexturePool::upload_to_gpu(const u8* data, u16 w, u16 h, VulkanTexture& texture) {
  // TODO: Get Mipmap Level here
  unsigned mipLevels = 1;

  VkExtent3D extents{w, h, 1};
  texture.createImage(extents, mipLevels, VK_IMAGE_TYPE_2D, m_device->getMsaaCount(),
                      VK_FORMAT_A8B8G8R8_SRGB_PACK32, VK_IMAGE_TILING_LINEAR,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

  texture.writeToImage((u8*)data);

  texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_A8B8G8R8_SRGB_PACK32,
                          VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = m_device->getMaxSamplerAnisotropy();
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  m_device->createSampler(&samplerInfo, nullptr, &m_placeholder_sampler);

  m_placeholder_descriptor_image_info = VkDescriptorImageInfo{
      m_placeholder_sampler, texture.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}

VulkanGpuTextureMap* VulkanTexturePool::give_texture(const VulkanTextureInput& in) {
  const auto [texture_map, does_texture_exist] = lookup_or_insert_loaded_textures(in.id);
  if (!does_texture_exist) {
    // nothing references this texture yet.
    texture_map->set_texture_id(in.id);
    texture_map->set_common_texture_status(in.common);
    texture_map->gpu_textures.push_back(in.texture);
    texture_map->set_placeholder_status(false);
    *lookup_or_insert_id_to_name(in.id).first =
        fmt::format("{}/{}", in.debug_page_name, in.debug_name);
    return texture_map;
  }

  if (!texture_map->get_placeholder_status()) {
    // two sources for texture. this is fine.
    ASSERT(!texture_map->gpu_textures.empty());
  } else {
    ASSERT(texture_map->gpu_textures.empty());
  }
  texture_map->set_placeholder_status(false);
  texture_map->gpu_textures.push_back(in.texture);
  texture_map->set_common_texture_status(in.common);
  refresh_links(*texture_map);
  return texture_map;
}

VulkanGpuTextureMap* VulkanTexturePool::give_texture_and_load_to_vram(const VulkanTextureInput& in,
                                                                      u32 vram_slot) {
  auto tex = give_texture(in);
  move_existing_to_vram(tex, vram_slot);
  return tex;
}

void VulkanTexturePool::move_existing_to_vram(VulkanGpuTextureMap* tex, u32 slot_addr) {
  ASSERT(!tex->get_placeholder_status());
  ASSERT(!tex->gpu_textures.empty());
  auto& slot = m_textures[slot_addr];
  tex->add_slot_if_not_found(slot_addr);

  if (!slot.source) {
    slot.source = tex;
    slot.texture = tex->get_selected_texture();
  } else if (slot.source != tex) {
    slot.source->remove_slot(slot_addr);
    slot.source = tex;
    slot.texture = tex->get_selected_texture();
  }
}

void VulkanTexturePool::refresh_links(VulkanGpuTextureMap& texture_map) {
  VulkanTexture* tex_to_use =
      texture_map.get_placeholder_status() ? &m_placeholder_texture : texture_map.get_selected_texture();

  auto slots = texture_map.get_slots();
  for (const auto& slot : slots) {
    auto& t = m_textures[slot];
    ASSERT(t.source == &texture_map);
    t.texture = tex_to_use;
  }

  auto mt4hh_slots = texture_map.get_mt4hh_slots();
  for (const auto& slot : mt4hh_slots) {
    for (auto& tex : m_mt4hh_textures) {
      if (tex.slot == slot) {
        tex.ref.texture = tex_to_use;
      }
    }
  }
}

void VulkanTexturePool::unload_texture(PcTextureId tex_id, u64 gpu_id) {
  auto* tex = lookup_existing_loaded_textures(tex_id);
  ASSERT(tex);
  if (tex->get_common_texture_status()) {
    ASSERT(false);
    return;
  }
  ASSERT_MSG(!tex->get_common_texture_status(),
             fmt::format("trying to unload something that was already placholdered: {} {}\n",
                         get_debug_texture_name(tex_id), tex->gpu_textures.size()));
  auto it = std::find_if(tex->gpu_textures.begin(), tex->gpu_textures.end(),
                         [&](const auto& a) { return a->GetTextureId() == gpu_id; });
  ASSERT(it != tex->gpu_textures.end());

  tex->gpu_textures.erase(it);
  if (tex->gpu_textures.empty()) {
    tex->set_placeholder_status(true);
  }
  refresh_links(*tex);
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
void VulkanTexturePool::handle_upload_now(const u8* tpage,
                                          int mode,
                                          const u8* memory_base,
                                          u32 s7_ptr) {
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
    if (!texture_page.try_copy_texture_description(&tex, tex_idx, memory_base, tpage, s7_ptr)) {
      // texture was #f, skip it.
      continue;
    }
      // each texture may have multiple mip levels.
    for (int mip_idx = 0; mip_idx < tex.num_mips; mip_idx++) {
      if (has_segment[tex.segment_of_mip(mip_idx)]) {
        PcTextureId current_id(texture_page.id, tex_idx);
        if (!lookup_existing_id_to_name(current_id)) {
          auto name = std::string(texture_pool::goal_string(texture_page.name_ptr, memory_base)) +
                      texture_pool::goal_string(tex.name_ptr, memory_base);
          *lookup_or_insert_id_to_name(current_id).first = name;
          set_name_to_id(name, current_id);
        }

        auto& slot = m_textures[tex.dest[mip_idx]];

        if (!slot.source) {
          slot.source = get_gpu_texture_for_slot(current_id, tex.dest[mip_idx]);
          ASSERT(slot.texture);
          continue;
        }
        if (slot.source->get_texture_id() != current_id) {
          slot.source->remove_slot(tex.dest[mip_idx]);
          slot.source = get_gpu_texture_for_slot(current_id, tex.dest[mip_idx]);
          ASSERT(slot.texture);
        }  
      }
    }
  }
}

void VulkanTexturePool::relocate(u32 destination, u32 source, u32 format) {
  std::unique_lock<std::mutex> lk(m_mutex);
  VulkanGpuTextureMap* src = lookup_vulkan_gpu_texture(source);
  ASSERT(src);
  if (format == 44) {
    m_mt4hh_textures.emplace_back();
    m_mt4hh_textures.back().slot = destination;
    m_mt4hh_textures.back().ref.source = src;
    m_mt4hh_textures.back().ref.texture = src->get_selected_texture();
    src->force_add_mt4hh_slot(destination);
  } else {
    move_existing_to_vram(src, destination);
  }
}

VulkanGpuTextureMap* VulkanTexturePool::get_gpu_texture_for_slot(PcTextureId id, u32 slot) {
  auto [texture_map, is_placeholder] = lookup_or_insert_loaded_textures(id);
  if (!is_placeholder) {
    texture_map->set_texture_id(id);
    texture_map->set_placeholder_status(true);
    texture_map->force_add_slot(slot);

    m_textures[slot].texture = &m_placeholder_texture;
    return texture_map;
  }

  texture_map->add_slot(slot);
  m_textures[slot].texture =
      texture_map->get_placeholder_status() ? &m_placeholder_texture : texture_map->get_selected_texture();
  return texture_map;
}

VulkanTexture* VulkanTexturePool::lookup_vulkan_texture(u32 location) {
  auto& t = m_textures[location];
  if (!t.source) {
    return nullptr;
  }
  if constexpr (EXTRA_TEX_DEBUG) {
    if (t.source->get_placeholder_status()) {
      ASSERT(t.texture == &m_placeholder_texture);
    } else {
      bool fnd = false;
      for (auto& tt : t.source->gpu_textures) {
        if (tt->GetTextureId() == t.texture->GetTextureId()) {
          fnd = true;
          break;
        }
      }
      ASSERT(fnd);
    }
  }
  return t.texture;
}

VulkanTexture* VulkanTexturePool::lookup_mt4hh_vulkan_texture(u32 location) {
  for (auto& t : m_mt4hh_textures) {
    if (t.slot == location) {
      if (t.ref.source) {
        return t.ref.texture;
      }
    }
  }
  return nullptr;
}

VulkanTexturePool::VulkanTexturePool(std::shared_ptr<GraphicsDeviceVulkan> device)
    : m_device(device) {
  std::vector<u8> placeholder_data;
  placeholder_data.resize(16 * 16);

  u32 c0 = 0xa0303030;
  u32 c1 = 0xa0e0e0e0;
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      placeholder_data[i * 16 + j] = (((i / 4) & 1) ^ ((j / 4) & 1)) ? c1 : c0;
    }
  }
  upload_to_gpu((const u8*)(placeholder_data.data()), 16, 16, m_placeholder_texture);
}

void VulkanTexturePool::draw_debug_window() {
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
      if (std::regex_search(get_debug_texture_name(record.source->get_texture_id()), regex)) {
        ImGui::PushID(id++);
        draw_debug_for_tex(get_debug_texture_name(record.source->get_texture_id()), record.source, i);
        ImGui::PopID();
        total_displayed_textures++;
      }
      if (!record.source->gpu_textures.empty()) {
        VulkanTexture* vulkan_texture = record.source->get_selected_texture();
        total_vram_bytes += vulkan_texture->getWidth() * vulkan_texture->getHeight() *
                            4;  // todo, if we support other formats
      }

      total_uploaded_textures++;
    }
  }

  // todo mt4hh
  ImGui::Text("Total Textures: %d Uploaded: %d Shown: %d VRAM: %.3f MB", total_textures,
              total_uploaded_textures, total_displayed_textures,
              (float)total_vram_bytes / (1024 * 1024));
}

void VulkanTexturePool::draw_debug_for_tex(const std::string& name,
                                           VulkanGpuTextureMap* tex,
                                           u32 slot) {
  if (tex->get_placeholder_status()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8, 0.3, 0.3, 1.0));
  } else if (tex->gpu_textures.size() == 1) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3, 0.8, 0.3, 1.0));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8, 0.8, 0.3, 1.0));
  }
  if (ImGui::TreeNode(fmt::format("{} {}", name, slot).c_str())) {
    VulkanTexture* vulkan_texture = tex->get_selected_texture();
    ImGui::Text("P: %s sz: %d x %d", get_debug_texture_name(tex->get_texture_id()).c_str(),
                vulkan_texture->getWidth(), vulkan_texture->getHeight());
    if (!tex->get_placeholder_status()) {
      VulkanTexture* selected_texture = tex->get_selected_texture();
      ImGui::Image((void*)selected_texture->GetTextureId(),
                   ImVec2(selected_texture->getWidth(), selected_texture->getHeight()));
    } else {
      ImGui::Text("PLACEHOLDER");
    }

    ImGui::TreePop();
    ImGui::Separator();
  }
  ImGui::PopStyleColor();
}

PcTextureId VulkanTexturePoolJak1::allocate_pc_port_texture() {
  ASSERT(m_next_pc_texture_to_allocate < EXTRA_PC_PORT_TEXTURE_COUNT);
  return PcTextureId(get_jak1_tpage_dir().size() - 1, m_next_pc_texture_to_allocate++);
}

PcTextureId VulkanTexturePoolJak2::allocate_pc_port_texture() {
  ASSERT(m_next_pc_texture_to_allocate < EXTRA_PC_PORT_TEXTURE_COUNT);
  return PcTextureId(get_jak2_tpage_dir().size() - 1, m_next_pc_texture_to_allocate++);
}

std::string VulkanTexturePool::get_debug_texture_name(PcTextureId id) {
  auto it = lookup_existing_id_to_name(id);
  if (it) {
    return *it;
  }
  return "???";
}

VulkanTexturePool::~VulkanTexturePool() {
   m_device->destroySampler(m_placeholder_sampler, nullptr);
}

VulkanTexturePoolJak1::VulkanTexturePoolJak1(std::shared_ptr<GraphicsDeviceVulkan> device)
    : VulkanTexturePool(device),
      m_loaded_textures(get_jak1_tpage_dir()),
      m_id_to_name(get_jak1_tpage_dir()) {
  m_tpage_dir_size = get_jak1_tpage_dir().size();
}

VulkanTexturePoolJak2::VulkanTexturePoolJak2(std::shared_ptr<GraphicsDeviceVulkan> device)
    : VulkanTexturePool(device),
      m_loaded_textures(get_jak2_tpage_dir()),
      m_id_to_name(get_jak2_tpage_dir()) {
  m_tpage_dir_size = get_jak2_tpage_dir().size();
}

VulkanGpuTextureMap* VulkanTexturePoolJak1::lookup_existing_loaded_textures(PcTextureId tex_id) {
  return m_loaded_textures.lookup_existing(tex_id);
}

std::string* VulkanTexturePoolJak1::lookup_existing_id_to_name(PcTextureId tex_id) {
  return m_id_to_name.lookup_existing(tex_id);
}

std::pair<VulkanGpuTextureMap*, bool> VulkanTexturePoolJak1::lookup_or_insert_loaded_textures(
    PcTextureId tex_id) {
  return m_loaded_textures.lookup_or_insert(tex_id);
}

std::pair<std::string*, bool> VulkanTexturePoolJak1::lookup_or_insert_id_to_name(
    PcTextureId tex_id) {
  return m_id_to_name.lookup_or_insert(tex_id);
}

void VulkanTexturePoolJak1::set_name_to_id(const std::string& name, PcTextureId id) {
  m_name_to_id[name] = id;
}

VulkanGpuTextureMap* VulkanTexturePoolJak2::lookup_existing_loaded_textures(PcTextureId tex_id) {
  return m_loaded_textures.lookup_existing(tex_id);
}

std::string* VulkanTexturePoolJak2::lookup_existing_id_to_name(PcTextureId tex_id) {
  return m_id_to_name.lookup_existing(tex_id);
}

std::pair<VulkanGpuTextureMap*, bool> VulkanTexturePoolJak2::lookup_or_insert_loaded_textures(
    PcTextureId tex_id) {
  return m_loaded_textures.lookup_or_insert(tex_id);
}

std::pair<std::string*, bool> VulkanTexturePoolJak2::lookup_or_insert_id_to_name(
    PcTextureId tex_id) {
  return m_id_to_name.lookup_or_insert(tex_id);
}

void VulkanTexturePoolJak2::set_name_to_id(const std::string& name, PcTextureId id) {
  m_name_to_id[name] = id;
}
