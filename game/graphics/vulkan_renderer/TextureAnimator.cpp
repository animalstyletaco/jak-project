#include "TextureAnimator.h"

#include "common/global_profiler/GlobalProfiler.h"
#include "common/texture/texture_slots.h"
#include "common/util/FileUtil.h"
#include "common/util/Timer.h"

#include "game/graphics/texture/TexturePoolOpenGL.h"

ClutBlenderVulkan::ClutBlenderVulkan(const std::string& dest,
                                     const std::vector<std::string>& sources,
                                     const std::optional<std::string>& level_name,
                                     const tfrag3::Level* level,
                                     std::unique_ptr<GraphicsDeviceVulkan>& device)
    : BaseClutBlender(dest, sources, level_name, level), m_texture(device) {
  m_dest = itex_by_name(level, dest, level_name);
  m_texture = tpool->allocate(m_dest->w, m_dest->h);
  m_temp_rgba.resize(m_dest->w * m_dest->h);
}

void ClutBlenderVulkan::run(const float* weights) {
  bool needs_run = false;

  for (size_t i = 0; i < m_current_weights.size(); i++) {
    if (weights[i] != m_current_weights[i]) {
      needs_run = true;
      break;
    }
  }

  if (!needs_run) {
    return;
  }

  for (size_t i = 0; i < m_current_weights.size(); i++) {
    m_current_weights[i] = weights[i];
  }

  for (int i = 0; i < 256; i++) {
    math::Vector4f v = math::Vector4f::zero();
    for (size_t j = 0; j < m_current_weights.size(); j++) {
      v += (*m_cluts[j])[i].cast<float>() * m_current_weights[j];
    }
    m_temp_clut[i] = v.cast<u8>();
  }

  for (int i = 0; i < m_temp_rgba.size(); i++) {
    memcpy(&m_temp_rgba[i], m_temp_clut[m_dest->index_data[i]].data(), 4);
  }

  VkExtent3D extents{m_dest->w, m_dest->h, 1};
  //TODO: figure out what to do with possible mipmap
  m_texture.createImage(extents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                        VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT);
  m_texture.writeToImage(m_temp_rgba.data());
}

VulkanTextureAnimator::VulkanTextureAnimator(const tfrag3::Level* common_level, std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info)
    : BaseTextureAnimator(common_level), m_device(device), m_vulkan_info(vulkan_info), m_dummy_texture(m_device) {
  // The TextureAnimator does a lot of "draws" which are just a single quad, so we create a 4-vertex
  // buffer. It turns out that just storing the vertex index in the vertex, then indexing into a
  // uniform buffer is faster to update. (though this may be driver specific?)
  std::array<Vertex, 4> vertices = {Vertex{0, 0, 0, 0}, Vertex{1, 0, 0, 0}, Vertex{2, 0, 0, 0},
                                    Vertex{3, 0, 0, 0}};
  m_vertex_buffer = std::make_unique<VertexBuffer>(device, sizeof(Vertex), 4, 1);
  m_vertex_buffer->writeToGpuBuffer(vertices.data());

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);
  InitializeInputVertexAttribute();
  create_pipeline_layout();
  init_shaders();

  // create a single "dummy texture" with all 0 data.
  // this is faster and easier than switching shaders to one without texturing, and is used
  // only rarely
  std::vector<u8> data(16 * 16 * 4);
  VkExtent3D extents{16, 16, 1};
  m_dummy_texture.createImage(extents, 1, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                              VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT);
  m_dummy_texture.writeToImage(data.data());
}

int VulkanTextureAnimator::create_clut_blender_group(const std::vector<std::string>& textures,
                                               const std::string& suffix0,
                                               const std::string& suffix1,
                                               const std::optional<std::string>& dgo) {
  int ret = m_clut_blender_groups.size();
  m_clut_blender_groups.emplace_back();
  add_to_clut_blender_group(ret, textures, suffix0, suffix1, dgo);
  return ret;
}

