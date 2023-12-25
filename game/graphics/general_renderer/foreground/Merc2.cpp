#include "Merc2.h"

#include "game/graphics/general_renderer/background/background_common.h"

#include "third-party/imgui/imgui.h"

BaseMerc2::BaseMerc2() {
  m_mod_vtx_temp.resize(MAX_MOD_VTX);
  m_mod_vtx_unpack_temp.resize(MAX_MOD_VTX * 2);
}

void BaseMerc2::draw_debug_window(BaseMercDebugStats* debug_status) {
  ImGui::Text("Models   : %d", m_stats.num_models);
  ImGui::Text("Effects  : %d", m_stats.num_effects);
  ImGui::Text("Draws (p): %d", m_stats.num_predicted_draws);
  ImGui::Text("Tris  (p): %d", m_stats.num_predicted_tris);
  ImGui::Text("Bones    : %d", m_stats.num_bones_uploaded);
  ImGui::Text("Lights   : %d", m_stats.num_lights);
  ImGui::Text("Dflush   : %d", m_stats.num_draw_flush);
  if (m_debug_mode) {
    for (int i = 0; i < kMaxEffect; i++) {
      ImGui::Checkbox(fmt::format("e{:02d}", i).c_str(), &m_effect_debug_mask[i]);
    }

    for (const auto& model : debug_status->model_list) {
      if (ImGui::TreeNode(model.name.c_str())) {
        ImGui::Text("Level: %s\n", model.level.c_str());
        for (const auto& e : model.effects) {
          for (const auto& d : e.draws) {
            ImGui::Text("%s", d.mode.to_string().c_str());
          }
          ImGui::Separator();
        }
        ImGui::TreePop();
      }
    }
  }
}

/*!
 * Main BaseMerc2 rendering.
 */
void BaseMerc2::render(DmaFollower& dma,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof,
                       BaseMercDebugStats* debug_stats) {
  *debug_stats = {};
  if (debug_stats->collect_debug_model_list) {
    debug_stats->model_list.clear();
  }

  {
    auto pp = profiler::scoped_prof("handle-all-dma");
    // iterate through the dma chain, filling buckets
    handle_all_dma(dma, render_state, prof, debug_stats);
  }

  {
    auto pp = profiler::scoped_prof("flush-buckets");
    // flush buckets to draws
    flush_draw_buckets(render_state, prof, debug_stats);
  }
}

u32 BaseMerc2::alloc_lights(const VuLights& lights) {
  ASSERT(m_next_free_light < MAX_LIGHTS);
  m_stats.num_lights++;
  u32 light_idx = m_next_free_light;
  m_lights_buffer[m_next_free_light++] = lights;
  static_assert(sizeof(VuLights) == 7 * 16);
  return light_idx;
}

std::string BaseMerc2::ShaderMercMat::to_string() const {
  return fmt::format("tmat:\n{}\n{}\n{}\n{}\n", tmat[0].to_string_aligned(),
                     tmat[1].to_string_aligned(), tmat[2].to_string_aligned(),
                     tmat[3].to_string_aligned());
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
                               BaseSharedRenderState* render_state,
                               ScopedProfilerNode& prof,
                               BaseMercDebugStats* debug_stats) {
  // process the first tag. this is just jumping to the merc-specific dma.
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0 || data0.vifcode1().kind == VifCode::Kind::NOP);
  ASSERT(data0.vif0() == 0 || data0.vifcode0().kind == VifCode::Kind::NOP ||
         data0.vifcode0().kind == VifCode::Kind::MARK);
  ASSERT(data0.size_bytes == 0);
  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }

  if (dma.current_tag_offset() == render_state->next_bucket) {
    return;
  }
  // if we reach here, there's stuff to draw
  // this handles merc-specific setup DMA
  handle_setup_dma(dma, render_state);

  // handle each merc transfer
  while (dma.current_tag_offset() != render_state->next_bucket) {
    handle_merc_chain(dma, render_state, prof, debug_stats);
  }
  ASSERT(dma.current_tag_offset() == render_state->next_bucket);
}

