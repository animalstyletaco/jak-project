#include "TextureAnimator.h"

#include "common/global_profiler/GlobalProfiler.h"
#include "common/log/log.h"
#include "common/texture/texture_slots.h"
#include "common/util/FileUtil.h"
#include "common/util/Timer.h"

#include "third-party/imgui/imgui.h"

#define dprintf(...)
#define dfmt(...)

const tfrag3::IndexTexture* itex_by_name(const tfrag3::Level* level,
                                         const std::string& name,
                                         const std::optional<std::string>& level_name) {
  const tfrag3::IndexTexture* ret = nullptr;
  for (const auto& t : level->index_textures) {
    bool match = t.name == name;
    if (level_name && match) {
      match =
          std::find(t.level_names.begin(), t.level_names.end(), *level_name) != t.level_names.end();
      if (!match && false) {
        lg::warn("rejecting {} because it wasn't in desired level {}, but was in:", t.name,
                 *level_name);
        for (auto& l : t.level_names) {
          lg::warn("  {}", l);
        }
      }
    }
    if (match) {
      if (ret) {
        lg::error("Multiple index textures named {}", name);
        ASSERT(ret->color_table == t.color_table);
        ASSERT(ret->index_data == t.index_data);
      }
      ret = &t;
    }
  }
  if (!ret) {
    lg::die("no index texture named {}", name);
  } else {
    // lg::info("got idx: {}", name);
  }
  return ret;
}

int output_slot_by_idx(GameVersion version, const std::string& name) {
  const std::vector<std::string>* v = nullptr;
  switch (version) {
    case GameVersion::Jak2:
      v = &jak2_animated_texture_slots();
      break;
    default:
    case GameVersion::Jak1:
      ASSERT_NOT_REACHED();
  }

  for (size_t i = 0; i < v->size(); i++) {
    if ((*v)[i] == name) {
      return i;
    }
  }
  ASSERT_NOT_REACHED();
}

BaseClutBlender::BaseClutBlender(const std::string& dest,
                                 const std::vector<std::string>& sources,
                                 const std::optional<std::string>& level_name,
                                 const tfrag3::Level* level) {
  m_dest = itex_by_name(level, dest, level_name);
  for (const auto& sname : sources) {
    m_cluts.push_back(&itex_by_name(level, sname, level_name)->color_table);
    m_current_weights.push_back(0);
  }
  m_temp_rgba.resize(m_dest->w * m_dest->h);

  std::vector<float> init_weights(m_current_weights.size(), 0);
  init_weights.at(0) = 1.f;
  run(init_weights.data());
}

BaseTextureAnimator::BaseTextureAnimator(const tfrag3::Level* common_level)
    : m_common_level(common_level),
      m_psm32_to_psm8_8_8(8, 8, 8, 64),
      m_psm32_to_psm8_16_16(16, 16, 16, 64),
      m_psm32_to_psm8_32_32(32, 32, 16, 64),
      m_psm32_to_psm8_64_64(64, 64, 64, 64) {
  // Initialize graphics

  m_output_debug_flags = std::vector<u8>{0, jak2_animated_texture_slots().size()};

  // generate CLUT table.
  for (int i = 0; i < 256; i++) {
    u32 clut_chunk = i / 16;
    u32 off_in_chunk = i % 16;
    u8 clx = 0, cly = 0;
    if (clut_chunk & 1) {
      clx = 8;
    }
    cly = (clut_chunk >> 1) * 2;
    if (off_in_chunk >= 8) {
      off_in_chunk -= 8;
      cly++;
    }
    clx += off_in_chunk;
    m_index_to_clut_addr[i] = clx + cly * 16;
  }
}

namespace texture_utils {
// initial values of the random table for cloud texture generation.
constexpr Vector16ub kInitialRandomTable[BaseTextureAnimator::kRandomTableSize] = {
    {0x20, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x89, 0x67, 0x45, 0x23, 0x1},
    {0x37, 0x82, 0x87, 0x23, 0x78, 0x87, 0x4, 0x32, 0x97, 0x91, 0x48, 0x98, 0x30, 0x38, 0x89, 0x87},
    {0x62, 0x47, 0x2, 0x62, 0x78, 0x92, 0x28, 0x90, 0x81, 0x47, 0x72, 0x28, 0x83, 0x29, 0x71, 0x68},
    {0x28, 0x61, 0x17, 0x62, 0x87, 0x74, 0x38, 0x12, 0x83, 0x9, 0x78, 0x12, 0x76, 0x31, 0x72, 0x80},
    {0x39, 0x72, 0x98, 0x34, 0x72, 0x98, 0x69, 0x78, 0x65, 0x71, 0x98, 0x83, 0x97, 0x23, 0x98, 0x1},
    {0x97, 0x38, 0x72, 0x98, 0x23, 0x87, 0x23, 0x98, 0x93, 0x72, 0x98, 0x20, 0x81, 0x29, 0x10,
     0x62},
    {0x28, 0x75, 0x38, 0x82, 0x99, 0x30, 0x72, 0x87, 0x83, 0x9, 0x14, 0x98, 0x10, 0x43, 0x87, 0x29},
    {0x87, 0x23, 0x0, 0x87, 0x18, 0x98, 0x12, 0x98, 0x10, 0x98, 0x21, 0x83, 0x90, 0x37, 0x62,
     0x71}};

/*!
 * Update dest and random_table.
 */
int make_noise_texture(u8* dest, Vector16ub* random_table, int dim, int random_index_in) {
  ASSERT(dim % 16 == 0);
  const int qw_per_row = dim / 16;
  for (int row = 0; row < dim; row++) {
    for (int qw_in_row = 0; qw_in_row < qw_per_row; qw_in_row++) {
      const int row_start_qwi = row * qw_per_row;
      Vector16ub* rand_rows[4] = {
          random_table + (random_index_in + 0) % BaseTextureAnimator::kRandomTableSize,
          random_table + (random_index_in + 3) % BaseTextureAnimator::kRandomTableSize,
          random_table + (random_index_in + 5) % BaseTextureAnimator::kRandomTableSize,
          random_table + (random_index_in + 7) % BaseTextureAnimator::kRandomTableSize,
      };
      const int qwi = row_start_qwi + qw_in_row;
      *rand_rows[3] = *rand_rows[0] + *rand_rows[1] + *rand_rows[2];
      memcpy(dest + 16 * qwi, rand_rows[3]->data(), 16);
      random_index_in = (random_index_in + 1) % BaseTextureAnimator::kRandomTableSize;
    }
  }
  return random_index_in;
}
}  // namespace texture_utils

