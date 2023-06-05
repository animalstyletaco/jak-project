#include "Merc2.h"

#include <mutex>

#include "game/graphics/vulkan_renderer/background/background_common.h"
#include "game/graphics/gfx.h"
#include "game/graphics/vulkan_renderer/EyeRenderer.h"


/* Merc 2 renderer:
 The merc2 renderer is the main "foreground" renderer, which draws characters, collectables,
 and even some water.
 The PC format renderer does the usual tricks of buffering stuff head of time as much as possible.
 The main trick here is to buffer up draws and upload "bones" (skinning matrix) for many draws all
 at once.
 The other tricky part is "mod vertices", which may be modified by the game.
 We know ahead of time which vertices could be modified, and have a way to upload only those
 vertices.
 Each "merc model" corresponds to a merc-ctrl in game. There's one merc-ctrl per LOD of an
 art-group. So generally, this will be something like "jak" or "orb" or "some enemy".
 Each model is made up of "effect"s. There are a number of per-effect settings, like environment
 mapping. Generally, the purpose of an "effect" is to divide up a model into parts that should be
 rendered with a different configuration.
 Within each model, there are fragments. These correspond to how much data can be uploaded to VU1
 memory. For the most part, fragments are not considered by the PC renderer. The only exception is
 updating vertices - we must read the data from the game, which is stored in fragments.
 Per level, there is an FR3 file loaded by the loader. Each merc renderer can access multiple
 levels.
*/

/*!
 * Remaining ideas for optimization:
 * - port blerc to C++, do it in the rendering thread and avoid the lock.
 * - combine envmap draws per effect (might require some funky indexing stuff, or multidraw)
 * - smaller vertex formats for mod-vertex
 * - AVX version of vertex conversion math
 * - eliminate the "copy" step of vertex modification
 * - batch uploading the vertex modification data
 */

MercVulkan2::MercVulkan2(const std::string& name,
             int my_id,
             std::unique_ptr<GraphicsDeviceVulkan>& device,
             VulkanInitializationInfo& vulkan_info) :
  BaseMerc2(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  m_tie_vertex_push_constant.height_scale = (vulkan_info.m_version == GameVersion::Jak2) ? 0.5 : 1;
  m_tie_vertex_push_constant.scissor_adjust = (vulkan_info.m_version == GameVersion::Jak2) ? (-512 / 416.f) : (-512/448.f);

  m_light_control_vertex_uniform_buffer =
      std::make_unique<MercLightControlVertexUniformBuffer>(m_device, MAX_DRAWS_PER_LEVEL, 1);
  m_bone_vertex_uniform_buffer = std::make_unique<MercBoneVertexUniformBuffer>(device);

  // annoyingly, glBindBufferRange can have alignment restrictions that vary per platform.
  // the GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT gives us the minimum alignment for views into the bone
  // buffer. The bone buffer stores things per-16-byte "quadword".
  uint32_t minimum_uniform_buffer_alignment_size = m_device->getMinimumBufferOffsetAlignment();
  if (minimum_uniform_buffer_alignment_size <= 16) {
    // somehow doubt this can happen, but just in case
    m_graphics_buffer_alignment = 1;
  } else {
    m_graphics_buffer_alignment =
        minimum_uniform_buffer_alignment_size / 16;  // number of bone vectors
    if (m_graphics_buffer_alignment * 16 != (u32)minimum_uniform_buffer_alignment_size) {
      ASSERT_MSG(false, fmt::format("Vulkan uniform buffer alignment is {}, which is strange\n",
                                    minimum_uniform_buffer_alignment_size));
    }
  }

  m_vertex_descriptor_layout =
    DescriptorLayout::Builder(m_device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

  m_emerc_vertex_descriptor_layout = DescriptorLayout::Builder(m_device)
    .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT).build();

  //Emerc fragment descriptor is the same as standard merc no need for separate object
  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);
  m_emerc_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);
  m_emerc_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                          m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  m_emerc_descriptor_sets.resize(2);

  m_light_control_vertex_buffer_descriptor_info =
      m_light_control_vertex_uniform_buffer->descriptorInfo();
  m_bone_vertex_buffer_descriptor_info = m_bone_vertex_uniform_buffer
      ->descriptorInfo();

  m_vertex_descriptor_writer->writeBuffer(0, &m_light_control_vertex_buffer_descriptor_info)
      .writeBuffer(1, &m_bone_vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);

  m_placeholder_descriptor_image_info =
      *m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info();
  m_fragment_descriptor_writer->writeImage(1, &m_placeholder_descriptor_image_info, 2);
  m_emerc_fragment_descriptor_writer->writeImage(1, &m_placeholder_descriptor_image_info, 2);

  create_pipeline_layout();
  for (int i = 0; i < MAX_LEVELS; i++) {
    auto& bucket = m_level_draw_buckets.emplace_back();
    bucket.draws.resize(MAX_DRAWS_PER_LEVEL);
    bucket.envmap_draws.resize(MAX_ENVMAP_DRAWS_PER_LEVEL);
    bucket.samplers.resize(MAX_DRAWS_PER_LEVEL, m_device);
    bucket.descriptor_image_infos.resize(MAX_DRAWS_PER_LEVEL);
    bucket.pipeline_layouts.resize(MAX_DRAWS_PER_LEVEL, m_device);
  }

  init_shaders();
  InitializeInputAttributes();
}

