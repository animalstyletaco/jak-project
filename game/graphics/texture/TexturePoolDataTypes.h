#pragma once

#include <vector>

#include "common/log/log.h"
#include "common/common_types.h"
#include "game/graphics/texture/TextureID.h"


// verify all texture lookups.
// will make texture lookups slower and likely caused dropped frames when loading
constexpr bool EXTRA_TEX_DEBUG = false;

// sky, cloud textures
constexpr int SKY_TEXTURE_VRAM_ADDRS[2] = {8064, 8096};

/*!
 * PC Port Texture System
 *
 * The main goal of this texture system is to support fast lookup textures by VRAM address
 * (sometimes called texture base pointer or TBP). The lookup ends up being a single read from
 * an array - no pointer chasing required.
 *
 * The TIE/TFRAG background renderers use their own more efficient system for this.
 * This is only used for renderers that interpret GIF data (sky, eyes, generic, merc, direct,
 * sprite).
 *
 * Some additional challenges:
 * - Some textures are generated by rendering to a texture (eye, sky)
 * - The game may try to render things before their textures have been loaded.  This is a "bug" in
 *   the original game, but can't be seen most of the time because the objects are often hidden.
 * - We preconvert PS2-format textures and store them in the FR3 level asset files. But the game may
 *   try to use the textures before the PC port has finished loading them.
 * - The game may copy textures from one location in VRAM to another
 * - The game may store two texture on top of each other in some formats (only the font). The PS2's
 *   texture formats allow you to do this if you use the right pair formats.
 * - The same texture may appear in multiple levels, both of which can be loaded at the same time.
 *   The two levels can unload in either order, and the remaining level should be able to use the
 *   texture.
 * - Some renderers need to access the actual texture data on the CPU.
 * - We don't want to load all the textures into VRAM at the same time.
 *
 * But, we have a few assumptions we make to simplify things:
 * - Two textures with the same "combined name" are always identical data. (This is verified by the
 *   decompiler). So we can use the name as an ID for the texture.
 * - The game will remove all references to textures that belong to an unloaded level, so once the
 *   level is gone, we can forget its textures.
 * - The number of times a texture is duplicated (both in VRAM, and in loaded levels) is small
 *
 * Unlike the first version of the texture system, our approach is to load all the textures to
 * the GPU during loading.
 *
 * This approach has three layers:
 * - A VRAM entry (TextureReference), which refers to a GpuTexture
 * - A GpuTexture, which represents an in-game texture, and refers to all loaded instances of it
 * - Actual texture data
 *
 * Note that the VRAM entries store the GLuint for the actual texture reference inline, so texture
 * lookups during drawing are very fast. The time to set up and maintain all these links only
 * happens during loading, and it's insignificant compared to reading from the hard drive or
 * unpacking/uploading meshes.
 *
 * The loader will inform us when things are added/removed.
 * The game will inform us when it uploads to VRAM
 */

template <typename T>
class TextureMap {
 public:
  TextureMap(const std::vector<u32>& tpage_dir) {
    u32 off = 0;
    for (auto& x : tpage_dir) {
      m_dir.push_back(off);
      off += x;
    }
    m_data.resize(off);
  }

  T* lookup_existing(PcTextureId id) {
    auto& elt = m_data[m_dir[id.page] + id.tex];
    if (elt.present) {
      return &elt.val;
    } else {
      return nullptr;
    }
  }

  T& at(PcTextureId id) {
    auto& elt = m_data[m_dir[id.page] + id.tex];
    if (elt.present) {
      return elt.val;
    }
    ASSERT(false);
  }

  std::pair<T*, bool> lookup_or_insert(PcTextureId id) {
    auto& elt = m_data[m_dir[id.page] + id.tex];
    if (elt.present) {
      return std::make_pair(&elt.val, true);
    } else {
      elt.present = true;
      return std::make_pair(&elt.val, false);
    }
  }

  void erase(PcTextureId id) {
    auto& elt = m_data[m_dir[id.page] + id.tex];
    elt.present = false;
  }

 private:
  std::vector<u32> m_dir;
  struct Element {
    T val;
    bool present = false;
  };
  std::vector<Element> m_data;
};

/*!
 * A texture provided by the loader.
 */
struct BaseTextureInput {
  std::string debug_page_name;
  std::string debug_name;

  PcTextureId id;

  bool common = false;
};

/*!
 * The in-game texture type.
 */
struct GoalTexture {
  s16 w;
  s16 h;
  u8 num_mips;
  u8 tex1_control;
  u8 psm;
  u8 mip_shift;
  u16 clutpsm;
  u16 dest[7];
  u16 clut_dest;
  u8 width[7];
  u32 name_ptr;
  u32 size;
  float uv_dist;
  u32 masks[3];

  s32 segment_of_mip(s32 mip) const {
    if (2 >= num_mips) {
      return num_mips - mip - 1;
    } else {
      return std::max(0, 2 - mip);
    }
  }
};

static_assert(sizeof(GoalTexture) == 60, "GoalTexture size");
static_assert(offsetof(GoalTexture, clutpsm) == 8);
static_assert(offsetof(GoalTexture, clut_dest) == 24);

/*!
 * The in-game texture page type.
 */
struct GoalTexturePage {
  struct Seg {
    u32 block_data_ptr;
    u32 size;
    u32 dest;
  };
  u32 file_info_ptr;
  u32 name_ptr;
  u32 id;
  s32 length;  // texture count
  u32 mip0_size;
  u32 size;
  Seg segment[3];
  u32 pad[16];
  // start of array.

  std::string print() const {
    return fmt::format("Tpage id {} textures {} seg0 {} {} seg1 {} {} seg2 {} {}\n", id, length,
                       segment[0].size, segment[0].dest, segment[1].size, segment[1].dest,
                       segment[2].size, segment[2].dest);
  }

  bool try_copy_texture_description(GoalTexture* dest,
                                    int idx,
                                    const u8* memory_base,
                                    const u8* tpage,
                                    u32 s7_ptr) {
    u32 ptr;
    memcpy(&ptr, tpage + sizeof(GoalTexturePage) + 4 * idx, 4);
    if (ptr == s7_ptr) {
      return false;
    }
    memcpy(dest, memory_base + ptr, sizeof(GoalTexture));
    return true;
  }
};

namespace texture_pool {
const char* goal_string(u32 ptr, const u8* memory_base);
}  // namespace