void BaseTextureAnimator::setup_texture_anims() {
  // DARKJAK
  m_darkjak_clut_blender_idx = create_clut_blender_group(
      {"jakbsmall-eyebrow", "jakbsmall-face", "jakbsmall-finger", "jakbsmall-hair"}, "-norm",
      "-dark", {});

  // PRISON
  // MISSING EYELID
  m_jakb_prison_clut_blender_idx = create_clut_blender_group(
      {"jak-orig-arm-formorph", "jak-orig-eyebrow-formorph", "jak-orig-finger-formorph"}, "-start",
      "-end", "LDJAKBRN.DGO");
  add_to_clut_blender_group(m_jakb_prison_clut_blender_idx,
                            {"jakb-facelft", "jakb-facert", "jakb-hairtrans"}, "-norm", "-dark",
                            "LDJAKBRN.DGO");

  // ORACLE
  // MISSING FINGER
  m_jakb_oracle_clut_blender_idx = create_clut_blender_group(
      {"jakb-eyebrow", "jakb-eyelid", "jakb-facelft", "jakb-facert", "jakb-hairtrans"}, "-norm",
      "-dark", "ORACLE.DGO");

  // NEST
  // MISSING FINGER
  m_jakb_nest_clut_blender_idx = create_clut_blender_group(
      {"jakb-eyebrow", "jakb-eyelid", "jakb-facelft", "jakb-facert", "jakb-hairtrans"}, "-norm",
      "-dark", "NEB.DGO");

  // KOR (doesn't work??)
  m_kor_transform_clut_blender_idx = create_clut_blender_group(
      {
          // "kor-eyeeffect-formorph",
          // "kor-hair-formorph",
          // "kor-head-formorph",
          // "kor-head-formorph-noreflect",
          // "kor-lowercaps-formorph",
          // "kor-uppercaps-formorph",
      },
      "-start", "-end", {});

  // Skull Gem
  {
    BaseFixedAnimDef skull_gem;
    skull_gem.move_to_pool = true;
    skull_gem.tex_name = "skull-gem-dest";
    skull_gem.color = math::Vector4<u8>{0, 0, 0, 0x80};
    // overriden in texture-finish.gc
    skull_gem.override_size = math::Vector2<int>(32, 32);

    auto& skull_gem_0 = skull_gem.layers.emplace_back();
    skull_gem_0.end_time = 300.;
    skull_gem_0.tex_name = "skull-gem-alpha-00";
    skull_gem_0.set_blend_b2_d1();
    skull_gem_0.set_no_z_write_no_z_test();

    auto& skull_gem_1 = skull_gem.layers.emplace_back();
    skull_gem_1.end_time = 300.;
    skull_gem_1.tex_name = "skull-gem-alpha-01";
    skull_gem_1.set_blend_b2_d1();
    skull_gem_1.set_no_z_write_no_z_test();

    auto& skull_gem_2 = skull_gem.layers.emplace_back();
    skull_gem_2.end_time = 300.;
    skull_gem_2.tex_name = "skull-gem-alpha-02";
    skull_gem_2.set_blend_b2_d1();
    skull_gem_2.set_no_z_write_no_z_test();

    m_skull_gem_fixed_anim_array_idx = create_fixed_anim_array({skull_gem});
  }

  // Bomb
  {
    BaseFixedAnimDef bomb;
    bomb.tex_name = "bomb-gradient";
    bomb.color = math::Vector4<u8>{0, 0, 0, 0x80};

    auto& bomb_0 = bomb.layers.emplace_back();
    bomb_0.end_time = 300.;
    bomb_0.tex_name = "bomb-gradient-rim";
    bomb_0.set_blend_b2_d1();
    bomb_0.set_no_z_write_no_z_test();
    // :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
    bomb_0.channel_masks[3] = false;  // no alpha writes.

    auto& bomb_1 = bomb.layers.emplace_back();
    bomb_1.end_time = 300.;
    bomb_1.tex_name = "bomb-gradient-flames";
    bomb_1.set_blend_b2_d1();
    bomb_1.set_no_z_write_no_z_test();
    // :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
    bomb_1.channel_masks[3] = false;  // no alpha writes.

    m_bomb_fixed_anim_array_idx = create_fixed_anim_array({bomb});
  }

  // CAS conveyor
  {
    BaseFixedAnimDef conveyor_0;
    conveyor_0.tex_name = "cas-conveyor-dest";
    conveyor_0.color = math::Vector4<u8>(0, 0, 0, 0x80);
    conveyor_0.override_size = math::Vector2<int>(64, 32);
    auto& c0 = conveyor_0.layers.emplace_back();
    c0.set_blend_b2_d1();
    c0.set_no_z_write_no_z_test();
    // :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
    c0.channel_masks[3] = false;  // no alpha writes.
    c0.end_time = 300.;
    c0.tex_name = "cas-conveyor";

    BaseFixedAnimDef conveyor_1;
    conveyor_1.tex_name = "cas-conveyor-dest-01";
    conveyor_1.color = math::Vector4<u8>(0, 0, 0, 0x80);
    conveyor_1.override_size = math::Vector2<int>(64, 32);
    auto& c1 = conveyor_1.layers.emplace_back();
    c1.set_blend_b2_d1();
    c1.set_no_z_write_no_z_test();
    // :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
    c1.channel_masks[3] = false;  // no alpha writes.
    c1.end_time = 300.;
    c1.tex_name = "cas-conveyor";

    BaseFixedAnimDef conveyor_2;
    conveyor_2.tex_name = "cas-conveyor-dest-02";
    conveyor_2.color = math::Vector4<u8>(0, 0, 0, 0x80);
    conveyor_2.override_size = math::Vector2<int>(64, 32);
    auto& c2 = conveyor_2.layers.emplace_back();
    c2.set_blend_b2_d1();
    c2.set_no_z_write_no_z_test();
    // :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
    c2.channel_masks[3] = false;  // no alpha writes.
    c2.end_time = 300.;
    c2.tex_name = "cas-conveyor";

    BaseFixedAnimDef conveyor_3;
    conveyor_3.tex_name = "cas-conveyor-dest-03";
    conveyor_3.color = math::Vector4<u8>(0, 0, 0, 0x80);
    conveyor_3.override_size = math::Vector2<int>(64, 32);
    auto& c3 = conveyor_3.layers.emplace_back();
    c3.set_blend_b2_d1();
    c3.set_no_z_write_no_z_test();
    // :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
    c3.channel_masks[3] = false;  // no alpha writes.
    c3.end_time = 300.;
    c3.tex_name = "cas-conveyor";

    m_cas_conveyor_anim_array_idx =
        create_fixed_anim_array({conveyor_0, conveyor_1, conveyor_2, conveyor_3});
  }

  // SECURITY
  {
    BaseFixedAnimDef env;
    env.color = math::Vector4<u8>(0, 0, 0, 0x80);
    env.tex_name = "security-env-dest";
    for (int i = 0; i < 2; i++) {
      auto& env1 = env.layers.emplace_back();
      env1.tex_name = "security-env-uscroll";
      //    :test (new 'static 'gs-test :ate #x1 :afail #x3 :zte #x1 :ztst (gs-ztest always))
      env1.set_no_z_write_no_z_test();
      env1.channel_masks[3] = false;  // no alpha writes.
      //    :alpha (new 'static 'gs-alpha :b #x2 :d #x1)
      env1.set_blend_b2_d1();
      env1.end_time = 4800.f;
    }

    BaseFixedAnimDef dot;
    dot.color = math::Vector4<u8>(0, 0, 0, 0x80);
    dot.tex_name = "security-dot-dest";

    auto& cwhite = dot.layers.emplace_back();
    cwhite.set_blend_b2_d1();
    cwhite.set_no_z_write_no_z_test();
    cwhite.tex_name = "common-white";
    cwhite.end_time = 4800.f;

    for (int i = 0; i < 2; i++) {
      auto& dsrc = dot.layers.emplace_back();
      dsrc.set_blend_b2_d1();
      dsrc.set_no_z_write_no_z_test();
      dsrc.end_time = 600.f;
      dsrc.tex_name = "security-dot-src";
    }

    m_security_anim_array_idx = create_fixed_anim_array({env, dot});
  }

  // WATERFALL
  {
    BaseFixedAnimDef waterfall;
    waterfall.color = math::Vector4<u8>(0, 0, 0, 0x80);
    waterfall.tex_name = "waterfall-dest";
    for (int i = 0; i < 4; i++) {
      auto& src = waterfall.layers.emplace_back();
      src.set_blend_b1_d1();
      src.set_no_z_write_no_z_test();
      src.end_time = 450.f;
      src.tex_name = "waterfall";
    }
    m_waterfall_anim_array_idx = create_fixed_anim_array({waterfall});
  }

  {
    BaseFixedAnimDef waterfall;
    waterfall.color = math::Vector4<u8>(0, 0, 0, 0x80);
    waterfall.tex_name = "waterfall-dest";
    for (int i = 0; i < 4; i++) {
      auto& src = waterfall.layers.emplace_back();
      src.set_blend_b1_d1();
      src.set_no_z_write_no_z_test();
      src.end_time = 450.f;
      src.tex_name = "waterfall";
    }
    m_waterfall_b_anim_array_idx = create_fixed_anim_array({waterfall});
  }

  // LAVA
  {
    BaseFixedAnimDef lava;
    lava.color = math::Vector4<u8>(0, 0, 0, 0x80);
    lava.tex_name = "dig-lava-01-dest";
    for (int i = 0; i < 2; i++) {
      auto& src = lava.layers.emplace_back();
      src.set_blend_b1_d1();
      src.set_no_z_write_no_z_test();
      src.end_time = 3600.f;
      src.tex_name = "dig-lava-01";
    }
    m_lava_anim_array_idx = create_fixed_anim_array({lava});
  }

  {
    BaseFixedAnimDef lava;
    lava.color = math::Vector4<u8>(0, 0, 0, 0x80);
    lava.tex_name = "dig-lava-01-dest";
    for (int i = 0; i < 2; i++) {
      auto& src = lava.layers.emplace_back();
      src.set_blend_b1_d1();
      src.set_no_z_write_no_z_test();
      src.end_time = 3600.f;
      src.tex_name = "dig-lava-01";
    }
    m_lava_b_anim_array_idx = create_fixed_anim_array({lava});
  }

  // Stadiumb
  {
    BaseFixedAnimDef def;
    def.color = math::Vector4<u8>(0, 0, 0, 0x80);
    def.tex_name = "stdmb-energy-wall-01-dest";
    for (int i = 0; i < 2; i++) {
      auto& src = def.layers.emplace_back();
      src.set_blend_b1_d1();
      src.set_no_z_write_no_z_test();
      src.end_time = 300.f;
      src.tex_name = "stdmb-energy-wall-01";
    }
    m_stadiumb_anim_array_idx = create_fixed_anim_array({def});
  }

  // Fortress pris
  {
    BaseFixedAnimDef l_tread;
    l_tread.color = math::Vector4<u8>(0, 0, 0, 0x80);
    l_tread.tex_name = "robotank-tread-l-dest";
    auto& l_src = l_tread.layers.emplace_back();
    l_src.set_blend_b1_d1();
    l_src.set_no_z_write_no_z_test();
    l_src.channel_masks[3] = false;  // no alpha writes.
    l_src.end_time = 1.f;
    l_src.tex_name = "robotank-tread";

    BaseFixedAnimDef r_tread;
    r_tread.color = math::Vector4<u8>(0, 0, 0, 0x80);
    r_tread.tex_name = "robotank-tread-r-dest";
    auto& r_src = r_tread.layers.emplace_back();
    r_src.set_blend_b1_d1();
    r_src.set_no_z_write_no_z_test();
    r_src.channel_masks[3] = false;  // no alpha writes.
    r_src.end_time = 1.f;
    r_src.tex_name = "robotank-tread";

    m_fortress_pris_anim_array_idx = create_fixed_anim_array({l_tread, r_tread});
  }

  // Fortress Warp
  {
    BaseFixedAnimDef def;
    def.color = math::Vector4<u8>(0, 0, 0, 0x80);
    def.move_to_pool = true;
    def.tex_name = "fort-roboscreen-dest";
    auto& src = def.layers.emplace_back();
    src.set_blend_b2_d1();
    src.channel_masks[3] = false;  // no alpha writes.
    src.set_no_z_write_no_z_test();
    src.end_time = 300.f;
    src.tex_name = "fort-roboscreen-env";
    m_fortress_warp_anim_array_idx = create_fixed_anim_array({def});
  }

  // metkor
  {
    BaseFixedAnimDef def;
    def.color = math::Vector4<u8>(0, 0, 0, 0x80);
    def.tex_name = "squid-env-rim-dest";
    def.move_to_pool = true;
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-noise";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-scan";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-rim";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-rim";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "environment-phong-rim";
    }
    m_metkor_anim_array_idx = create_fixed_anim_array({def});
  }

  // shield
  {
    BaseFixedAnimDef def;
    def.color = math::Vector4<u8>(0, 0, 0, 0x80);
    def.tex_name = "squid-env-rim-dest";
    def.move_to_pool = true;

    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "common-white";
    }

    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "squid-env-uscroll";
    }

    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "squid-env-uscroll";
    }

    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "squid-env-rim-src";
    }

    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "squid-env-rim-src";
    }
    m_shield_anim_array_idx = create_fixed_anim_array({def});
  }

  // krew
  {
    BaseFixedAnimDef def;
    def.color = math::Vector4<u8>(0, 0, 0, 0x80);
    def.tex_name = "krew-holo-dest";
    def.move_to_pool = true;
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-noise";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-scan";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-rim";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-head-env-rim";
    }
    {
      auto& src = def.layers.emplace_back();
      src.set_blend_b2_d1();
      src.channel_masks[3] = false;  // no alpha writes.
      src.set_no_z_write_no_z_test();
      src.set_clamp();
      src.end_time = 1200.f;
      src.tex_name = "metkor-phong-env";
    }
    m_krew_holo_anim_array_idx = create_fixed_anim_array({def});
  }
}