/*!
 * Main MercVulkan2 rendering.
 */
void MercVulkan2::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseMerc2::render(dma, render_state, prof);
}

void MercVulkan2::create_pipeline_layout() {
    std::vector<VkDescriptorSetLayout> mercDescriptorSetLayouts{
        m_vertex_descriptor_layout->getDescriptorSetLayout(),
        m_fragment_descriptor_layout->getDescriptorSetLayout()};

    std::vector<VkDescriptorSetLayout> emercDescriptorSetLayouts{
        m_emerc_vertex_descriptor_layout->getDescriptorSetLayout(),
        m_fragment_descriptor_layout->getDescriptorSetLayout()};

    std::array<VkPushConstantRange, 2> pushConstantRanges{};
    pushConstantRanges[0].offset = 0;
    pushConstantRanges[0].size = sizeof(m_tie_vertex_push_constant);
    pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    pushConstantRanges[1].offset = pushConstantRanges[0].size;
    pushConstantRanges[1].size = sizeof(m_fragment_push_constant);
    pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo mercPipelineLayoutInfo{};
    mercPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    mercPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(mercDescriptorSetLayouts.size());
    mercPipelineLayoutInfo.pSetLayouts = mercDescriptorSetLayouts.data();
    
    mercPipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
    mercPipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

    VkPipelineLayoutCreateInfo emercPipelineLayoutInfo{};
    emercPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    emercPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(emercDescriptorSetLayouts.size());
    emercPipelineLayoutInfo.pSetLayouts = emercDescriptorSetLayouts.data();

    std::array<VkPushConstantRange, 2> emercPushConstantRanges{};
    emercPushConstantRanges[0].offset = 0;
    emercPushConstantRanges[0].size = sizeof(m_etie_vertex_push_constant);
    emercPushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    emercPushConstantRanges[1].offset = pushConstantRanges[0].size;
    emercPushConstantRanges[1].size = sizeof(float);
    emercPushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    emercPipelineLayoutInfo.pPushConstantRanges = emercPushConstantRanges.data();
    emercPipelineLayoutInfo.pushConstantRangeCount = emercPushConstantRanges.size();

    if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &mercPipelineLayoutInfo, nullptr,
                               &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &emercPipelineLayoutInfo, nullptr,
                               &m_emerc_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }
}

void MercVulkan2::flush_draw_buckets(BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& prof) {
  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant.height_scale),
                     (void*)&m_push_constant.height_scale);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  m_stats.num_draw_flush++;

  for (u32 li = 0; li < m_next_free_level_bucket; li++) {
    auto& lev_bucket = m_level_draw_buckets[li];

    VkDeviceSize offsets[] = {0};
    VkBuffer vertex_buffer_vulkan = lev_bucket.level->merc_vertices->getBuffer();
    vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan,
                           offsets);

    vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                         lev_bucket.level->merc_indices->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);

    draw_merc2(lev_bucket, prof);
    if (lev_bucket.next_free_envmap_draw) {
      draw_emercs(lev_bucket, prof);
    }
  }

  m_next_free_light = 0;
  m_next_free_bone_vector = 0;
  m_next_free_level_bucket = 0;
}

