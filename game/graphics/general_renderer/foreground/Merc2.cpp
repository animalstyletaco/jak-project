#include "BaseMerc2.h"

#include "game/graphics/opengl_renderer/background/background_common.h"

#include "third-party/imgui/imgui.h"

BaseMerc2::BaseMerc2(const std::string& name, int my_id) : BucketRenderer(name, my_id) {
}

/*!
 * Handle the merc renderer switching to a different model.
 */
void BaseMerc2::init_pc_model(const DmaTransfer& setup, SharedRenderState* render_state) {
  // determine the name. We've packed this in a separate PC-port specific packet.
  char name[128];
  strcpy(name, (const char*)setup.data);

  // get the model from the loader
  m_current_model = render_state->loader->get_merc_model(name);

  // update stats
  m_stats.num_models++;
  if (m_current_model) {
    for (const auto& effect : m_current_model->model->effects) {
      m_stats.num_effects++;
      m_stats.num_predicted_draws += effect.draws.size();
      for (const auto& draw : effect.draws) {
        m_stats.num_predicted_tris += draw.num_triangles;
      }
    }
  }
}

/*!
 * Once-per-frame initialization
 */
void BaseMerc2::init_for_frame(SharedRenderState* render_state) {
  // reset state
  m_current_model = std::nullopt;
  m_stats = {};

  // activate the merc shader used for all draws
  render_state->shaders[ShaderId::BaseMerc2].activate();

  // set uniforms that we know from render_state
  glUniform4f(m_uniforms.fog_color, render_state->fog_color[0] / 255.f,
              render_state->fog_color[1] / 255.f, render_state->fog_color[2] / 255.f,
              render_state->fog_intensity / 255);
  glUniform1i(m_uniforms.gfx_hack_no_tex, Gfx::g_global_settings.hack_no_tex);
}

void BaseMerc2::draw_debug_window() {
  ImGui::Text("Models   : %d", m_stats.num_models);
  ImGui::Text("Effects  : %d", m_stats.num_effects);
  ImGui::Text("Draws (p): %d", m_stats.num_predicted_draws);
  ImGui::Text("Tris  (p): %d", m_stats.num_predicted_tris);
  ImGui::Text("Bones    : %d", m_stats.num_bones_uploaded);
  ImGui::Text("Lights   : %d", m_stats.num_lights);
  ImGui::Text("Dflush   : %d", m_stats.num_draw_flush);
}

void BaseMerc2::init_shaders(ShaderLibrary& shaders) {
  const auto& shader = shaders[ShaderId::BaseMerc2];
  auto id = shader.id();
  shaders[ShaderId::BaseMerc2].activate();
  m_uniforms.light_direction[0] = glGetUniformLocation(id, "light_dir0");
  m_uniforms.light_direction[1] = glGetUniformLocation(id, "light_dir1");
  m_uniforms.light_direction[2] = glGetUniformLocation(id, "light_dir2");
  m_uniforms.light_color[0] = glGetUniformLocation(id, "light_col0");
  m_uniforms.light_color[1] = glGetUniformLocation(id, "light_col1");
  m_uniforms.light_color[2] = glGetUniformLocation(id, "light_col2");
  m_uniforms.light_ambient = glGetUniformLocation(id, "light_ambient");

  m_uniforms.hvdf_offset = glGetUniformLocation(id, "hvdf_offset");
  m_uniforms.perspective[0] = glGetUniformLocation(id, "perspective0");
  m_uniforms.perspective[1] = glGetUniformLocation(id, "perspective1");
  m_uniforms.perspective[2] = glGetUniformLocation(id, "perspective2");
  m_uniforms.perspective[3] = glGetUniformLocation(id, "perspective3");

  m_uniforms.fog = glGetUniformLocation(id, "fog_constants");
  m_uniforms.decal = glGetUniformLocation(id, "decal_enable");

  m_uniforms.fog_color = glGetUniformLocation(id, "fog_color");
  m_uniforms.perspective_matrix = glGetUniformLocation(id, "perspective_matrix");
  m_uniforms.ignore_alpha = glGetUniformLocation(id, "ignore_alpha");

  m_uniforms.gfx_hack_no_tex = glGetUniformLocation(id, "gfx_hack_no_tex");
}

/*!
 * Main BaseMerc2 rendering.
 */
void BaseMerc2::render(DmaFollower& dma, SharedRenderState* render_state, ScopedProfilerNode& prof) {
  // skip if disabled
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  init_for_frame(render_state);

  // iterate through the dma chain, filling buckets
  handle_all_dma(dma, render_state, prof);

  // flush model data to buckets
  flush_pending_model(render_state, prof);
  // flush buckets to draws
  flush_draw_buckets(render_state, prof);
}