int BaseTextureAnimator::create_clut_blender_group(const std::vector<std::string>& textures,
                                                   const std::string& suffix0,
                                                   const std::string& suffix1,
                                                   const std::optional<std::string>& dgo) {
  int ret = get_clut_blender_groups_size();
  clut_blender_groups_emplace_back();
  add_to_clut_blender_group(ret, textures, suffix0, suffix1, dgo);
  return ret;
}

BaseTextureAnimator::~BaseTextureAnimator() {}

/*!
 * Main function to run texture animations from DMA. Updates textures in the pool.
 */
void BaseTextureAnimator::handle_texture_anim_data(DmaFollower& dma, const u8* ee_mem) {
  clear_in_use_temp_texture();

  // loop over DMA, and do the appropriate texture operations.
  // this will fill out m_textures, which is keyed on TBP.
  // as much as possible, we keep around buffers/textures.
  // this will also record which tbp's have been "erased", for the next step.
  bool done = false;
  while (!done) {
    u32 offset = dma.current_tag_offset();
    auto tf = dma.read_and_advance();
    auto vif0 = tf.vifcode0();
    if (vif0.kind == VifCode::Kind::PC_PORT) {
      switch (vif0.immediate) {
        case UPLOAD_CLUT_16_16: {
          auto p = profiler::scoped_prof("clut-16-16");
          handle_upload_clut_16_16(tf, ee_mem);
        } break;
        case ERASE_DEST_TEXTURE: {
          auto p = profiler::scoped_prof("erase");
          handle_erase_dest(dma);
        } break;
        case GENERIC_UPLOAD: {
          auto p = profiler::scoped_prof("generic-upload");
          handle_generic_upload(tf, ee_mem);
        } break;
        case SET_SHADER: {
          auto p = profiler::scoped_prof("set-shader");
          handle_set_shader(dma);
        } break;
        case DRAW: {
          auto p = profiler::scoped_prof("draw");
          handle_draw(dma);
        } break;
        case FINISH_ARRAY:
          done = true;
          break;
        case DARKJAK: {
          auto p = profiler::scoped_prof("darkjak");
          run_clut_blender_group(tf, m_darkjak_clut_blender_idx);
        } break;
        case PRISON_JAK: {
          auto p = profiler::scoped_prof("prisonjak");
          run_clut_blender_group(tf, m_jakb_prison_clut_blender_idx);
        } break;
        case ORACLE_JAK: {
          auto p = profiler::scoped_prof("oraclejak");
          run_clut_blender_group(tf, m_jakb_oracle_clut_blender_idx);
        } break;
        case NEST_JAK: {
          auto p = profiler::scoped_prof("nestjak");
          run_clut_blender_group(tf, m_jakb_nest_clut_blender_idx);
        } break;
        case KOR_TRANSFORM: {
          auto p = profiler::scoped_prof("kor");
          run_clut_blender_group(tf, m_kor_transform_clut_blender_idx);
        } break;
        default:
          fmt::print("bad imm: {}\n", vif0.immediate);
          ASSERT_NOT_REACHED();
      }
    } else {
      printf("[tex anim] unhandled VIF in main loop\n");
      fmt::print("{} {}\n", vif0.print(), tf.vifcode1().print());
      fmt::print("dma address 0x{:x}\n", offset);
      ASSERT_NOT_REACHED();
    }
  }

  // The steps above will populate m_textures with some combination of GPU/CPU textures.
  // we need to make sure that all final textures end up on the GPU. For now, we detect this by
  // seeing if the "erase" operation ran on an tbp, indicating that it was cleared, which is
  // always done to all textures by the GOAL code.
  for (auto tbp : m_erased_on_this_frame) {
    auto p = profiler::scoped_prof("handle-one-erased");
    force_to_gpu(tbp);
  }

  update_and_move_texture_data_to_pool();
}