void BaseMerc2::handle_setup_dma(DmaFollower& dma, BaseSharedRenderState* render_state) {
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
  set_merc_uniform_buffer_data(first);

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

  if (render_state->GetVersion() == GameVersion::Jak1) {
    auto second = dma.read_and_advance();
    ASSERT(second.size_bytes == 32);  // setting up test register.
    auto nothing = dma.read_and_advance();
    ASSERT(nothing.size_bytes == 0);
    ASSERT(nothing.vif0() == 0);
    ASSERT(nothing.vif1() == 0);
  } else {
    auto second = dma.read_and_advance();
    ASSERT(second.size_bytes == 48);  // setting up test/zbuf register.
    // todo z write mask stuff.
    auto nothing = dma.read_and_advance();
    ASSERT(nothing.size_bytes == 0);
    ASSERT(nothing.vif0() == 0);
    ASSERT(nothing.vif1() == 0);
  }
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
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof,
                                  BaseMercDebugStats* debug_stats) {
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
  int skip_count = 2;
  if (render_state->GetVersion() == GameVersion::Jak2) {
    skip_count = 1;
  }

  while (init.vifcode1().kind == VifCode::Kind::PC_PORT) {
    // flush_pending_model(render_state, prof);
    handle_pc_model(init, render_state, prof, debug_stats);
    for (int i = 0; i < skip_count; i++) {
      auto link = dma.read_and_advance();
      ASSERT(link.vifcode0().kind == VifCode::Kind::NOP);
      ASSERT(link.vifcode1().kind == VifCode::Kind::NOP);
      ASSERT(link.size_bytes == 0);
    }
    init = dma.read_and_advance();
  }

  if (init.vifcode0().kind == VifCode::Kind::FLUSHA) {
    int num_skipped = 0;
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
      num_skipped++;
    }
    ASSERT(num_skipped < 4);
    return;
  }
}

/*!
 * Queue up some bones to be included in the bone buffer.
 * Returns the index of the first bone vector.
 */
u32 BaseMerc2::alloc_bones(int count, ShaderMercMat* data) {
  u32 first_bone_vector = m_next_free_bone_vector;
  ASSERT(count * 8 + first_bone_vector <= MAX_SHADER_BONE_VECTORS);

  // model should have under 128 bones.
  ASSERT(count <= MAX_SKEL_BONES);

  // iterate over each bone we need
  for (int i = 0; i < count; i++) {
    auto& skel_mat = data[i];
    auto* shader_mat = &m_shader_bone_vector_buffer[m_next_free_bone_vector];
    int bv = 0;

    // and copy to the large bone buffer.
    for (int j = 0; j < 4; j++) {
      shader_mat[bv++] = skel_mat.tmat[j];
    }

    for (int j = 0; j < 3; j++) {
      shader_mat[bv++] = skel_mat.nmat[j];
    }

    m_next_free_bone_vector += 8;
  }

  auto b0 = m_next_free_bone_vector;
  m_next_free_bone_vector += m_graphics_buffer_alignment - 1;
  m_next_free_bone_vector /= m_graphics_buffer_alignment;
  m_next_free_bone_vector *= m_graphics_buffer_alignment;
  ASSERT(b0 <= m_next_free_bone_vector);
  ASSERT(first_bone_vector + count * 8 <= m_next_free_bone_vector);
  return first_bone_vector;
}