void VulkanTextureAnimator::add_to_clut_blender_group(int idx,
                                                const std::vector<std::string>& textures,
                                                const std::string& suffix0,
                                                const std::string& suffix1,
                                                const std::optional<std::string>& dgo) {
  auto& grp = m_clut_blender_groups.at(idx);
  for (auto& prefix : textures) {
    grp.blenders.emplace_back(prefix, std::vector<std::string>{prefix + suffix0, prefix + suffix1},
                              dgo, m_common_level, &m_opengl_texture_pool);
    grp.outputs.push_back(output_slot_by_idx(GameVersion::Jak2, prefix));
    m_output_slots.at(grp.outputs.back()) = grp.blenders.back().texture();
  }
}

VulkanTextureAnimator::~VulkanTextureAnimator() {
}

VulkanTexture& VulkanTextureAnimator::get_by_slot(int idx) {
  ASSERT(idx >= 0 && idx < (int)m_output_slots.size());
  return m_output_slots[idx];
}

/*!
 * Main function to run texture animations from DMA. Updates textures in the pool.
 */
void VulkanTextureAnimator::handle_texture_anim_data(DmaFollower& dma,
                                               const u8* ee_mem) {
  glDepthMask(GL_FALSE);
  m_pipeline_config_info.DepthTestEnable = false;
  for (auto& t : m_in_use_temp_textures) {
    //m_opengl_texture_pool.free(t.tex, t.w, t.h);
  }
  m_in_use_temp_textures.clear();  // reset temp texture allocator.
  m_erased_on_this_frame.clear();

  //Call to base texture animator here

  // The steps above will populate m_textures with some combination of GPU/CPU textures.
  // we need to make sure that all final textures end up on the GPU. For now, we detect this by
  // seeing if the "erase" operation ran on an tbp, indicating that it was cleared, which is
  // always done to all textures by the GOAL code.
  for (auto tbp : m_erased_on_this_frame) {
    auto p = profiler::scoped_prof("handle-one-erased");
    force_to_gpu(tbp);
  }

  // Loop over textures and put them in the pool if needed
  for (auto& [tbp, entry] : m_textures) {
    if (entry.kind != BaseVramEntry::Kind::GPU) {
      // not on the GPU, we can't put it in the texture pool.
      // if it was skipped by the above step, this is just some temporary texture we don't need
      // (hopefully)
      // (TODO: could flag these somehow?)
      continue;
    }
    dprintf("end processing on %d\n", tbp);

    // in the ideal case, the texture processing code will just modify the OpenGL texture in-place.
    // however, if the size changes, or we need to add a new texture, we have additional work to
    // do.

    if (entry.needs_pool_update) {
      if (entry.pool_gpu_tex) {
        // we have a GPU texture in the pool, but we need to change the actual texture.
        auto p = profiler::scoped_prof("pool-update");
        ASSERT(entry.pool_gpu_tex);
        // change OpenGL texture in the pool
        texture_pool->update_gl_texture(entry.pool_gpu_tex, entry.tex_width, entry.tex_height,
                                        entry.tex.value().texture());
        // set as the active texture in this vram slot (other textures can be loaded for
        // different part of the frame that we need to replace). This is a fast operation.
        texture_pool->move_existing_to_vram(entry.pool_gpu_tex, tbp);
        entry.needs_pool_update = false;
        dprintf("update texture %d\n", tbp);
      } else {
        // this is the first time we use a texture in this slot, so we need to create it.
        // should happen only once per TBP.
        auto p = profiler::scoped_prof("pool-create");
        TextureInput in;
        in.gpu_texture = entry.tex.value().texture();
        in.w = entry.tex_width;
        in.h = entry.tex_height;
        in.debug_page_name = "PC-ANIM";
        in.debug_name = std::to_string(tbp);
        in.id = get_id_for_tbp(texture_pool, tbp);
        entry.pool_gpu_tex = texture_pool->give_texture_and_load_to_vram(in, tbp);
        entry.needs_pool_update = false;
        dprintf("create texture %d\n", tbp);
      }
    } else {
      // ideal case: OpenGL texture modified in place, just have to simulate "upload".
      auto p = profiler::scoped_prof("pool-move");
      texture_pool->move_existing_to_vram(entry.pool_gpu_tex, tbp);
      dprintf("no change %d\n", tbp);
    }
  }
  m_pipeline_config_info.DepthTestEnabled = true;
}