/*!
 * Handle the initialization of an animated texture. This fills the entire texture with a solid
 * color. We set up a GPU texture here - drawing operations are done on the GPU, so we'd never
 * need this solid color on the CPU. Also sets a bunch of GS state for the shaders.
 * These may be modified by animation functions, but most of the time they aren't.
 */
void BaseTextureAnimator::handle_erase_dest(DmaFollower& dma) {
  // auto& out = m_new_dest_textures.emplace_back();
  int dest_texture_address, tex_width, tex_height;
  dest_texture_address = tex_width = tex_height = 0;
  math::Vector<u32, 4> rgba_u32{};

  // first transfer will be a bunch of ad (modifies the shader)
  auto ad_transfer = dma.read_and_advance();
  ASSERT(ad_transfer.size_bytes == 10 * 16);
  ASSERT(ad_transfer.vifcode0().kind == VifCode::Kind::FLUSHA);
  ASSERT(ad_transfer.vifcode1().kind == VifCode::Kind::DIRECT);
  const u64* ad_data = (const u64*)(ad_transfer.data + 16);

  // for (int i = 0; i < 9; i++) {
  // dprintf(" ad: 0x%lx 0x%lx\n", ad_data[i * 2], ad_data[i * 2 + 1]);
  // }
  // 0 (scissor-1 (new 'static 'gs-scissor :scax1 (+ tex-width -1) :scay1 (+ tex-height -1)))
  ASSERT(ad_data[0 * 2 + 1] == (int)GsRegisterAddress::SCISSOR_1);
  GsScissor scissor(ad_data[0]);
  tex_width = scissor.x1() + 1;
  tex_height = scissor.y1() + 1;
  dprintf(" size: %d x %d\n", tex_width, tex_height);

  // 1 (xyoffset-1 (new 'static 'gs-xy-offset :ofx #x8000 :ofy #x8000))
  // 2 (frame-1 (new 'static 'gs-frame :fbw (/ (+ tex-width 63) 64) :fbp fbp-for-tex))
  ASSERT(ad_data[2 * 2 + 1] == (int)GsRegisterAddress::FRAME_1);
  GsFrame frame(ad_data[2 * 2]);
  dest_texture_address = 32 * frame.fbp();
  dprintf(" dest: 0x%x\n", dest_texture_address);

  // 3 (test-1 (-> anim test))
  ASSERT(ad_data[2 * 3 + 1] == (int)GsRegisterAddress::TEST_1);
  m_current_shader.test = GsTest(ad_data[3 * 2]);
  dfmt(" test: {}", m_current_shader.test.print());

  // 4 (alpha-1 (-> anim alpha))
  ASSERT(ad_data[2 * 4 + 1] == (int)GsRegisterAddress::ALPHA_1);
  m_current_shader.alpha = GsAlpha(ad_data[4 * 2]);
  dfmt(" alpha: {}\n", m_current_shader.alpha.print());

  // 5 (clamp-1 (-> anim clamp))
  ASSERT(ad_data[2 * 5 + 1] == (int)GsRegisterAddress::CLAMP_1);
  u64 creg = ad_data[5 * 2];
  m_current_shader.clamp_u = creg & 0b001;
  m_current_shader.clamp_v = creg & 0b100;
  u64 mask = ~0b101;
  ASSERT((creg & mask) == 0);
  dfmt(" clamp: {} {}\n", m_current_shader.clamp_u, m_current_shader.clamp_v);

  // 6 (texa (new 'static 'gs-texa :ta0 #x80 :ta1 #x80))
  // 7 (zbuf-1 (new 'static 'gs-zbuf :zbp #x130 :psm (gs-psm ct24) :zmsk #x1))
  // 8 (texflush 0)

  // next transfer is the erase. This is done with alpha blending off
  auto clear_transfer = dma.read_and_advance();
  ASSERT(clear_transfer.size_bytes == 16 * 4);
  memcpy(rgba_u32.data(), clear_transfer.data + 16, 16);
  dfmt(" clear: {}\n", rgba_u32.to_string_hex_byte());

  handle_graphics_erase_dest(dma, tex_width, tex_height, dest_texture_address, rgba_u32);
}

