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
#include "game/graphics/texture/TexturePool.h"

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
class TexturePool : public BaseTexturePool {
 public:
  TexturePool(GameVersion version);
  void handle_upload_now(const u8* tpage, int mode, const u8* memory_base, u32 s7_ptr);
  GpuTexture* give_texture(const TextureInput& in);
  GpuTexture* give_texture_and_load_to_vram(const TextureInput& in, u32 vram_slot);
  void unload_texture(PcTextureId tex_id, u64 gpu_id);

  /*!
   * Look up an Graphics texture by vram address. Return std::nullopt if the game hasn't loaded
   * anything to this address.
   */
  std::optional<u64> lookup(u32 location) {
    auto& t = m_textures[location];
    if (t.source) {
      if constexpr (EXTRA_TEX_DEBUG) {
        if (t.source->is_placeholder) {
          ASSERT(t.gpu_texture == m_placeholder_texture_id);
        } else {
          bool fnd = false;
          for (auto& tt : t.source->gpu_textures) {
            if (tt.imageId == t.gpu_texture) {
              fnd = true;
              break;
            }
          }
          ASSERT(fnd);
        }
      }
      return t.gpu_texture;
    } else {
      return {};
    }
  }

  /*!
   * Look up a game texture by VRAM address. Will be nullptr if the game hasn't loaded anything to
   * this address.
   *
   * You should probably not use this to lookup textures that could be uploaded with
   * handle_upload_now.
   */
  std::optional<u64> lookup_mt4hh(u32 location);
  u64 get_placeholder_texture() { return m_placeholder_texture_id; }
  void draw_debug_window();
  void relocate(u32 destination, u32 source, u32 format);
  void draw_debug_for_tex(const std::string& name, GpuTexture* tex, u32 slot);
  const std::array<TextureVRAMReference, 1024 * 1024 * 4 / 256> all_textures() const {
    return m_textures;
  }
  void move_existing_to_vram(GpuTexture* tex, u32 slot_addr);

  std::mutex& mutex() { return m_mutex; }
  PcTextureId allocate_pc_port_texture();

  std::string get_debug_texture_name(PcTextureId id);

 private:
  void refresh_links(GpuTexture& texture);
  GpuTexture* get_gpu_texture_for_slot(PcTextureId id, u32 slot);

  char m_regex_input[256] = "";
  std::array<TextureVRAMReference, 1024 * 1024 * 4 / 256> m_textures;
  struct Mt4hhTexture {
    TextureVRAMReference ref;
    u32 slot;
  };
  std::vector<Mt4hhTexture> m_mt4hh_textures;

  std::vector<u32> m_placeholder_data;
  u64 m_placeholder_texture_id = 0;

  TextureMap<GpuTexture> m_loaded_textures;

  // we maintain a mapping of all textures/ids we've seen so far.
  // this is only used for debug.
  TextureMap<std::string> m_id_to_name;
  std::unordered_map<std::string, PcTextureId> m_name_to_id;

  u32 m_next_pc_texture_to_allocate = 0;
  u32 m_tpage_dir_size = 0;

  std::mutex m_mutex;
};