u32 BaseMerc2::alloc_lights(const VuLights& lights) {
  ASSERT(m_next_free_light < MAX_LIGHTS);
  m_stats.num_lights++;
  u32 light_idx = m_next_free_light;
  m_lights_buffer[m_next_free_light++] = lights;
  static_assert(sizeof(VuLights) == 7 * 16);
  return light_idx;
}

/*!
 * Store light values
 */
void BaseMerc2::set_lights(const DmaTransfer& dma) {
  memcpy(&m_current_lights, dma.data, sizeof(VuLights));
}

void BaseMerc2::handle_matrix_dma(const DmaTransfer& dma) {
  int slot = dma.vif0() & 0xff;
  ASSERT(slot < MAX_SKEL_BONES);
  memcpy(&m_skel_matrix_buffer[slot], dma.data, sizeof(MercMat));
}

/*!
 * Main BaseMerc2 function to handle DMA
 */
void BaseMerc2::handle_all_dma(DmaFollower& dma,
                           SharedRenderState* render_state,
                           ScopedProfilerNode& prof) {
  // process the first tag. this is just jumping to the merc-specific dma.
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.size_bytes == 0);
  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }
  ASSERT(data0.size_bytes == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.vif1() == 0);

  // if we reach here, there's stuff to draw
  // this handles merc-specific setup DMA
  handle_setup_dma(dma);

  // handle each merc transfer
  while (dma.current_tag_offset() != render_state->next_bucket) {
    handle_merc_chain(dma, render_state, prof);
  }
  ASSERT(dma.current_tag_offset() == render_state->next_bucket);
}

namespace {
void set_uniform(GLuint uniform, const math::Vector3f& val) {
  glUniform3f(uniform, val.x(), val.y(), val.z());
}
void set_uniform(GLuint uniform, const math::Vector4f& val) {
  glUniform4f(uniform, val.x(), val.y(), val.z(), val.w());
}
}  // namespace

void BaseMerc2::handle_setup_dma(DmaFollower& dma) {
  auto first = dma.read_and_advance();

  // 10 quadword setup packet
  ASSERT(first.size_bytes == 10 * 16);
  // m_stats.str += fmt::format("Setup 0: {} {} {}", first.size_bytes / 16,
  // first.vifcode0().print(), first.vifcode1().print());

  // transferred vifcodes
  {
    auto vif0 = first.vifcode0();
    auto vif1 = first.vifcode1();
    // STCYCL 4, 4
    ASSERT(vif0.kind == VifCode::Kind::STCYCL);
    auto vif0_st = VifCodeStcycl(vif0);
    ASSERT(vif0_st.cl == 4 && vif0_st.wl == 4);
    // STMOD
    ASSERT(vif1.kind == VifCode::Kind::STMOD);
    ASSERT(vif1.immediate == 0);
  }

  // 1 qw with 4 vifcodes.
  u32 vifcode_data[4];
  memcpy(vifcode_data, first.data, 16);
  {
    auto vif0 = VifCode(vifcode_data[0]);
    ASSERT(vif0.kind == VifCode::Kind::BASE);
    ASSERT(vif0.immediate == MercDataMemory::BUFFER_BASE);
    auto vif1 = VifCode(vifcode_data[1]);
    ASSERT(vif1.kind == VifCode::Kind::OFFSET);
    ASSERT((s16)vif1.immediate == MercDataMemory::BUFFER_OFFSET);
    auto vif2 = VifCode(vifcode_data[2]);
    ASSERT(vif2.kind == VifCode::Kind::NOP);
    auto vif3 = VifCode(vifcode_data[3]);
    ASSERT(vif3.kind == VifCode::Kind::UNPACK_V4_32);
    VifCodeUnpack up(vif3);
    ASSERT(up.addr_qw == MercDataMemory::LOW_MEMORY);
    ASSERT(!up.use_tops_flag);
    ASSERT(vif3.num == 8);
  }

  // 8 qw's of low memory data
  memcpy(&m_low_memory, first.data + 16, sizeof(LowMemory));
  set_uniform(m_uniforms.hvdf_offset, m_low_memory.hvdf_offset);
  set_uniform(m_uniforms.fog, m_low_memory.fog);
  for (int i = 0; i < 4; i++) {
    set_uniform(m_uniforms.perspective[i], m_low_memory.perspective[i]);
  }
  // todo rm.
  glUniformMatrix4fv(m_uniforms.perspective_matrix, 1, GL_FALSE, &m_low_memory.perspective[0].x());

  // 1 qw with another 4 vifcodes.
  u32 vifcode_final_data[4];
  memcpy(vifcode_final_data, first.data + 16 + sizeof(LowMemory), 16);
  {
    ASSERT(VifCode(vifcode_final_data[0]).kind == VifCode::Kind::FLUSHE);
    ASSERT(vifcode_final_data[1] == 0);
    ASSERT(vifcode_final_data[2] == 0);
    VifCode mscal(vifcode_final_data[3]);
    ASSERT(mscal.kind == VifCode::Kind::MSCAL);
    ASSERT(mscal.immediate == 0);
  }

  // TODO: process low memory initialization

  auto second = dma.read_and_advance();
  ASSERT(second.size_bytes == 32);  // setting up test register.
  auto nothing = dma.read_and_advance();
  ASSERT(nothing.size_bytes == 0);
  ASSERT(nothing.vif0() == 0);
  ASSERT(nothing.vif1() == 0);
}