/*!
 * Create an entry for a 16x16 clut texture upload. Leaves it on the CPU.
 * They upload cluts as PSM32, so there's no funny addressing stuff, other than
 * the CLUT indexing scramble stuff.
 */
void BaseTextureAnimator::handle_upload_clut_16_16(const DmaTransfer& tf, const u8* ee_mem) {
  dprintf("[tex anim] upload clut 16 16\n");
  ASSERT(tf.size_bytes == sizeof(TextureAnimPcUpload));
  auto* upload = (const TextureAnimPcUpload*)(tf.data);
  ASSERT(upload->width == 16);
  ASSERT(upload->height == 16);
  dprintf("  dest is 0x%x\n", upload->dest);
  auto& vram = get_vram_entry_at_index(upload->dest);
  vram.reset();
  vram.kind = BaseVramEntry::Kind::CLUT16_16_IN_PSM32;
  vram.data.resize(16 * 16 * 4);
  vram.tex_width = upload->width;
  vram.tex_height = upload->height;
  memcpy(vram.data.data(), ee_mem + upload->data, vram.data.size());
  if (m_tex_looking_for_clut) {
    m_tex_looking_for_clut->cbp = upload->dest;
    m_tex_looking_for_clut = nullptr;
  }
}

/*!
 * ADGIF shader update
 */