void BaseMerc2::handle_mod_vertices(const DmaTransfer& setup,
                                    const tfrag3::MercEffect& effect,
                                    const u8* input_data,
                                    uint32_t index,
                                    const tfrag3::MercModel* model) {
  // start with the "correct" vertices from the model data:
  memcpy(m_mod_vtx_temp.data(), effect.mod.vertices.data(),
         sizeof(tfrag3::MercVertex) * effect.mod.vertices.size());

  // get pointers to the fragment and fragment control data
  u32 goal_addr;
  memcpy(&goal_addr, input_data + 4 * index, 4);
  const u8* ee0 = setup.data - setup.data_offset;
  const u8* merc_effect = ee0 + goal_addr;
  u16 frag_cnt;
  memcpy(&frag_cnt, merc_effect + 18, 2);
  ASSERT(frag_cnt >= effect.mod.fragment_mask.size());
  u32 frag_goal;
  memcpy(&frag_goal, merc_effect, 4);
  u32 frag_ctrl_goal;
  memcpy(&frag_ctrl_goal, merc_effect + 4, 4);
  const u8* frag = ee0 + frag_goal;
  const u8* frag_ctrl = ee0 + frag_ctrl_goal;

  // loop over frags
  u32 vidx = 0;
  // u32 st_vif_add = model->st_vif_add;
  float xyz_scale = model->xyz_scale;
  profiler::prof().end_event();
  {
    // we're going to look at data that the game may be modifying.
    // in the original game, they didn't have any lock, but I think that the
    // scratchpad access from the EE would effectively block the VIF1 DMA, so you'd
    // hopefully never get a partially updated model (which causes obvious holes).
    // this lock is not ideal, and can block the rendering thread while blerc_execute runs,
    // which can take up to 2ms on really blerc-heavy scenes
    std::unique_lock<std::mutex> lk(g_merc_data_mutex);
    int frags_done = 0;
    auto p = profiler::scoped_prof("vert-math");

    // loop over fragments
    for (u32 fi = 0; fi < effect.mod.fragment_mask.size(); fi++) {
      frags_done++;
      u8 mat_xfer_count = frag_ctrl[3];

      // we create a mask of fragments to skip because they have no vertices.
      // the indexing data assumes that we skip the other fragments.
      if (effect.mod.fragment_mask[fi]) {
        // read fragment metadata
        u8 unsigned_four_count = frag_ctrl[0];
        u8 lump_four_count = frag_ctrl[1];
        u32 mm_qwc_off = frag[10];
        float float_offsets[3];
        memcpy(float_offsets, &frag[mm_qwc_off * 16], 12);
        u32 my_u4_count = ((unsigned_four_count + 3) / 4) * 16;
        u32 my_l4_count = my_u4_count + ((lump_four_count + 3) / 4) * 16;

        // loop over vertices in the fragment and unpack
        for (u32 w = my_u4_count / 4; w < (my_l4_count / 4) - 2; w += 3) {
          // positions
          u32 q0w = 0x4b010000 + frag[w * 4 + (0 * 4) + 3];
          u32 q1w = 0x4b010000 + frag[w * 4 + (1 * 4) + 3];
          u32 q2w = 0x4b010000 + frag[w * 4 + (2 * 4) + 3];

          // normals
          u32 q0z = 0x47800000 + frag[w * 4 + (0 * 4) + 2];
          u32 q1z = 0x47800000 + frag[w * 4 + (1 * 4) + 2];
          u32 q2z = 0x47800000 + frag[w * 4 + (2 * 4) + 2];

          // uvs
          u32 q2x = model->st_vif_add + frag[w * 4 + (2 * 4) + 0];
          u32 q2y = model->st_vif_add + frag[w * 4 + (2 * 4) + 1];

          auto* pos_array = m_mod_vtx_unpack_temp[vidx].pos;
          memcpy(&pos_array[0], &q0w, 4);
          memcpy(&pos_array[1], &q1w, 4);
          memcpy(&pos_array[2], &q2w, 4);
          pos_array[0] += float_offsets[0];
          pos_array[1] += float_offsets[1];
          pos_array[2] += float_offsets[2];
          pos_array[0] *= xyz_scale;
          pos_array[1] *= xyz_scale;
          pos_array[2] *= xyz_scale;

          auto* nrm_array = m_mod_vtx_unpack_temp[vidx].nrm;
          memcpy(&nrm_array[0], &q0z, 4);
          memcpy(&nrm_array[1], &q1z, 4);
          memcpy(&nrm_array[2], &q2z, 4);
          nrm_array[0] += -65537;
          nrm_array[1] += -65537;
          nrm_array[2] += -65537;

          auto* uv_array = m_mod_vtx_unpack_temp[vidx].uv;
          memcpy(&uv_array[0], &q2x, 4);
          memcpy(&uv_array[1], &q2y, 4);
          uv_array[0] += model->st_magic;
          uv_array[1] += model->st_magic;

          vidx++;
        }
      }

      // next control
      frag_ctrl += 4 + 2 * mat_xfer_count;

      // next frag
      u32 mm_qwc_count = frag[11];
      frag += mm_qwc_count * 16;
    }

    // sanity check
    if (effect.mod.expect_vidx_end != vidx) {
      fmt::print("---------- BAD {}/{}\n", effect.mod.expect_vidx_end, vidx);
      ASSERT(false);
    }
  }

  {
    auto pp = profiler::scoped_prof("copy");
    // now copy the data in merc original vertex order to the output.
    for (u32 vi = 0; vi < effect.mod.vertices.size(); vi++) {
      u32 addr = effect.mod.vertex_lump4_addr[vi];
      if (addr < vidx) {
        memcpy(&m_mod_vtx_temp[vi], &m_mod_vtx_unpack_temp[addr], 32);
        m_mod_vtx_temp[vi].st[0] = m_mod_vtx_unpack_temp[addr].uv[0];
        m_mod_vtx_temp[vi].st[1] = m_mod_vtx_unpack_temp[addr].uv[1];
      }
    }
  }
}