/*!
 * Make sure that this texture is a GPU texture. If not, convert it.
 * GPU textures don't support CLUT, so this should be done at the last possible point in time, as
 * CLUT effects can no longer be applied to the texture after this happens.
 */
void VulkanTextureAnimator::force_to_gpu(int tbp) {
  auto& entry = m_textures.at(tbp);
  switch (entry.kind) {
    default:
      printf("unhandled non-gpu conversion: %d (tbp = %d)\n", (int)entry.kind, tbp);
      ASSERT_NOT_REACHED();
    case VramEntry::Kind::CLUT16_16_IN_PSM32:
      // HACK: never convert known CLUT textures to GPU.
      // The main loop will incorrectly flag CLUT textures as final ones because we can't tell
      // the difference. So hopefully this is just an optimization. But we'll have to revisit if
      // they use texture data as both texture/clut.
      dprintf("suspicious clut...\n");
      break;
    case VramEntry::Kind::GPU:
      break;  // already on the gpu.
    case VramEntry::Kind::GENERIC_PSMT8: {
      // we have data that was uploaded in PSMT8 format. Assume that it will also be read in this
      // format. Convert to normal format.
      int tw = entry.tex_width;
      int th = entry.tex_height;
      std::vector<u32> rgba_data(tw * th);

      {
        auto p = profiler::scoped_prof("convert");
        // the CLUT is usually uploaded in PSM32 format, as a 16x16.
        const u32* clut = get_clut_16_16_psm32(entry.cbp);
        for (int r = 0; r < th; r++) {
          for (int c = 0; c < tw; c++) {
            rgba_data[c + r * tw] = clut[m_index_to_clut_addr[entry.data[c + r * tw]]];
          }
        }
      }

      // do OpenGL tricks to make sure this entry is set up to hold a texture with the size.
      // will also set flags for updating the pool
      setup_vram_entry_for_gpu_texture(tw, th, tbp);
      // load the texture.
      glBindTexture(GL_TEXTURE_2D, entry.tex.value().texture());
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                   rgba_data.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      entry.kind = VramEntry::Kind::GPU;
    } break;
  }
}

/*!
 * Get a pool texture ID for this texture. For now, there's just a unique ID per TBP.
 * The only purpose is to avoid putting all the textures with the same ID, which is a slow-path
 * in the pool (which is optimized for only a few textures with the same ID at most).
 */
PcTextureId VulkanTextureAnimator::get_id_for_tbp(TexturePool* pool, u32 tbp) {
  const auto& it = m_ids_by_vram.find(tbp);
  if (it == m_ids_by_vram.end()) {
    auto ret = pool->allocate_pc_port_texture(GameVersion::Jak2);
    m_ids_by_vram[tbp] = ret;
    return ret;
  } else {
    return it->second;
  }
}

/*!
 * Copy rg channels to ba from src to dst.
 * The PS2 implementation is confusing, and this is just a guess at how it works.
 */
void VulkanTextureAnimator::handle_rg_to_ba(const DmaTransfer& tf) {
  dprintf("[tex anim] rg -> ba\n");
  ASSERT(tf.size_bytes == sizeof(TextureAnimPcTransform));
  auto* data = (const TextureAnimPcTransform*)(tf.data);
  dprintf("  src: %d, dest: %d\n", data->src_tbp, data->dst_tbp);
  const auto& src = m_textures.find(data->src_tbp);
  const auto& dst = m_textures.find(data->dst_tbp);
  if (src == m_textures.end() && dst == m_textures.end()) {
    ASSERT_NOT_REACHED();
  }

  ASSERT(src->second.kind == VramEntry::Kind::GPU);
  ASSERT(dst->second.kind == VramEntry::Kind::GPU);
  ASSERT(src->second.tex.value().texture() != dst->second.tex.value().texture());
  FramebufferTexturePairContext ctxt(dst->second.tex.value());
  float uvs[2 * 4] = {0, 0, 1, 0, 1, 1, 0, 1};
  m_vertex_push_constant.positions = {{0, 0, 0, 1} {0, 0, 1, 1}, {0, 0, 1, 0}};
  m_vertex_push_constant.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  m_fragment_push_constant.enable_tex = 1;
  m_fragment_push_constant.rgba = {256, 256, 256, 128};  // TODO - seems wrong.
  m_fragment_push_constant.channel_scramble = {0, 1, 0, 1};
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_vertex_push_constant),
                     (void*)&m_vertex_push_constant);
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_vertex_push_constant),
                     sizeof(m_fragment_push_constant),
                     (void*)&m_fragment_push_constant);

  glBindTexture(GL_TEXTURE_2D, src->second.tex.value().texture());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  m_pipeline_config_info.BlendEnable = false;
  m_pipeline_config_info.DepthTestEnable = false;

  //TODO: Setup pipeline here
  vkCmdDraw(m_vulkan_info.render_command, 4, 1, 0, 0, 0);
}