void BaseTextureAnimator::handle_set_shader(DmaFollower& dma) {
  dprintf("[tex anim] set shader\n");
  auto ad_transfer = dma.read_and_advance();
  const int num_regs = (ad_transfer.size_bytes - 16) / 16;
  ASSERT(ad_transfer.vifcode0().kind == VifCode::Kind::NOP ||
         ad_transfer.vifcode0().kind == VifCode::Kind::FLUSHA);
  ASSERT(ad_transfer.vifcode1().kind == VifCode::Kind::DIRECT);
  const u64* ad_data = (const u64*)(ad_transfer.data + 16);

  for (int i = 0; i < num_regs; i++) {
    u64 addr = ad_data[i * 2 + 1];
    u64 data = ad_data[i * 2];

    switch (GsRegisterAddress(addr)) {
      case GsRegisterAddress::TEX0_1:
        m_current_shader.tex0 = GsTex0(data);
        m_current_shader.source_texture_set = true;
        dfmt(" tex0: {}", m_current_shader.tex0.print());
        break;
      case GsRegisterAddress::TEX1_1:
        m_current_shader.tex1 = GsTex1(data);
        dfmt(" tex1: {}", m_current_shader.tex1.print());
        break;
      case GsRegisterAddress::TEST_1:
        m_current_shader.test = GsTest(data);
        dfmt(" test: {}", m_current_shader.test.print());
        break;
      case GsRegisterAddress::ALPHA_1:
        m_current_shader.alpha = GsAlpha(data);
        dfmt(" alpha: {}\n", m_current_shader.alpha.print());
        break;
      case GsRegisterAddress::CLAMP_1:
        m_current_shader.clamp_u = data & 0b001;
        m_current_shader.clamp_v = data & 0b100;
        ASSERT((data & (~(u64(0b101)))) == 0);
        dfmt(" clamp: {} {}\n", m_current_shader.clamp_u, m_current_shader.clamp_v);
        break;
      default:
        dfmt("unknown reg {}\n", addr);
        ASSERT_NOT_REACHED();
    }
  }
}

/*!
 * Get a 16x16 CLUT texture, stored in psm32 (in-memory format, not vram). Fatal if it doesn't
 * exist.
 */