void BaseMerc2::populate_envmap_draw(const tfrag3::MercDraw& mdraw,
                                     DrawMode envmap_mode,
                                     u32 envmap_texture,
                                     const ModSettings& settings,
                                     const u8* fade,
                                     BaseMerc2::Draw* draw) {
  draw->flags = 0;
  draw->first_index = mdraw.first_index;
  draw->index_count = mdraw.index_count;
  draw->mode = envmap_mode;
  if (settings.uses_jak1_water) {
    draw->mode.set_ab(true);
    draw->mode.disable_depth_write();
  }
  draw->texture = envmap_texture;
  draw->first_bone = settings.first_bone;
  draw->light_idx = settings.lights;
  draw->num_triangles = mdraw.num_triangles;
  for (int i = 0; i < 4; i++) {
    draw->fade[i] = fade[i];
  }
}

void BaseMerc2::populate_normal_draw(const tfrag3::MercDraw& mdraw,
                                     const ModSettings& settings,
                                     BaseMerc2::Draw* draw) {
  draw->flags = 0;
  draw->first_index = mdraw.first_index;
  draw->index_count = mdraw.index_count;
  draw->mode = mdraw.mode;
  if (settings.uses_jak1_water) {
    draw->mode.set_ab(true);
    draw->mode.disable_depth_write();
  }

  if (settings.model_disables_fog) {
    draw->mode.set_fog(false);
    // but don't toggle it the other way?
  }

  draw->texture = mdraw.eye_id == 0xff ? mdraw.tree_tex_id : (0xffffff00 | mdraw.eye_id);
  draw->first_bone = settings.first_bone;
  draw->light_idx = settings.lights;
  draw->num_triangles = mdraw.num_triangles;
  if (settings.ignore_alpha) {
    draw->flags |= IGNORE_ALPHA;
  }
  for (int i = 0; i < 4; i++) {
    draw->fade[i] = 0;
  }
}

/*!
 * Modify vertices for blerc.
 */