namespace {
bool tag_is_nothing_next(const DmaFollower& dma) {
  return dma.current_tag().kind == DmaTag::Kind::NEXT && dma.current_tag().qwc == 0 &&
         dma.current_tag_vif0() == 0 && dma.current_tag_vif1() == 0;
}
bool tag_is_nothing_cnt(const DmaFollower& dma) {
  return dma.current_tag().kind == DmaTag::Kind::CNT && dma.current_tag().qwc == 0 &&
         dma.current_tag_vif0() == 0 && dma.current_tag_vif1() == 0;
}
}  // namespace

void BaseMerc2::handle_merc_chain(DmaFollower& dma,
                              SharedRenderState* render_state,
                              ScopedProfilerNode& prof) {
  while (tag_is_nothing_next(dma)) {
    auto nothing = dma.read_and_advance();
    ASSERT(nothing.size_bytes == 0);
  }
  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    return;
  }

  auto init = dma.read_and_advance();

  if (init.vifcode1().kind == VifCode::Kind::PC_PORT) {
    // we got a PC PORT packet. this contains some extra data to set up the model
    flush_pending_model(render_state, prof);
    init_pc_model(init, render_state);
    ASSERT(tag_is_nothing_cnt(dma));
    init = dma.read_and_advance();  // dummy tag in pc port
    init = dma.read_and_advance();
  }

  // row stuff.
  ASSERT(init.vifcode0().kind == VifCode::Kind::STROW);
  ASSERT(init.size_bytes == 16);
  // m_vif.row[0] = init.vif1();
  // memcpy(m_vif.row + 1, init.data, 12);
  u32 extra;
  memcpy(&extra, init.data + 12, 4);
  // ASSERT(extra == 0);
  m_current_effect_enable_bits = extra;
  m_current_ignore_alpha_bits = extra >> 16;
  DmaTransfer next;

  bool setting_up = true;
  u32 mscal_addr = -1;
  while (setting_up) {
    next = dma.read_and_advance();
    // fmt::print("next: {}", dma.current_tag().print());
    u32 offset_in_data = 0;
    //    fmt::print("START {} : {} {}\n", next.size_bytes, next.vifcode0().print(),
    //               next.vifcode1().print());
    auto vif0 = next.vifcode0();
    switch (vif0.kind) {
      case VifCode::Kind::NOP:
      case VifCode::Kind::FLUSHE:
        break;
      case VifCode::Kind::STMOD:
        ASSERT(vif0.immediate == 0 || vif0.immediate == 1);
        // m_vif.stmod = vif0.immediate;
        break;
      default:
        ASSERT(false);
    }

    auto vif1 = next.vifcode1();
    switch (vif1.kind) {
      case VifCode::Kind::UNPACK_V4_8: {
        VifCodeUnpack up(vif1);
        offset_in_data += 4 * vif1.num;
      } break;
      case VifCode::Kind::UNPACK_V4_32: {
        VifCodeUnpack up(vif1);
        if (up.addr_qw == 132 && vif1.num == 8) {
          set_lights(next);
        } else if (vif1.num == 7) {
          handle_matrix_dma(next);
        }
        offset_in_data += 16 * vif1.num;
      } break;
      case VifCode::Kind::MSCAL:
        // fmt::print("cal\n");
        mscal_addr = vif1.immediate;
        ASSERT(next.size_bytes == 0);
        setting_up = false;
        break;
      default:
        ASSERT(false);
    }

    ASSERT(offset_in_data <= next.size_bytes);
    if (offset_in_data < next.size_bytes) {
      ASSERT((offset_in_data % 4) == 0);
      u32 leftover = next.size_bytes - offset_in_data;
      if (leftover < 16) {
        for (u32 i = 0; i < leftover; i++) {
          ASSERT(next.data[offset_in_data + i] == 0);
        }
      } else {
        ASSERT(false);
      }
    }
  }
}

/*!
 * Queue up some bones to be included in the bone buffer.
 * Returns the index of the first bone vector.
 */
