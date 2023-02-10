#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/common_types.h"
#include "common/util/Serializer.h"
#include "common/util/SmallVector.h"
#include "common/versions.h"

#include "game/graphics/texture/TextureConverter.h"
#include "game/graphics/texture/TexturePoolDataTypes.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"

/*!
 * A texture provided by the loader.
 */
struct VulkanTextureInput : BaseTextureInput {
  VulkanTexture* texture = nullptr;
};

/*!
 * This represents a unique in-game texture, including any instances of it that are loaded.
 * It's possible for there to be 0 instances of the texture loaded yet.
 */
class VulkanGpuTextureMap  {
 public:
  VulkanGpuTextureMap(PcTextureId id) : tex_id(id) {}
  VulkanGpuTextureMap() = default;
  PcTextureId tex_id;

  // set to true if we have no copies of the texture, and we should use a placeholder
  bool is_placeholder = false;

  // set to true if we are part of the textures in GAME.CGO that are always loaded.
  // for these textures, the pool can assume that we are never a placeholder.
  bool is_common = false;

  bool are_textures_available() { return !gpu_textures.empty(); }
  u32 get_selected_slot() const { return (!slots.empty()) ? slots.at(0) : 0; }
    // the size of our data, in bytes
  u32 data_size() const { return gpu_textures.at(get_selected_slot())->getMemorySize(); }
  VulkanTexture* get_selected_texture() const { return (!gpu_textures.empty()) ? gpu_textures.front() : nullptr; }

  // add or remove a VRAM reference to this texture
  void remove_slot(u32 slot);
  void add_slot(u32 slot);

  // all the currently loaded copies of this texture
  std::vector<VulkanTexture*> gpu_textures;

  std::vector<u32> slots;
  std::vector<u32> mt4hh_slots;
};

/*!
 * A VRAM slot.
 * If the source is nullptr, the game has not loaded anything to this address.
 * If the game has loaded something, but the loader hasn't loaded the converted texture, the
 * source will be non-null and the gpu_texture will be a placeholder that is safe to use.
 */
struct VulkanTextureVRAMReference {
  VulkanTexture* texture = nullptr;
  VulkanGpuTextureMap* source = nullptr;
};

/*!
 * The main texture pool.
 * Moving textures around should be done with locking. (the game EE thread and the loader run
 * simultaneously)
 *
 * Lookups can be done without locking.
 * It is safe for renderers to use textures without worrying about locking - OpenGL textures
 * themselves are only removed from the rendering thread.
 *
 * There could be races with the game doing texture uploads and doing texture lookups, but these
 * races are harmless. If there's an actual in-game race condition, the exact texture you get may be
 * unknown, but you will get a valid texture.
 *
 * (note that the above property is only true because we never make a VRAM slot invalid after
 *  it has been loaded once)
 */
class VulkanTexturePool {
 public:
  VulkanTexturePool(GameVersion version, std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~VulkanTexturePool();
  void handle_upload_now(const u8* tpage, int mode, const u8* memory_base, u32 s7_ptr);
  VulkanGpuTextureMap* give_texture(const VulkanTextureInput& in);
  VulkanGpuTextureMap* give_texture_and_load_to_vram(const VulkanTextureInput& in, u32 vram_slot);
  void unload_texture(PcTextureId tex_id, u64 gpu_id);

  /*!
   * Look up an OpenGL texture by vram address. Return std::nullopt if the game hasn't loaded
   * anything to this address.
   */
  VulkanTexture* lookup_vulkan_texture(u32 location);

  /*!
   * Look up a game texture by VRAM address. Will be nullptr if the game hasn't loaded anything to
   * this address.
   *
   * You should probably not use this to lookup textures that could be uploaded with
   * handle_upload_now.
   */
  VulkanGpuTextureMap* lookup_vulkan_gpu_texture(u32 location) {
    return m_textures[location].source;
  }
  VulkanTexture* lookup_mt4hh_vulkan_texture(u32 location);
  VulkanTexture* get_placeholder_vulkan_texture() { return &m_placeholder_texture; }
  void draw_debug_window();
  void relocate(u32 destination, u32 source, u32 format);
  void draw_debug_for_tex(const std::string& name, VulkanGpuTextureMap* tex, u32 slot);
  const std::array<VulkanTextureVRAMReference, 1024 * 1024 * 4 / 256> all_textures() const {
    return m_textures;
  }
  void move_existing_to_vram(VulkanGpuTextureMap* tex, u32 slot_addr);
  VkDescriptorImageInfo* get_placeholder_descriptor_image_info() {
    return &m_placeholder_descriptor_image_info;
  }

  std::mutex& mutex() { return m_mutex; }
  PcTextureId allocate_pc_port_texture();

  std::string get_debug_texture_name(PcTextureId id);

 private:
  void upload_to_gpu(const u8* data, u16 w, u16 h, VulkanTexture& texture);
  void refresh_links(VulkanGpuTextureMap& texture);
  VulkanGpuTextureMap* get_gpu_texture_for_slot(PcTextureId id, u32 slot);

  char m_regex_input[256] = "";
  std::array<VulkanTextureVRAMReference, 1024 * 1024 * 4 / 256> m_textures;

  struct Mt4hhTexture {
    VulkanTextureVRAMReference ref;
    u32 slot;
  };
  std::vector<Mt4hhTexture> m_mt4hh_textures;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  VulkanTexture m_placeholder_texture{m_device};
  VkSampler m_placeholder_sampler = VK_NULL_HANDLE;

  VkDescriptorImageInfo m_placeholder_descriptor_image_info;

  TextureMap<VulkanGpuTextureMap> m_loaded_textures;

  // we maintain a mapping of all textures/ids we've seen so far.
  // this is only used for debug.
  TextureMap<std::string> m_id_to_name;
  std::unordered_map<std::string, PcTextureId> m_name_to_id;

  u32 m_next_pc_texture_to_allocate = 0;
  u32 m_tpage_dir_size = 0;

  std::mutex m_mutex;
};