void BaseMerc2::blerc_avx(const u32* i_data,
               const u32* i_data_end,
               const tfrag3::BlercFloatData* floats,
               const float* weights,
               tfrag3::MercVertex* out,
               float multiplier) {
  // store a table of weights. It's faster to load the 16-bytes of weights than load and broadcast
  // the float.
  __m128 weights_table[kMaxBlerc] = {};
  for (int i = 0; i < kMaxBlerc; i++) {
    weights_table[i] = _mm_set1_ps(weights[i] * multiplier);
  }

  // loop over vertices
  while (i_data != i_data_end) {
    // load the base position
    __m128 pos = _mm_load_ps(floats->v);
    __m128 nrm = _mm_load_ps(floats->v + 4);
    floats++;

    // loop over targets
    while (*i_data != tfrag3::Blerc::kTargetIdxTerminator) {
      // get the weights for this target, from the game data.
      __m128 weight_multiplier = weights_table[*i_data];
      // get the pos/normal offset for this target.
      __m128 posm = _mm_load_ps(floats->v);
      __m128 nrmm = _mm_load_ps(floats->v + 4);
      floats++;

      // apply weights and add
      posm = _mm_mul_ps(posm, weight_multiplier);
      nrmm = _mm_mul_ps(nrmm, weight_multiplier);
      pos = _mm_add_ps(pos, posm);
      nrm = _mm_add_ps(nrm, nrmm);

      i_data++;
    }
    i_data++;

    // store final position/normal.
    _mm_store_ps(out[*i_data].pos, pos);
    _mm_store_ps(out[*i_data].normal, nrm);
    i_data++;
  }
}

void BaseMerc2::validate_merc_vertices(const tfrag3::MercEffect& effect) {
  // check that we have enough room for the finished thing.
  if (effect.mod.vertices.size() > MAX_MOD_VTX) {
    fmt::print("More mod vertices than MAX_MOD_VTX. {} > {}\n", effect.mod.vertices.size(),
               MAX_MOD_VTX);
    ASSERT_NOT_REACHED();
  }

  // check that we have enough room for unpack
  if (effect.mod.expect_vidx_end > MAX_MOD_VTX) {
    fmt::print("More mod vertices (temp) than MAX_MOD_VTX. {} > {}\n", effect.mod.expect_vidx_end,
               MAX_MOD_VTX);
    ASSERT_NOT_REACHED();
  }
}