u32 BaseMerc2::alloc_bones(int count) {
  u32 first_bone_vector = m_next_free_bone_vector;
  ASSERT(count * 8 + first_bone_vector <= MAX_SHADER_BONE_VECTORS);

  // model should have under 128 bones.
  ASSERT(count <= MAX_SKEL_BONES);

  // iterate over each bone we need
  for (int i = 0; i < count; i++) {
    auto& skel_mat = m_skel_matrix_buffer[i];
    auto* shader_mat = &m_shader_bone_vector_buffer[m_next_free_bone_vector];
    int bv = 0;

    // and copy to the large bone buffer.
    for (int j = 0; j < 4; j++) {
      shader_mat[bv++] = skel_mat.tmat[j];
    }

    for (int j = 0; j < 3; j++) {
      shader_mat[bv++] = skel_mat.nmat[j];
    }

    // we could include the effect of the perspective matrix here.
    //        for (int j = 0; j < 3; j++) {
    //          tbone_buffer[i][j] = vf15.elementwise_multiply(bone_mat[j]);
    //          tbone_buffer[i][j].w() += p.w() * bone_mat[j].z();
    //          tbone_buffer[i][j] *= scale;
    //        }
    //
    //        tbone_buffer[i][3] = vf15.elementwise_multiply(bone_mat[3]) +
    //        m_low_memory.perspective[3]; tbone_buffer[i][3].w() += p.w() * bone_mat[3].z();

    m_next_free_bone_vector += 8;
  }

  auto b0 = m_next_free_bone_vector;
  m_next_free_bone_vector += m_opengl_buffer_alignment - 1;
  m_next_free_bone_vector /= m_opengl_buffer_alignment;
  m_next_free_bone_vector *= m_opengl_buffer_alignment;
  ASSERT(b0 <= m_next_free_bone_vector);
  ASSERT(first_bone_vector + count * 8 <= m_next_free_bone_vector);
  return first_bone_vector;
}
/*!
 * Flush a model to draw buckets
 */
void BaseMerc2::flush_pending_model(SharedRenderState* render_state, ScopedProfilerNode& prof) {
  if (!m_current_model) {
    return;
  }

  const LevelData* lev = m_current_model->level;
  const tfrag3::MercModel* model = m_current_model->model;

  int bone_count = model->max_bones + 1;

  if (m_next_free_light >= MAX_LIGHTS) {
    fmt::print("BaseMerc2 out of lights, consider increasing MAX_LIGHTS\n");
    flush_draw_buckets(render_state, prof);
  }

  if (m_next_free_bone_vector + m_opengl_buffer_alignment + bone_count * 8 >
      MAX_SHADER_BONE_VECTORS) {
    fmt::print("BaseMerc2 out of bones, consider increasing MAX_SHADER_BONE_VECTORS\n");
    flush_draw_buckets(render_state, prof);
  }

  // find a level bucket
  LevelDrawBucket* lev_bucket = nullptr;
  for (u32 i = 0; i < m_next_free_level_bucket; i++) {
    if (m_level_draw_buckets[i].level == lev) {
      lev_bucket = &m_level_draw_buckets[i];
      break;
    }
  }

  if (!lev_bucket) {
    // no existing bucket
    if (m_next_free_level_bucket >= m_level_draw_buckets.size()) {
      // out of room, flush
      // fmt::print("BaseMerc2 out of levels, consider increasing MAX_LEVELS\n");
      flush_draw_buckets(render_state, prof);
      // and retry the whole thing.
      flush_pending_model(render_state, prof);
      return;
    }
    // alloc a new one
    lev_bucket = &m_level_draw_buckets[m_next_free_level_bucket++];
    lev_bucket->reset();
    lev_bucket->level = lev;
  }

  if (lev_bucket->next_free_draw + model->max_draws >= lev_bucket->draws.size()) {
    // out of room, flush
    fmt::print("BaseMerc2 out of draws, consider increasing MAX_DRAWS_PER_LEVEL\n");
    flush_draw_buckets(render_state, prof);
    // and retry the whole thing.
    flush_pending_model(render_state, prof);
    return;
  }

  u32 first_bone = alloc_bones(bone_count);

  // allocate lights
  u32 lights = alloc_lights(m_current_lights);
  //
  for (size_t ei = 0; ei < model->effects.size(); ei++) {
    if (!(m_current_effect_enable_bits & (1 << ei))) {
      continue;
    }

    u8 ignore_alpha = (m_current_ignore_alpha_bits & (1 << ei));
    auto& effect = model->effects[ei];
    for (auto& mdraw : effect.draws) {
      Draw* draw = &lev_bucket->draws[lev_bucket->next_free_draw++];
      draw->first_index = mdraw.first_index;
      draw->index_count = mdraw.index_count;
      draw->mode = mdraw.mode;
      draw->texture = mdraw.tree_tex_id;
      draw->first_bone = first_bone;
      draw->light_idx = lights;
      draw->num_triangles = mdraw.num_triangles;
      draw->ignore_alpha = ignore_alpha;
    }
  }

  m_current_model = std::nullopt;
}