const u32* BaseTextureAnimator::get_clut_16_16_psm32(int cbp) {
  if (!is_vram_entry_available_at_index(cbp)) {
    printf("get_clut_16_16_psm32 referenced an unknown clut texture in %d\n", cbp);
    ASSERT_NOT_REACHED();
  }
  const auto& clut_lookup = get_vram_entry_at_index(cbp);
  if (clut_lookup.kind != BaseVramEntry::Kind::CLUT16_16_IN_PSM32) {
    ASSERT_NOT_REACHED();
  }

  return (const u32*)clut_lookup.data.data();
}

/*!
 * Using the current shader settings, load the CLUT table to the texture coverter "VRAM".
 */
void BaseTextureAnimator::load_clut_to_converter() {
  if (!is_vram_entry_available_at_index(m_current_shader.tex0.cbp())) {
    printf("set shader referenced an unknown clut texture in %d\n", m_current_shader.tex0.cbp());
    ASSERT_NOT_REACHED();
  }

  const auto& clut_lookup = get_vram_entry_at_index(m_current_shader.tex0.cbp());
  switch (clut_lookup.kind) {
    case BaseVramEntry::Kind::CLUT16_16_IN_PSM32:
      m_converter.upload_width(clut_lookup.data.data(), m_current_shader.tex0.cbp(), 16, 16);
      break;
    default:
      printf("unhandled clut source kind: %d\n", (int)clut_lookup.kind);
      ASSERT_NOT_REACHED();
  }
}

void BaseTextureAnimator::loop_over_dma_tex_anims_operations(DmaFollower& dma,
                                                             const u8* ee_mem,
                                                             u64 frame_idx) {
  // loop over DMA, and do the appropriate texture operations.
  // this will fill out m_textures, which is keyed on TBP.
  // as much as possible, we keep around buffers/textures.
  // this will also record which tbp's have been "erased", for the next step.
  bool done = false;
  while (!done) {
    u32 offset = dma.current_tag_offset();
    auto tf = dma.read_and_advance();
    auto vif0 = tf.vifcode0();
    if (vif0.kind == VifCode::Kind::PC_PORT) {
      switch (vif0.immediate) {
        case UPLOAD_CLUT_16_16: {
          auto p = profiler::scoped_prof("clut-16-16");
          handle_upload_clut_16_16(tf, ee_mem);
        } break;
        case ERASE_DEST_TEXTURE: {
          auto p = profiler::scoped_prof("erase");
          handle_erase_dest(dma);
        } break;
        case GENERIC_UPLOAD: {
          auto p = profiler::scoped_prof("generic-upload");
          handle_generic_upload(tf, ee_mem);
        } break;
        case SET_SHADER: {
          auto p = profiler::scoped_prof("set-shader");
          handle_set_shader(dma);
        } break;
        case DRAW: {
          auto p = profiler::scoped_prof("draw");
          handle_draw(dma);
        } break;
        case FINISH_ARRAY:
          done = true;
          break;
        case DARKJAK: {
          auto p = profiler::scoped_prof("darkjak");
          run_clut_blender_group(tf, m_darkjak_clut_blender_idx, frame_idx);
        } break;
        case PRISON_JAK: {
          auto p = profiler::scoped_prof("prisonjak");
          run_clut_blender_group(tf, m_jakb_prison_clut_blender_idx, frame_idx);
        } break;
        case ORACLE_JAK: {
          auto p = profiler::scoped_prof("oraclejak");
          run_clut_blender_group(tf, m_jakb_oracle_clut_blender_idx, frame_idx);
        } break;
        case NEST_JAK: {
          auto p = profiler::scoped_prof("nestjak");
          run_clut_blender_group(tf, m_jakb_nest_clut_blender_idx, frame_idx);
        } break;
        case KOR_TRANSFORM: {
          auto p = profiler::scoped_prof("kor");
          run_clut_blender_group(tf, m_kor_transform_clut_blender_idx, frame_idx);
        } break;
        case SKULL_GEM: {
          auto p = profiler::scoped_prof("skull-gem");
          run_fixed_animation_array(m_skull_gem_fixed_anim_array_idx, tf);
        } break;
        case BOMB: {
          auto p = profiler::scoped_prof("bomb");
          run_fixed_animation_array(m_bomb_fixed_anim_array_idx, tf);
        } break;
        case CAS_CONVEYOR: {
          auto p = profiler::scoped_prof("cas-conveyor");
          run_fixed_animation_array(m_cas_conveyor_anim_array_idx, tf);
        } break;
        case SECURITY: {
          auto p = profiler::scoped_prof("security");
          run_fixed_animation_array(m_security_anim_array_idx, tf);
        } break;
        case WATERFALL: {
          auto p = profiler::scoped_prof("waterfall");
          run_fixed_animation_array(m_waterfall_anim_array_idx, tf);
        } break;
        case WATERFALL_B: {
          auto p = profiler::scoped_prof("waterfall-b");
          run_fixed_animation_array(m_waterfall_b_anim_array_idx, tf);
        } break;
        case LAVA: {
          auto p = profiler::scoped_prof("lava");
          run_fixed_animation_array(m_lava_anim_array_idx, tf);
        } break;
        case LAVA_B: {
          auto p = profiler::scoped_prof("lava-b");
          run_fixed_animation_array(m_lava_b_anim_array_idx, tf);
        } break;
        case STADIUMB: {
          auto p = profiler::scoped_prof("stadiumb");
          run_fixed_animation_array(m_stadiumb_anim_array_idx, tf);
        } break;
        case FORTRESS_PRIS: {
          auto p = profiler::scoped_prof("fort-pris");
          run_fixed_animation_array(m_fortress_pris_anim_array_idx, tf);
        } break;
        case FORTRESS_WARP: {
          auto p = profiler::scoped_prof("fort-warp");
          run_fixed_animation_array(m_fortress_warp_anim_array_idx, tf);
        } break;
        case METKOR: {
          auto p = profiler::scoped_prof("metkor");
          run_fixed_animation_array(m_metkor_anim_array_idx, tf);
        } break;
        case SHIELD: {
          auto p = profiler::scoped_prof("shield");
          run_fixed_animation_array(m_shield_anim_array_idx, tf);
        } break;
        case KREW_HOLO: {
          auto p = profiler::scoped_prof("krew-holo");
          run_fixed_animation_array(m_krew_holo_anim_array_idx, tf);
        } break;
        case CLOUDS_AND_FOG: {
          auto p = profiler::scoped_prof("clouds-and-fog");
          handle_clouds_and_fog(tf);
        } break;
        case SLIME: {
          auto p = profiler::scoped_prof("slime");
          handle_slime(tf);
        } break;
        default:
          fmt::print("bad imm: {}\n", vif0.immediate);
          ASSERT_NOT_REACHED();
      }
    } else {
      printf("[tex anim] unhandled VIF in main loop\n");
      fmt::print("{} {}\n", vif0.print(), tf.vifcode1().print());
      fmt::print("dma address 0x{:x}\n", offset);
      ASSERT_NOT_REACHED();
    }
  }
}