void MercVulkan2::draw_merc2(LevelDrawBucketVulkan& lev_bucket, ScopedProfilerNode& prof) {
    auto* lev = lev_bucket.level;

    int last_tex = -1;
    int last_light = -1;
    m_stats.num_bones_uploaded += m_next_free_bone_vector;

    for (u32 di = 0; di < lev_bucket.next_free_draw; di++) {
      auto& draw = lev_bucket.draws[di];
      auto& sampler = lev_bucket.samplers[di];

      VulkanTexture* textureInfo = nullptr;
      if (draw.texture < lev->textures_map.size()) {
        textureInfo = &lev->textures_map.at(draw.texture);
      } else if ((draw.texture & 0xffffff00) == 0xffffff00) {
        //textureInfo = &render_state->eye_renderer->lookup_eye_texture(draw.texture & 0xff);
      }

      if ((int)draw.light_idx != last_light) {
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_direction0", m_lights_buffer[draw.light_idx].direction0, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_direction1", m_lights_buffer[draw.light_idx].direction1, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_direction2", m_lights_buffer[draw.light_idx].direction2, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector4f(
            "light_color0", m_lights_buffer[draw.light_idx].color0, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector4f(
            "light_color1", m_lights_buffer[draw.light_idx].color1, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector4f(
            "light_color2", m_lights_buffer[draw.light_idx].color2, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector4f(
            "light_ambient", m_lights_buffer[draw.light_idx].ambient, di);
        last_light = draw.light_idx;
      }

      prof.add_draw_call();
      prof.add_tri(draw.num_triangles);

      FinalizeVulkanDraw(di, lev_bucket, textureInfo);
    }
}

void MercVulkan2::draw_emercs(LevelDrawBucketVulkan& lev_bucket, ScopedProfilerNode& prof) {
   auto* lev = lev_bucket.level;
   
   int last_tex = -1;
   int last_light = -1;
   m_stats.num_bones_uploaded += m_next_free_bone_vector;
   
   for (u32 di = 0; di < lev_bucket.next_free_draw; di++) {
     auto& draw = lev_bucket.draws[di];
     auto& sampler = lev_bucket.samplers[di];
   
     VulkanTexture& textureInfo = lev->textures_map.at(draw.texture);

     prof.add_draw_call();
     prof.add_tri(draw.num_triangles);
   
     FinalizeVulkanDraw(di, lev_bucket, &textureInfo);
   }
}

void MercVulkan2::FinalizeVulkanDraw(uint32_t drawIndex, LevelDrawBucketVulkan& lev_bucket, VulkanTexture* texture) {
  auto& draw = lev_bucket.draws[drawIndex];
  auto& sampler = lev_bucket.samplers[drawIndex];

  vulkan_background_common::setup_vulkan_from_draw_mode(draw.mode, sampler, m_pipeline_config_info,
                                                  true);

  m_fragment_push_constant.settings |= draw.mode.get_decal();

  uint32_t dynamic_descriptors_offset = drawIndex * sizeof(MercLightControlVertexUniformBuffer);

  m_bone_vertex_uniform_buffer->map();
  m_bone_vertex_uniform_buffer->writeToCpuBuffer(m_shader_bone_vector_buffer,
                                                 sizeof(math::Vector4f) * draw.index_count,
                                                 sizeof(math::Vector4f) * draw.first_bone);
  m_bone_vertex_uniform_buffer->unmap();

  lev_bucket.pipeline_layouts[drawIndex].createGraphicsPipeline(m_pipeline_config_info);
  lev_bucket.pipeline_layouts[drawIndex].bind(m_vulkan_info.render_command_buffer);

  lev_bucket.descriptor_image_infos[drawIndex] = {sampler.GetSampler(), texture->getImageView(),
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  m_fragment_descriptor_writer->writeImage(1, &lev_bucket.descriptor_image_infos[drawIndex]);

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, m_descriptor_sets.size(),
                          m_descriptor_sets.data(), 1, &dynamic_descriptors_offset);
  vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, draw.index_count, 1, draw.first_index, 0,
                   0);
}

void MercVulkan2::set_merc_uniform_buffer_data(const DmaTransfer& dma) {
  memcpy(&m_low_memory, dma.data + 16, sizeof(LowMemory));
  m_tie_vertex_push_constant.camera_control.hvdf_offset = m_low_memory.hvdf_offset;
  m_tie_vertex_push_constant.camera_control.fog_constants = m_low_memory.fog;
  ::memcpy(&m_tie_vertex_push_constant.perspective_matrix, &m_low_memory.perspective[0],
           sizeof(math::Matrix4f));

  m_etie_vertex_push_constant.etie_data.hvdf_offset = m_low_memory.hvdf_offset;
  m_etie_vertex_push_constant.etie_data.fog_constants = m_low_memory.fog;
  // todo rm.
  ::memcpy(&m_etie_vertex_push_constant.perspective_matrix, &m_low_memory.perspective[0], sizeof(math::Matrix4f));
}

/*!
 * Handle the merc renderer switching to a different model.
 */
void MercVulkan2::handle_pc_model(const DmaTransfer& setup,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& proff) {
  auto p = scoped_prof("init-pc");

  // the format of the data is:
  //  ;; name   (128 char, 8 qw)
  //  ;; lights (7 qw x 1)
  //  ;; matrix slot string (128 char, 8 qw)
  //  ;; matrices (7 qw x N)
  //  ;; flags    (num-effects, effect-alpha-ignore, effect-disable)
  //  ;; fades    (u32 x N), padding to qw aligned
  //  ;; pointers (u32 x N), padding

  // Get the name
  const u8* input_data = setup.data;
  ASSERT(strlen((const char*)input_data) < 127);
  char name[128];
  strcpy(name, (const char*)setup.data);
  input_data += 128;

  // Look up the model by name in the loader.
  // This will return a reference to this model's data, plus a reference to the level's data
  // for stuff shared between models of the same level
  auto model_ref = m_vulkan_info.loader->get_merc_model(name);
  if (!model_ref) {
    // it can fail, if the game is faster than the loader. In this case, we just don't draw.
    m_stats.num_missing_models++;
    return;
  }

  // next, we need to check if we have enough room to draw this effect.
  LevelDataVulkan* lev = model_ref->level;
  const tfrag3::MercModel* model = model_ref->model;
  ModSettings settings{};

  // each model uses only 1 light.
  if (m_next_free_light >= MAX_LIGHTS) {
    fmt::print("MERC2 out of lights, consider increasing MAX_LIGHTS\n");
    flush_draw_buckets(render_state, proff);
  }

  // models use many bones. First check if we need to flush:
  int bone_count = model->max_bones + 1;
  if (m_next_free_bone_vector + m_graphics_buffer_alignment + bone_count * 8 >
      MAX_SHADER_BONE_VECTORS) {
    fmt::print("MERC2 out of bones, consider increasing MAX_SHADER_BONE_VECTORS\n");
    flush_draw_buckets(render_state, proff);
  }

  // also sanity check that we have enough to draw the model
  if (m_graphics_buffer_alignment + bone_count * 8 > MAX_SHADER_BONE_VECTORS) {
    fmt::print(
        "MERC2 doesn't have enough bones to draw a model, increase MAX_SHADER_BONE_VECTORS\n");
    ASSERT_NOT_REACHED();
  }

  // next, we need to find a bucket that holds draws for this level (will have the right buffers
  // bound for drawing)
  LevelDrawBucketVulkan* lev_bucket = nullptr;
  for (u32 i = 0; i < m_next_free_level_bucket; i++) {
    if (m_level_draw_buckets[i].level == lev) {
      lev_bucket = &m_level_draw_buckets[i];
      break;
    }
  }

  if (!lev_bucket) {
    // no existing bucket, allocate a new one.
    if (m_next_free_level_bucket >= m_level_draw_buckets.size()) {
      // out of room, flush
      // fmt::print("MERC2 out of levels, consider increasing MAX_LEVELS\n");
      flush_draw_buckets(render_state, proff);
    }
    // alloc a new one
    lev_bucket = &m_level_draw_buckets[m_next_free_level_bucket++];
    lev_bucket->reset();
    lev_bucket->level = lev;
  }

  // next check draws:
  if (lev_bucket->next_free_draw + model->max_draws >= lev_bucket->draws.size()) {
    // out of room, flush
    fmt::print("MERC2 out of draws, consider increasing MAX_DRAWS_PER_LEVEL\n");
    flush_draw_buckets(render_state, proff);
    if (model->max_draws >= lev_bucket->draws.size()) {
      ASSERT_NOT_REACHED_MSG("MERC2 draw buffer not big enough");
    }
  }

  // same for envmap draws
  if (lev_bucket->next_free_envmap_draw + model->max_draws >= lev_bucket->envmap_draws.size()) {
    // out of room, flush
    fmt::print("MERC2 out of envmap draws, consider increasing MAX_ENVMAP_DRAWS_PER_LEVEL\n");
    flush_draw_buckets(render_state, proff);
    if (model->max_draws >= lev_bucket->envmap_draws.size()) {
      ASSERT_NOT_REACHED_MSG("MERC2 envmap draw buffer not big enough");
    }
  }

  // Next part of input data is the lights
  VuLights current_lights;
  memcpy(&current_lights, input_data, sizeof(VuLights));
  input_data += sizeof(VuLights);

  u64 uses_water = 0;
  if (render_state->version == GameVersion::Jak1) {
    // jak 1 figures out water at runtime sadly
    memcpy(&uses_water, input_data, 8);
    input_data += 16;
  }
  settings.uses_water = uses_water;

  // Next part is the matrix slot string. The game sends us a bunch of bone matrices,
  // but they may not be in order, or include all bones. The matrix slot string tells
  // us which bones go where. (the game doesn't go in order because it follows the merc format)
  ShaderMercMat skel_matrix_buffer[MAX_SKEL_BONES];
  auto* matrix_array = (const u32*)(input_data + 128);
  int i;
  for (i = 0; i < 128; i++) {
    if (input_data[i] == 0xff) {  // indicates end of string.
      break;
    }
    // read goal addr of matrix (matrix data isn't known at merc dma time, bones runs after)
    u32 addr;
    memcpy(&addr, &matrix_array[i * 4], 4);
    const u8* real_addr = setup.data - setup.data_offset + addr;
    ASSERT(input_data[i] < MAX_SKEL_BONES);
    // get the matrix data
    memcpy(&skel_matrix_buffer[input_data[i]], real_addr, sizeof(MercMat));
  }
  input_data += 128 + 16 * i;

  // Next part is some flags

  const PcMercFlags* flags = (const PcMercFlags*)input_data;
  int num_effects = flags->effect_count;  // mostly just a sanity check
  ASSERT(num_effects < kMaxEffect);
  u64 current_ignore_alpha_bits = flags->ignore_alpha_mask;  // shader settings
  u64 current_effect_enable_bits = flags->enable_mask;       // mask for game to disable an effect
  settings.model_uses_mod = flags->bitflags & 1;  // if we should update vertices from game.
  settings.model_disables_fog = (flags->bitflags & 2);
  input_data += 32;

  // Next is "fade data", indicating the color/intensity of envmap effect
  u8 fade_buffer[4 * kMaxEffect];
  for (int ei = 0; ei < num_effects; ei++) {
    for (int j = 0; j < 4; j++) {
      fade_buffer[ei * 4 + j] = input_data[ei * 4 + j];
    }
  }
  input_data += (((num_effects * 4) + 15) / 16) * 16;

  // Next is pointers to merc data, needed so we can update vertices

  // will hold graphics vertex buffers for the updated vertices
  std::unordered_map<uint32_t, std::unique_ptr<VertexBuffer>> mod_graphics_buffers;
  if (/*settings.model_uses_mod*/ 0) {  // only if we've enabled, this path is slow.
    auto p = scoped_prof("update-verts");

    // loop over effects. Mod vertices are done per effect (possibly a bad idea?)
    for (unsigned ei = 0; ei < num_effects; ei++) {
      const auto& effect = model_ref->model->effects[ei];
      // some effects might have no mod draw info, and no modifiable vertices
      if (effect.mod.mod_draw.empty()) {
        continue;
      }

      prof().begin_event("start1");
      std::unique_ptr<VertexBuffer> mod_vertex_buffer = std::make_unique<VertexBuffer>(*model_ref->level->merc_vertices);
      mod_graphics_buffers.insert({ei, std::move(mod_vertex_buffer)});

      // check that we have enough room for the finished thing.
      if (effect.mod.vertices.size() > MAX_MOD_VTX) {
        fmt::print("More mod vertices than MAX_MOD_VTX. {} > {}\n", effect.mod.vertices.size(),
                   MAX_MOD_VTX);
        ASSERT_NOT_REACHED();
      }

      // check that we have enough room for unpack
      if (effect.mod.expect_vidx_end > MAX_MOD_VTX) {
        fmt::print("More mod vertices (temp) than MAX_MOD_VTX. {} > {}\n",
                   effect.mod.expect_vidx_end, MAX_MOD_VTX);
        ASSERT_NOT_REACHED();
      }

      handle_mod_vertices(setup, effect, input_data, ei, model);

      // and upload to GPU
      m_stats.num_uploads++;
      m_stats.num_upload_bytes += effect.mod.vertices.size() * sizeof(tfrag3::MercVertex);
      {
        auto pp = scoped_prof("update-verts-upload");
        mod_graphics_buffers.at(ei)->writeToGpuBuffer(
            m_mod_vtx_temp.data(), effect.mod.vertices.size() * sizeof(tfrag3::MercVertex), 0);
      }
    }

    // stats
    m_stats.num_models++;
    for (const auto& effect : model_ref->model->effects) {
      bool envmap = effect.has_envmap;
      m_stats.num_effects++;
      m_stats.num_predicted_draws += effect.all_draws.size();
      if (envmap) {
        m_stats.num_envmap_effects++;
        m_stats.num_predicted_draws += effect.all_draws.size();
      }
      for (const auto& draw : effect.all_draws) {
        m_stats.num_predicted_tris += draw.num_triangles;
        if (envmap) {
          m_stats.num_predicted_tris += draw.num_triangles;
        }
      }
    }

    if (m_debug_mode) {
      auto& d = m_debug.model_list.emplace_back();
      d.name = model->name;
      d.level = model_ref->level->level->level_name;
      for (auto& e : model->effects) {
        auto& de = d.effects.emplace_back();
        de.envmap = e.has_envmap;
        de.envmap_mode = e.envmap_mode;
        for (auto& draw : e.all_draws) {
          auto& dd = de.draws.emplace_back();
          dd.mode = draw.mode;
          dd.num_tris = draw.num_triangles;
        }
      }
    }

    // allocate bones in shared bone buffer to be sent to GPU at flush-time
    settings.first_bone = alloc_bones(bone_count, skel_matrix_buffer);

    // allocate lights
    settings.lights = alloc_lights(current_lights);

    // loop over effects, creating draws for each
    for (size_t ei = 0; ei < model->effects.size(); ei++) {
      // game has disabled it?
      if (!(current_effect_enable_bits & (1 << ei))) {
        continue;
      }

      // imgui menu disabled it?
      if (!m_effect_debug_mask[ei]) {
        continue;
      }

      bool ignore_alpha = (current_ignore_alpha_bits & (1 << ei));
      do_mod_draws(model->effects[ei], lev_bucket, fade_buffer, ei, settings, mod_graphics_buffers);
    }
  }
}

void MercVulkan2::do_mod_draws(const tfrag3::MercEffect& effect, 
                               LevelDrawBucketVulkan* lev_bucket, u8* fade_buffer,
                               uint32_t index,
                               ModSettings& settings,
                               std::unordered_map<uint32_t, std::unique_ptr<VertexBuffer>>& mod_graphics_buffers) {


  bool should_envmap = effect.has_envmap;
  bool should_mod = settings.model_uses_mod && effect.has_mod_draw;

  if (should_mod) {
    // draw as two parts, fixed and mod

    // do fixed draws:
    for (auto& fdraw : effect.mod.fix_draw) {
      alloc_normal_draw(fdraw, settings.ignore_alpha, lev_bucket, settings.first_bone, settings.lights, settings.uses_water, settings.model_disables_fog);
      if (should_envmap) {
        try_alloc_envmap_draw(fdraw, effect.envmap_mode, effect.envmap_texture, lev_bucket,
                              fade_buffer + 4 * index, settings.first_bone, settings.lights, settings.uses_water);
      }
    }

    // do mod draws
    for (auto& mdraw : effect.mod.mod_draw) {
      auto normal_draw = alloc_normal_draw(mdraw, settings.ignore_alpha, lev_bucket, settings.first_bone, settings.lights, settings.uses_water, settings.model_disables_fog);
      // modify the draw, set the mod flag and point it to the opengl buffer
      normal_draw->flags |= MOD_VTX;
      normal_draw->mod_vtx_buffer = std::move(mod_graphics_buffers[index]);
      if (should_envmap) {
        auto envmap_draw = try_alloc_envmap_draw(mdraw, effect.envmap_mode, effect.envmap_texture, lev_bucket,
                                       fade_buffer + 4 * index, settings.first_bone, settings.lights, settings.uses_water);
        envmap_draw->flags |= MOD_VTX;
        envmap_draw->mod_vtx_buffer = std::move(mod_graphics_buffers[index]);
      }
    }
  } else {
    // no mod, just do all_draws
    for (auto& draw : effect.all_draws) {
      if (should_envmap) {
        try_alloc_envmap_draw(draw, effect.envmap_mode, effect.envmap_texture, lev_bucket,
                              fade_buffer + 4 * index, settings.first_bone, settings.lights, settings.uses_water);
      }
      alloc_normal_draw(draw, settings.ignore_alpha, lev_bucket, settings.first_bone, settings.lights, settings.uses_water, settings.model_disables_fog);
    }
  }
}

MercVulkan2::VulkanDraw* MercVulkan2::try_alloc_envmap_draw(const tfrag3::MercDraw& mdraw, const DrawMode& envmap_mode,
                                                            u32 envmap_texture, LevelDrawBucketVulkan* lev_bucket,
                                                            const u8* fade,
                                                            u32 first_bone,
                                                            u32 lights,
                                                            bool jak1_water_mode) {
  bool nonzero_fade = false;
  for (int i = 0; i < 4; i++) {
    if (fade[i]) {
      nonzero_fade = true;
      break;
    }
  }
  if (!nonzero_fade) {
    return nullptr;
  }

  VulkanDraw* draw = &lev_bucket->envmap_draws[lev_bucket->next_free_envmap_draw++];
  draw->flags = 0;
  draw->first_index = mdraw.first_index;
  draw->index_count = mdraw.index_count;
  draw->mode = envmap_mode;
  if (jak1_water_mode) {
    draw->mode.set_ab(true);
    draw->mode.disable_depth_write();
  }
  draw->texture = envmap_texture;
  draw->first_bone = first_bone;
  draw->light_idx = lights;
  draw->num_triangles = mdraw.num_triangles;
  for (int i = 0; i < 4; i++) {
    draw->fade[i] = fade[i];
  }
  return draw;
}

MercVulkan2::VulkanDraw* MercVulkan2::alloc_normal_draw(const tfrag3::MercDraw& mdraw,
                                                bool ignore_alpha,
                                                LevelDrawBucketVulkan* lev_bucket,
                                                u32 first_bone,
                                                u32 lights,
                                                bool jak1_water_mode,
                                                bool disable_fog) {
  MercVulkan2::VulkanDraw* draw = &lev_bucket->draws[lev_bucket->next_free_draw++];
  draw->flags = 0;
  draw->first_index = mdraw.first_index;
  draw->index_count = mdraw.index_count;
  draw->mode = mdraw.mode;
  if (jak1_water_mode) {
    draw->mode.set_ab(true);
    draw->mode.disable_depth_write();
  }

  if (disable_fog) {
    draw->mode.set_fog(false);
    // but don't toggle it the other way?
  }

  draw->texture = mdraw.eye_id == 0xff ? mdraw.tree_tex_id : (0xffffff00 | mdraw.eye_id);
  draw->first_bone = first_bone;
  draw->light_idx = lights;
  draw->num_triangles = mdraw.num_triangles;
  if (ignore_alpha) {
    draw->flags |= IGNORE_ALPHA;
  }
  for (int i = 0; i < 4; i++) {
    draw->fade[i] = 0;
  }
  return draw;
}

void MercVulkan2::init_shaders() {
  auto& merc2_shader = m_vulkan_info.shaders[ShaderId::MERC2];
  auto& emerc_shader = m_vulkan_info.shaders[ShaderId::EMERC];

  VkPipelineShaderStageCreateInfo mercVertShaderStageInfo{};
  mercVertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  mercVertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  mercVertShaderStageInfo.module = merc2_shader.GetVertexShader();
  mercVertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo mercFragShaderStageInfo{};
  mercFragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  mercFragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  mercFragShaderStageInfo.module = merc2_shader.GetFragmentShader();
  mercFragShaderStageInfo.pName = "main";

  m_pipeline_config_info.shaderStages = {mercVertShaderStageInfo, mercFragShaderStageInfo};

  VkPipelineShaderStageCreateInfo emercVertShaderStageInfo{};
  emercVertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  emercVertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  emercVertShaderStageInfo.module = emerc_shader.GetVertexShader();
  emercVertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo emercFragShaderStageInfo{};
  emercFragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  emercFragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  emercFragShaderStageInfo.module = emerc_shader.GetFragmentShader();
  emercFragShaderStageInfo.pName = "main";

  m_emerc_pipeline_config_info.shaderStages = {mercVertShaderStageInfo, mercFragShaderStageInfo};
}

void MercVulkan2::InitializeInputAttributes() {
  VkVertexInputBindingDescription mercVertexBindingDescription{};
  mercVertexBindingDescription.binding = 0;
  mercVertexBindingDescription.stride = sizeof(tfrag3::MercVertex);
  mercVertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputBindingDescription mercMatrixBindingDescription{};
  mercMatrixBindingDescription.binding = 1;
  mercMatrixBindingDescription.stride = sizeof(ShaderMercMat);
  mercMatrixBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions = {mercVertexBindingDescription,
                                                mercMatrixBindingDescription};

  std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::MercVertex, pos);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::MercVertex, normal[0]);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(tfrag3::MercVertex, weights[0]);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[3].offset = offsetof(tfrag3::MercVertex, st[0]);

  // FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 4;
  attributeDescriptions[4].format = VK_FORMAT_R8G8B8A8_SNORM;
  attributeDescriptions[4].offset = offsetof(tfrag3::MercVertex, rgba[0]);

  attributeDescriptions[5].binding = 0;
  attributeDescriptions[5].location = 5;
  attributeDescriptions[5].format = VK_FORMAT_R8G8B8_UINT;
  attributeDescriptions[5].offset = offsetof(tfrag3::MercVertex, mats[0]);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_emerc_pipeline_config_info);
  m_emerc_pipeline_config_info.attributeDescriptions = m_pipeline_config_info.attributeDescriptions;
  m_emerc_pipeline_config_info.bindingDescriptions = m_pipeline_config_info.bindingDescriptions;
}

MercLightControlVertexUniformBuffer::MercLightControlVertexUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(MercLightControlUniformBufferVertexData),
                          instanceCount,
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"light_dir0", offsetof(MercLightControlUniformBufferVertexData, light_dir0)},
      {"light_dir1", offsetof(MercLightControlUniformBufferVertexData, light_dir1)},
      {"light_dir2", offsetof(MercLightControlUniformBufferVertexData, light_dir2)},
      {"light_col0", offsetof(MercLightControlUniformBufferVertexData, light_col0)},
      {"light_col1", offsetof(MercLightControlUniformBufferVertexData, light_col1)},
      {"light_col2", offsetof(MercLightControlUniformBufferVertexData, light_col2)},
      {"light_ambient", offsetof(MercLightControlUniformBufferVertexData, light_ambient)}};
}

MercVulkan2::MercBoneVertexUniformBuffer::MercBoneVertexUniformBuffer(
                                                     std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                     VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(BaseMerc2::ShaderMercMat),
                          128,
                          minOffsetAlignment) {
}


MercVulkan2::~MercVulkan2() {
}