void BaseMerc2::setup_mod_vertex_dma(const tfrag3::MercEffect& effect,
                                     const u8* input_data,
                                     const u32 index,
                                     const tfrag3::MercModel* model,
                                     const DmaTransfer& setup) {
  // start with the "correct" vertices from the model data:
  memcpy(m_mod_vtx_temp.data(), effect.mod.vertices.data(),
         sizeof(tfrag3::MercVertex) * effect.mod.vertices.size());

  // get pointers to the fragment and fragment control data
  u32 goal_addr;
  memcpy(&goal_addr, input_data + 4 * index, 4);
  const u8* ee0 = setup.data - setup.data_offset;
  const u8* merc_effect = ee0 + goal_addr;
  u16 frag_cnt;
  memcpy(&frag_cnt, merc_effect + 18, 2);
  ASSERT(frag_cnt >= effect.mod.fragment_mask.size());
  u32 frag_goal;
  memcpy(&frag_goal, merc_effect, 4);
  u32 frag_ctrl_goal;
  memcpy(&frag_ctrl_goal, merc_effect + 4, 4);
  const u8* frag = ee0 + frag_goal;
  const u8* frag_ctrl = ee0 + frag_ctrl_goal;

  // loop over frags
  u32 vidx = 0;
  // u32 st_vif_add = model->st_vif_add;
  float xyz_scale = model->xyz_scale;
  profiler::prof().end_event();
  {
    // we're going to look at data that the game may be modifying.
    // in the original game, they didn't have any lock, but I think that the
    // scratchpad access from the EE would effectively block the VIF1 DMA, so you'd
    // hopefully never get a partially updated model (which causes obvious holes).
    // this lock is not ideal, and can block the rendering thread while blerc_execute runs,
    // which can take up to 2ms on really blerc-heavy scenes
    std::unique_lock<std::mutex> lk(g_merc_data_mutex);
    [[maybe_unused]] int frags_done = 0;
    auto p = profiler::scoped_prof("vert-math");

    // loop over fragments
    for (u32 fi = 0; fi < effect.mod.fragment_mask.size(); fi++) {
      frags_done++;
      u8 mat_xfer_count = frag_ctrl[3];

      // we create a mask of fragments to skip because they have no vertices.
      // the indexing data assumes that we skip the other fragments.
      if (effect.mod.fragment_mask[fi]) {
        // read fragment metadata
        u8 unsigned_four_count = frag_ctrl[0];
        u8 lump_four_count = frag_ctrl[1];
        u32 mm_qwc_off = frag[10];
        float float_offsets[3];
        memcpy(float_offsets, &frag[mm_qwc_off * 16], 12);
        u32 my_u4_count = ((unsigned_four_count + 3) / 4) * 16;
        u32 my_l4_count = my_u4_count + ((lump_four_count + 3) / 4) * 16;

        // loop over vertices in the fragment and unpack
        for (u32 w = my_u4_count / 4; w < (my_l4_count / 4) - 2; w += 3) {
          // positions
          u32 q0w = 0x4b010000 + frag[w * 4 + (0 * 4) + 3];
          u32 q1w = 0x4b010000 + frag[w * 4 + (1 * 4) + 3];
          u32 q2w = 0x4b010000 + frag[w * 4 + (2 * 4) + 3];

          // normals
          u32 q0z = 0x47800000 + frag[w * 4 + (0 * 4) + 2];
          u32 q1z = 0x47800000 + frag[w * 4 + (1 * 4) + 2];
          u32 q2z = 0x47800000 + frag[w * 4 + (2 * 4) + 2];

          // uvs
          u32 q2x = model->st_vif_add + frag[w * 4 + (2 * 4) + 0];
          u32 q2y = model->st_vif_add + frag[w * 4 + (2 * 4) + 1];

          auto* pos_array = m_mod_vtx_unpack_temp[vidx].pos;
          memcpy(&pos_array[0], &q0w, 4);
          memcpy(&pos_array[1], &q1w, 4);
          memcpy(&pos_array[2], &q2w, 4);
          pos_array[0] += float_offsets[0];
          pos_array[1] += float_offsets[1];
          pos_array[2] += float_offsets[2];
          pos_array[0] *= xyz_scale;
          pos_array[1] *= xyz_scale;
          pos_array[2] *= xyz_scale;

          auto* nrm_array = m_mod_vtx_unpack_temp[vidx].nrm;
          memcpy(&nrm_array[0], &q0z, 4);
          memcpy(&nrm_array[1], &q1z, 4);
          memcpy(&nrm_array[2], &q2z, 4);
          nrm_array[0] += -65537;
          nrm_array[1] += -65537;
          nrm_array[2] += -65537;

          auto* uv_array = m_mod_vtx_unpack_temp[vidx].uv;
          memcpy(&uv_array[0], &q2x, 4);
          memcpy(&uv_array[1], &q2y, 4);
          uv_array[0] += model->st_magic;
          uv_array[1] += model->st_magic;

          vidx++;
        }
      }

      // next control
      frag_ctrl += 4 + 2 * mat_xfer_count;

      // next frag
      u32 mm_qwc_count = frag[11];
      frag += mm_qwc_count * 16;
    }

    // sanity check
    if (effect.mod.expect_vidx_end != vidx) {
      fmt::print("---------- BAD {}/{}\n", effect.mod.expect_vidx_end, vidx);
      ASSERT(false);
    }
  }

  {
    auto pp = profiler::scoped_prof("copy");
    // now copy the data in merc original vertex order to the output.
    for (u32 vi = 0; vi < effect.mod.vertices.size(); vi++) {
      u32 addr = effect.mod.vertex_lump4_addr[vi];
      if (addr < vidx) {
        memcpy(&m_mod_vtx_temp[vi], &m_mod_vtx_unpack_temp[addr], 32);
        m_mod_vtx_temp[vi].st[0] = m_mod_vtx_unpack_temp[addr].uv[0];
        m_mod_vtx_temp[vi].st[1] = m_mod_vtx_unpack_temp[addr].uv[1];
      }
    }
  }
}