int BaseTextureAnimator::get_private_output_slots_id(int idx) {
  return idx;
}

void BaseTextureAnimator::draw_debug_window() {
  ImGui::Checkbox("fast-scrambler", &m_debug.use_fast_scrambler);

  ImGui::Text("Slime:");
  ImGui::Text("dests %d %d", m_debug_slime_input.dest, m_debug_slime_input.scroll_dest);
  for (int i = 0; i < 9; i++) {
    ImGui::Text("Time[%d]: %f", i, m_debug_slime_input.times[i]);
  }
  imgui_show_final_slime_tex();
  imgui_show_final_slime_scroll_tex();

  ImGui::Text("Sky:");
  ImGui::Text("Fog Height: %f", m_debug_sky_input.fog_height);
  ImGui::Text("Cloud minmax: %f %f", m_debug_sky_input.cloud_min, m_debug_sky_input.cloud_max);
  for (int i = 0; i < 9; i++) {
    ImGui::Text("Time[%d]: %f", i, m_debug_sky_input.times[i]);
  }
  ImGui::Text("Dest %d", m_debug_sky_input.cloud_dest);

  imgui_show_sky_blend_tex();
  imgui_show_sky_final_tex();

  auto& slots = jak2_animated_texture_slots();
  for (size_t i = 0; i < slots.size(); i++) {
    ImGui::Text("Slot %d %s (%d)", (int)i, slots[i].c_str(), get_private_output_slots_id(i));
    imgui_show_private_output_slots_at_index(i);
    ImGui::Checkbox(fmt::format("mark {}", i).c_str(), (bool*)&m_output_debug_flags.at(i));
  }
}

namespace {
void convert_gs_position_to_vec3(float* out, const math::Vector<u32, 4>& in, int w, int h) {
  out[0] = ((((float)in.x()) / 16.f) - 2048.f) / (float)w;
  out[1] = ((((float)in.y()) / 16.f) - 2048.f) / (float)h;
  out[2] = 0;  // in.z();  // don't think it matters
}

void convert_gs_uv_to_vec2(float* out, const math::Vector<float, 4>& in) {
  out[0] = in.x();
  out[1] = in.y();
}
}  // namespace

void BaseTextureAnimator::set_uniforms_from_draw_data(const DrawData& dd, int dest_w, int dest_h) {
  const auto& vf = dd.color.cast<float>();
  set_uniform_vector_four_float(vf.x(), vf.y(), vf.z(), vf.w());

  float pos[3 * 4 + 1];
  convert_gs_position_to_vec3(pos, dd.pos0, dest_w, dest_h);
  convert_gs_position_to_vec3(pos + 3, dd.pos1, dest_w, dest_h);
  convert_gs_position_to_vec3(pos + 6, dd.pos2, dest_w, dest_h);
  convert_gs_position_to_vec3(pos + 9, dd.pos3, dest_w, dest_h);
  set_uniform_vector_three_float(pos);
  //  for (int i = 0; i < 4; i++) {
  //    fmt::print("fan vp {}: {:.3f} {:.3f} {:.3f}\n", i, pos[i * 3], pos[1 + i * 3], pos[2 + i *
  //    3]);
  //  }

  float uv[2 * 4];
  convert_gs_uv_to_vec2(uv, dd.st0);
  convert_gs_uv_to_vec2(uv + 2, dd.st1);
  convert_gs_uv_to_vec2(uv + 4, dd.st2);
  convert_gs_uv_to_vec2(uv + 6, dd.st3);
  set_uniform_vector_two_float(uv);
  //  for (int i = 0; i < 4; i++) {
  //    fmt::print("fan vt {}: {:.3f} {:.3f} \n", i, uv[i * 2], uv[1 + i * 2]);
  //  }
}
