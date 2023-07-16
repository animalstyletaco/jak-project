#include "TextureAnimator.h"

#include "common/global_profiler/GlobalProfiler.h"
#include "common/texture/texture_slots.h"
#include "common/util/FileUtil.h"
#include "common/util/Timer.h"

#include "common/log/log.h"

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
    : m_common_level(common_level) {
  //Initialize graphics

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

BaseTextureAnimator::~BaseTextureAnimator() {
}

/*!
 * Main function to run texture animations from DMA. Updates textures in the pool.
 */
void BaseTextureAnimator::handle_texture_anim_data(DmaFollower& dma,
                                               const u8* ee_mem) {
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
        case MOVE_RG_TO_BA: {
          auto p = profiler::scoped_prof("rg-to-ba");
          handle_rg_to_ba(tf);
        } break;
        case SET_CLUT_ALPHA: {
          auto p = profiler::scoped_prof("set-clut-alpha");
          handle_set_clut_alpha(tf);
        } break;
        case COPY_CLUT_ALPHA: {
          auto p = profiler::scoped_prof("copy-clut-alpha");
          handle_copy_clut_alpha(tf);
        } break;
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
 * Create an entry for any texture upload. Leaves it on the CPU, as we may do fancy scramble stuff.
 */
void BaseTextureAnimator::handle_generic_upload(const DmaTransfer& tf, const u8* ee_mem) {
  dprintf("[tex anim] upload generic @ 0x%lx\n", tf.data - ee_mem);
  ASSERT(tf.size_bytes == sizeof(TextureAnimPcUpload));
  auto* upload = (const TextureAnimPcUpload*)(tf.data);
  dprintf(" size %d x %d\n", upload->width, upload->height);
  dprintf(" dest is 0x%x\n", upload->dest);
  auto& vram = get_vram_entry_at_index(upload->dest);
  vram.reset();

  switch (upload->format) {
    case (int)GsTex0::PSM::PSMCT32:
      vram.kind = BaseVramEntry::Kind::GENERIC_PSM32;
      vram.data.resize(upload->width * upload->height * 4);
      vram.tex_width = upload->width;
      vram.tex_height = upload->height;
      memcpy(vram.data.data(), ee_mem + upload->data, vram.data.size());
      m_tex_looking_for_clut = nullptr;
      break;
    case (int)GsTex0::PSM::PSMT8:
      vram.kind = BaseVramEntry::Kind::GENERIC_PSMT8;
      vram.data.resize(upload->width * upload->height);
      vram.tex_width = upload->width;
      vram.tex_height = upload->height;
      memcpy(vram.data.data(), ee_mem + upload->data, vram.data.size());
      m_tex_looking_for_clut = &vram;
      break;
    default:
      fmt::print("Unhandled format: {}\n", upload->format);
      ASSERT_NOT_REACHED();
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
      m_converter.upload_width(clut_lookup.data.data(), m_current_shader.tex0.cbp(), 16,
                               16);
      break;
    default:
      printf("unhandled clut source kind: %d\n", (int)clut_lookup.kind);
      ASSERT_NOT_REACHED();
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
