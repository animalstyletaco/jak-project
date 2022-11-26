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

#include "game/graphics/texture/TexturePool.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/graphics/texture/TextureConverter.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/Image.h"

/*!
 * This represents a unique in-game texture, including any instances of it that are loaded.
 * It's possible for there to be 0 instances of the texture loaded yet.
 */
struct GpuTextureVulkan : GpuTexture {
  GpuTextureVulkan(PcTextureId id) : GpuTexture(id) {}
  GpuTextureVulkan() = default;

  // all the currently loaded copies of this texture
  std::vector<TextureData> gpu_textures;

  // get a pointer to our data, or nullptr if we are a placeholder.
  const u8* get_data_ptr() const {
    if (is_placeholder) {
      return nullptr;
    } else {
      return gpu_textures.at(0).data;
    }
  }
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
class TexturePoolVulkan : public BaseTexturePool {
 public:
  TexturePoolVulkan(std::unique_ptr<GraphicsDeviceVulkan>& m_device);
  GpuTexture* give_texture(const TextureInput& in);
  GpuTexture* give_texture_and_load_to_vram(const TextureInput& in, u32 vram_slot);
  void unload_texture(PcTextureId tex_id, VulkanTexture& texture);

      /*!
   * Look up an Graphics texture by vram address. Return std::nullopt if the game hasn't loaded
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
  VulkanTexture* lookup_gpu_vulkan_texture(u32 location);
  VulkanTexture* lookup_mt4hh_texture(u32 location);

  VulkanTexture* get_placeholder_vulkan_texture() { return &m_placeholder_texture; }
  void relocate(u32 destination, u32 source, u32 format);
  void draw_debug_for_tex(const std::string& name, GpuTexture* tex, u32 slot);

 protected:
  void upload_to_gpu(const u8* data, u16 w, u16 h, VulkanTexture&);
  void refresh_links(GpuTexture& texture);
  GpuTexture* get_gpu_texture_for_slot(PcTextureId id, u32 slot);

  std::vector<u32> m_placeholder_data;
  VulkanTexture m_placeholder_texture;

  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::unordered_map<u64, VulkanTexture*> m_vulkan_textures;
};