void VulkanTextureAnimator::handle_set_clut_alpha(const DmaTransfer& tf) {
  ASSERT_NOT_REACHED();
  dprintf("[tex anim] set clut alpha\n");
  ASSERT(tf.size_bytes == sizeof(TextureAnimPcTransform));
  auto* data = (const TextureAnimPcTransform*)(tf.data);
  dprintf("  src: %d, dest: %d\n", data->src_tbp, data->dst_tbp);
  const auto& tex = m_textures.find(data->dst_tbp);
  ASSERT(tex != m_textures.end());

  ASSERT(tex->second.kind == VramEntry::Kind::GPU);
  FramebufferTexturePairContext ctxt(tex->second.tex.value());
  float positions[3 * 4] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  float uvs[2 * 4] = {0, 0, 1, 0, 1, 1, 0, 1};
  glUniform3fv(m_uniforms.positions, 4, positions);
  glUniform2fv(m_uniforms.uvs, 4, uvs);
  glUniform1i(m_uniforms.enable_tex, 0);  // NO TEXTURE!
  glUniform4f(m_uniforms.rgba, 128, 128, 128, 128);
  glUniform4i(m_uniforms.channel_scramble, 0, 1, 2, 3);
  glBindTexture(GL_TEXTURE_2D, m_dummy_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glColorMask(false, false, false, true);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glColorMask(true, true, true, true);
}

void VulkanTextureAnimator::handle_copy_clut_alpha(const DmaTransfer& tf) {
  ASSERT_NOT_REACHED();
  dprintf("[tex anim] __copy__ clut alpha\n");
  ASSERT(tf.size_bytes == sizeof(TextureAnimPcTransform));
  auto* data = (const TextureAnimPcTransform*)(tf.data);
  dprintf("  src: %d, dest: %d\n", data->src_tbp, data->dst_tbp);
  const auto& dst_tex = m_textures.find(data->dst_tbp);
  const auto& src_tex = m_textures.find(data->src_tbp);
  ASSERT(dst_tex != m_textures.end());
  if (src_tex == m_textures.end()) {
    lg::error("Skipping copy clut alpha because source texture at {} wasn't found", data->src_tbp);
    return;
  }
  ASSERT(src_tex != m_textures.end());

  ASSERT(dst_tex->second.kind == VramEntry::Kind::GPU);
  ASSERT(src_tex->second.kind == VramEntry::Kind::GPU);

  FramebufferTexturePairContext ctxt(dst_tex->second.tex.value());
  float positions[3 * 4] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  float uvs[2 * 4] = {0, 0, 1, 0, 1, 1, 0, 1};
  glUniform3fv(m_uniforms.positions, 4, positions);
  glUniform2fv(m_uniforms.uvs, 4, uvs);
  glUniform1i(m_uniforms.enable_tex, 1);
  glUniform4f(m_uniforms.rgba, 128, 128, 128, 128);  // TODO - seems wrong.
  glUniform4i(m_uniforms.channel_scramble, 0, 1, 2, 3);
  glBindTexture(GL_TEXTURE_2D, src_tex->second.tex.value().texture());

  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glColorMask(false, false, false, true);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glColorMask(true, true, true, true);
}

void VulkanTextureAnimator::run_clut_blender_group(DmaTransfer& tf, int idx) {
  float f;
  ASSERT(tf.size_bytes == 16);
  memcpy(&f, tf.data, sizeof(float));
  float weights[2] = {1.f - f, f};
  auto& blender = m_clut_blender_groups.at(idx);
  for (size_t i = 0; i < blender.blenders.size(); i++) {
    m_output_slots[blender.outputs[i]] = blender.blenders[i].run(weights);
  }
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
  auto& vram = m_textures[upload->dest];
  vram.reset();
  vram.kind = VramEntry::Kind::CLUT16_16_IN_PSM32;
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
  auto& vram = m_textures[upload->dest];
  vram.reset();

  switch (upload->format) {
    case (int)GsTex0::PSM::PSMCT32:
      vram.kind = VramEntry::Kind::GENERIC_PSM32;
      vram.data.resize(upload->width * upload->height * 4);
      vram.tex_width = upload->width;
      vram.tex_height = upload->height;
      memcpy(vram.data.data(), ee_mem + upload->data, vram.data.size());
      m_tex_looking_for_clut = nullptr;
      break;
    case (int)GsTex0::PSM::PSMT8:
      vram.kind = VramEntry::Kind::GENERIC_PSMT8;
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
 * Handle the initialization of an animated texture. This fills the entire texture with a solid
 * color. We set up a GPU texture here - drawing operations are done on the GPU, so we'd never
 * need this solid color on the CPU. Also sets a bunch of GS state for the shaders.
 * These may be modified by animation functions, but most of the time they aren't.
 */
void TextureAnimator::handle_erase_dest(DmaFollower& dma) {
  dprintf("[tex anim] erase destination texture\n");
  // auto& out = m_new_dest_textures.emplace_back();
  VramEntry* entry = nullptr;

  // first transfer will be a bunch of ad (modifies the shader)
  {
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
    int tex_width = scissor.x1() + 1;
    int tex_height = scissor.y1() + 1;
    dprintf(" size: %d x %d\n", tex_width, tex_height);

    // 1 (xyoffset-1 (new 'static 'gs-xy-offset :ofx #x8000 :ofy #x8000))
    // 2 (frame-1 (new 'static 'gs-frame :fbw (/ (+ tex-width 63) 64) :fbp fbp-for-tex))
    ASSERT(ad_data[2 * 2 + 1] == (int)GsRegisterAddress::FRAME_1);
    GsFrame frame(ad_data[2 * 2]);
    int dest_texture_address = 32 * frame.fbp();
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

    // get the entry set up for being a GPU texture.
    entry = setup_vram_entry_for_gpu_texture(tex_width, tex_height, dest_texture_address);
  }

  // next transfer is the erase. This is done with alpha blending off
  auto clear_transfer = dma.read_and_advance();
  ASSERT(clear_transfer.size_bytes == 16 * 4);
  math::Vector<u32, 4> rgba_u32;
  memcpy(rgba_u32.data(), clear_transfer.data + 16, 16);
  dfmt(" clear: {}\n", rgba_u32.to_string_hex_byte());

  // create the opengl output texture.

  // do the clear:
  {
    FramebufferTexturePairContext ctxt(entry->tex.value());
    float positions[3 * 4] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    glUniform3fv(m_uniforms.positions, 4, positions);
    glUniform1i(m_uniforms.enable_tex, 0);
    glUniform4f(m_uniforms.rgba, rgba_u32[0], rgba_u32[1], rgba_u32[2], rgba_u32[3]);
    glUniform4i(m_uniforms.channel_scramble, 0, 1, 2, 3);
    glBindTexture(GL_TEXTURE_2D, m_dummy_texture);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glColorMask(true, true, true, true);
    {
      auto p = profiler::scoped_prof("erase-draw");
      glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
  }

  // set as active
  m_current_dest_tbp = entry->dest_texture_address;
  m_erased_on_this_frame.insert(entry->dest_texture_address);
}

/*!
 * Set up this texture as a GPU texture. This does a few things:
 * - sets the Kind to GPU
 * - makes sure the texture resource points to a valid OpenGL texture of the right size, without
 *   triggering the resize/delete sync issue mentioned above.
 * - sets flags to indicate if this GPU texture needs to be updated in the pool.
 */
VramEntry* TextureAnimator::setup_vram_entry_for_gpu_texture(int w, int h, int tbp) {
  auto pp = profiler::scoped_prof("setup-vram-entry");
  const auto& existing_dest = m_textures.find(tbp);

  // see if we have an existing OpenGL texture at all
  bool existing_opengl = existing_dest != m_textures.end() && existing_dest->second.tex.has_value();

  // see if we can reuse it (same size)
  bool can_reuse = true;
  if (existing_opengl) {
    if (existing_dest->second.tex->height() != h || existing_dest->second.tex->width() != w) {
      dprintf(" can't reuse, size mismatch\n");
      can_reuse = false;
    }
  } else {
    dprintf(" can't reuse, first time using this address\n");
    can_reuse = false;
  }

  VramEntry* entry = nullptr;
  if (can_reuse) {
    // texture is the right size, just use it again.
    entry = &existing_dest->second;
  } else {
    if (existing_opengl) {
      // we have a texture, but it's the wrong type. Remember that we need to update the pool
      entry = &existing_dest->second;
      entry->needs_pool_update = true;
    } else {
      // create the entry. Also need to update the pool
      entry = &m_textures[tbp];
      entry->reset();
      entry->needs_pool_update = true;
    }

    // if we already have a texture, try to swap it with an OpenGL texture of the right size.
    if (entry->tex.has_value()) {
      // gross
      m_opengl_texture_pool.free(entry->tex->texture(), entry->tex->width(), entry->tex->height());
      entry->tex->update_texture_size(w, h);
      entry->tex->update_texture_unsafe(m_opengl_texture_pool.allocate(w, h));
    } else {
      entry->tex.emplace(w, h, GL_UNSIGNED_INT_8_8_8_8_REV);
    }
  }

  entry->kind = VramEntry::Kind::GPU;
  entry->tex_width = w;
  entry->tex_height = h;
  entry->dest_texture_address = tbp;
  return entry;
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
  const auto& clut_lookup = m_textures.find(cbp);
  if (clut_lookup == m_textures.end()) {
    printf("get_clut_16_16_psm32 referenced an unknown clut texture in %d\n", cbp);
    ASSERT_NOT_REACHED();
  }

  if (clut_lookup->second.kind != VramEntry::Kind::CLUT16_16_IN_PSM32) {
    ASSERT_NOT_REACHED();
  }

  return (const u32*)clut_lookup->second.data.data();
}

/*!
 * Using the current shader settings, load the CLUT table to the texture coverter "VRAM".
 */
void BaseTextureAnimator::load_clut_to_converter() {
  const auto& clut_lookup = m_textures.find(m_current_shader.tex0.cbp());
  if (clut_lookup == m_textures.end()) {
    printf("set shader referenced an unknown clut texture in %d\n", m_current_shader.tex0.cbp());
    ASSERT_NOT_REACHED();
  }

  switch (clut_lookup->second.kind) {
    case VramEntry::Kind::CLUT16_16_IN_PSM32:
      m_converter.upload_width(clut_lookup->second.data.data(), m_current_shader.tex0.cbp(), 16,
                               16);
      break;
    default:
      printf("unhandled clut source kind: %d\n", (int)clut_lookup->second.kind);
      ASSERT_NOT_REACHED();
  }
}

/*!
 * Read the current shader settings, and get/create/setup a GPU texture for the source texture.
 */
GLuint TextureAnimator::make_or_get_gpu_texture_for_current_shader(TexturePool& texture_pool) {
  u32 tbp = m_current_shader.tex0.tbp0();
  const auto& lookup = m_textures.find(m_current_shader.tex0.tbp0());
  if (lookup == m_textures.end()) {
    auto tpool = texture_pool.lookup(tbp);
    if (tpool.has_value()) {
      return *tpool;
    }
    // printf("referenced an unknown texture in %d\n", tbp);
    lg::error("unknown texture in {} (0x{:x})", tbp, tbp);
    return texture_pool.get_placeholder_texture();

    // ASSERT_NOT_REACHED();
  }

  auto* vram_entry = &lookup->second;

  // see what format the source is
  switch (vram_entry->kind) {
    case VramEntry::Kind::GPU:
      // already on the GPU, just return it.
      return lookup->second.tex->texture();
    // data on the CPU, in PSM32
    case VramEntry::Kind::GENERIC_PSM32:
      // see how we're reading it:
      switch (m_current_shader.tex0.psm()) {
        // reading as a different format, needs scrambler.
        case GsTex0::PSM::PSMT8: {
          int w = 1 << m_current_shader.tex0.tw();
          int h = 1 << m_current_shader.tex0.th();
          ASSERT(w == vram_entry->tex_width * 2);
          ASSERT(h == vram_entry->tex_height * 2);

          Timer timer;
          m_converter.upload_width(vram_entry->data.data(), m_current_shader.tex0.tbp0(),
                                   vram_entry->tex_width, vram_entry->tex_height);

          // also needs clut lookup
          load_clut_to_converter();
          {
            std::vector<u32> rgba_data(w * h);
            m_converter.download_rgba8888(
                (u8*)rgba_data.data(), m_current_shader.tex0.tbp0(), m_current_shader.tex0.tbw(), w,
                h, (int)m_current_shader.tex0.psm(), (int)m_current_shader.tex0.cpsm(),
                m_current_shader.tex0.cbp(), rgba_data.size() * 4);
            //              file_util::write_rgba_png("out.png", rgba_data.data(), 1 <<
            //              m_current_shader.tex0.tw(),
            //                                        1 << m_current_shader.tex0.th());
            dprintf("processing %d x %d took %.3f ms\n", w, h, timer.getMs());
            return make_temp_gpu_texture(rgba_data.data(), w, h);
          }

          ASSERT_NOT_REACHED();
        } break;
        default:
          fmt::print("unhandled source texture format {}\n", (int)m_current_shader.tex0.psm());
          ASSERT_NOT_REACHED();
      }
      break;
    case VramEntry::Kind::CLUT16_16_IN_PSM32:
      ASSERT_NOT_REACHED();

      /*
    case VramEntry::Kind::GENERIC_PSMT8: {
      fmt::print("drawing: {}\n", (int)m_current_shader.tex0.psm());
      ASSERT(m_current_shader.tex0.psm() == GsTex0::PSM::PSMT8);
      ASSERT(m_current_shader.tex0.cpsm() == 0);  // psm32.
      int tw = 1 << m_current_shader.tex0.tw();
      int th = 1 << m_current_shader.tex0.th();
      ASSERT(tw == vram_entry->tex_width);
      ASSERT(th == vram_entry->tex_height);
      std::vector<u32> rgba_data(tw * th);
      const u32* clut = get_current_clut_16_16_psm32();
      for (int r = 0; r < th; r++) {
        for (int c = 0; c < tw; c++) {
          rgba_data[c + r * tw] = clut[vram_entry->data[c + r * tw]];
        }
      }
      return make_temp_gpu_texture(rgba_data.data(), tw, th);
    }
       */

      break;
    default:
      ASSERT_NOT_REACHED();
  }
}

void TextureAnimator::set_up_opengl_for_shader(const ShaderContext& shader,
                                               std::optional<GLuint> texture,
                                               bool prim_abe) {
  if (texture) {
    glBindTexture(GL_TEXTURE_2D, *texture);
    glUniform1i(m_uniforms.enable_tex, 1);
  } else {
    glBindTexture(GL_TEXTURE_2D, m_dummy_texture);
    glUniform1i(m_uniforms.enable_tex, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  // tex0
  u32 tcc = shader.tex0.tcc();
  ASSERT(tcc == 1 || tcc == 0);
  glUniform1i(m_uniforms.tcc, tcc);

  ASSERT(shader.tex0.tfx() == GsTex0::TextureFunction::MODULATE);
  // tex1
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  shader.tex1.mmag() ? GL_LINEAR : GL_NEAREST);
  switch (shader.tex1.mmin()) {
    case 0:
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      break;
    case 1:
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      break;
    default:
      ASSERT_NOT_REACHED();
  }

  bool do_alpha_test = false;
  bool alpha_test_mask_alpha_trick = false;
  bool alpha_test_mask_depth_trick = false;

  // test
  if (shader.test.alpha_test_enable()) {
    auto atst = shader.test.alpha_test();
    if (atst == GsTest::AlphaTest::ALWAYS) {
      do_alpha_test = false;
      // atest effectively disabled - everybody passes.
    } else if (atst == GsTest::AlphaTest::NEVER) {
      // everybody fails. They use alpha test to mask out some channel
      do_alpha_test = false;

      switch (shader.test.afail()) {
        case GsTest::AlphaFail::RGB_ONLY:
          alpha_test_mask_alpha_trick = true;
          break;
        case GsTest::AlphaFail::FB_ONLY:
          alpha_test_mask_depth_trick = true;
          break;
        default:
          ASSERT_NOT_REACHED();
      }

    } else {
      ASSERT_NOT_REACHED();
    }
  } else {
    do_alpha_test = false;
  }

  if (alpha_test_mask_alpha_trick) {
    glColorMask(true, true, true, false);
  } else {
    glColorMask(true, true, true, true);
  }

  if (alpha_test_mask_depth_trick) {
    glDepthMask(GL_FALSE);
  } else {
    glDepthMask(GL_TRUE);
  }

  ASSERT(shader.test.date() == false);
  // DATM
  ASSERT(shader.test.zte() == true);  // required
  switch (shader.test.ztest()) {
    case GsTest::ZTest::ALWAYS:
      glDisable(GL_DEPTH_TEST);
      break;
    default:
      ASSERT(false);
  }

  if (shader.clamp_u) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  }

  if (shader.clamp_v) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }

  if (prim_abe) {
    auto blend_a = shader.alpha.a_mode();
    auto blend_b = shader.alpha.b_mode();
    auto blend_c = shader.alpha.c_mode();
    auto blend_d = shader.alpha.d_mode();
    glEnable(GL_BLEND);

    // 0 2 0 1
    if (blend_a == GsAlpha::BlendMode::SOURCE && blend_b == GsAlpha::BlendMode::ZERO_OR_FIXED &&
        blend_c == GsAlpha::BlendMode::SOURCE && blend_d == GsAlpha::BlendMode::DEST) {
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
    } else if (blend_a == GsAlpha::BlendMode::SOURCE && blend_b == GsAlpha::BlendMode::DEST &&
               blend_c == GsAlpha::BlendMode::SOURCE && blend_d == GsAlpha::BlendMode::DEST) {
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    } else {
      fmt::print("unhandled blend: {} {} {} {}\n", (int)blend_a, (int)blend_b, (int)blend_c,
                 (int)blend_d);
      ASSERT_NOT_REACHED();
    }

  } else {
    glDisable(GL_BLEND);
  }
  glUniform4i(m_uniforms.channel_scramble, 0, 1, 2, 3);
}

BaseVramEntry& VulkanTextureAnimator::get_vram_entry_at_index(unsigned) {
}
bool VulkanTextureAnimator::s_vram_entry_available_at_index(unsigned) {
}

void VulkanTextureAnimator::init_shaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::TEX_ANIM];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "main";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

void VulkanTextureAnimator::create_pipeline_layout() {
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  std::array<VkPushConstantRange, 2> pushConstantRanges;
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(m_vertex_push_constant);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset =
      sizeof(m_vertex_push_constant);  // Offset need to be a multiple of the push constant size
  pushConstantRanges[1].size = sizeof(m_fragment_push_constant);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void VulkanTextureAnimator::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  VkVertexInputAttributeDescription attributeDescription{};
  attributeDescription.binding = 0;
  attributeDescription.location = 0;
  attributeDescription.format = VK_FORMAT_R32G32B32A32_SINT;
  attributeDescription.offset = 0;
  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescription);
}

void VulkanTextureAnimator::Draw(math::Vector4f positions[3], math::Vector2f uvs[4], bool enable_texture, math::Vector4f& rgba, math::Vector<int, 4> channel_scramble, std::array<bool, 4> colorMask) {
  m_vertex_push_constant.positions = positions;
  m_vertex_push_constant.uvs = uvs;
  m_fragment_push_constant.enable_tex = enable_texture;
  m_fragment_push_constant.rgba = rgba;  // TODO - seems wrong.
  m_fragment_push_constant.channel_scramble = channel_scramble;
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_vertex_push_constant),
                     (void*)&m_vertex_push_constant);
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_vertex_push_constant),
                     sizeof(m_fragment_push_constant), (void*)&m_fragment_push_constant);

  glBindTexture(GL_TEXTURE_2D, src->second.tex.value().texture());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  m_pipeline_config_info.BlendEnable = false;
  m_pipeline_config_info.DepthTestEnable = false;
}
