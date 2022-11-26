

#include "Sprite3.h"

#include "game/graphics/vulkan_renderer/background/background_common.h"
#include "game/graphics/general_renderer/dma_helpers.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

SpriteVulkan3::SpriteVulkan3(const std::string& name,
                 int my_id,
                 std::unique_ptr<GraphicsDeviceVulkan>& device,
                 VulkanInitializationInfo& graphics_info)
    : BaseSprite3(name, my_id), BucketVulkanRenderer(device, graphics_info), m_direct(name, my_id, device, graphics_info, 1024),
  m_distorted_pipeline_layout(device), m_distorted_instance_pipeline_layout(device) {
  m_sprite_3d_vertex_uniform_buffer = std::make_unique<Sprite3dVertexUniformBuffer>(
      m_device, 1);

  m_sprite_3d_fragment_uniform_buffer = std::make_unique<Sprite3dFragmentUniformBuffer>(
    m_device, 1);

  m_sprite_3d_instanced_fragment_uniform_buffer =
      std::make_unique<SpriteDistortInstancedFragmentUniformBuffer>(m_device, 1);

  graphics_setup();
}

void SpriteVulkan3::graphics_setup() {
  // Set up OpenGL for 'normal' sprites
  graphics_setup_normal();

  // Set up OpenGL for distort sprites
  graphics_setup_distort();
}

void SpriteVulkan3::SetupShader(ShaderId shaderId) {
  auto& shader = m_vulkan_info.shaders[shaderId];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Sprite 3 Vertex";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Sprite 3 Fragment";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

 void SpriteVulkan3::render(DmaFollower& dma,
                     SharedVulkanRenderState* render_state,
                     ScopedProfilerNode& prof) {
  BaseSprite3::render(dma, render_state, prof);
 }


void SpriteVulkan3::graphics_setup_normal() {
  auto verts = SPRITE_RENDERER_MAX_SPRITES * 4;
  auto bytes = verts * sizeof(SpriteVertex3D);

  VkDeviceSize index_device_size = SPRITE_RENDERER_MAX_SPRITES * 5 * sizeof(u32);
  
  m_vertices_3d.resize(verts);
  m_index_buffer_data.resize(index_device_size);

  m_ogl.index_buffer = std::make_unique<IndexBuffer>(m_device, index_device_size, 1);

  m_vertices_3d.resize(verts);

  VkDeviceSize vertex_device_size = bytes;
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(m_device, vertex_device_size, 1);

  m_default_mode.disable_depth_write();
  m_default_mode.set_depth_test(GsTest::ZTest::GEQUAL);
  m_default_mode.set_alpha_blend(DrawMode::AlphaBlend::SRC_DST_SRC_DST);
  m_default_mode.set_aref(38);
  m_default_mode.set_alpha_test(DrawMode::AlphaTest::GEQUAL);
  m_default_mode.set_alpha_fail(GsTest::AlphaFail::FB_ONLY);
  m_default_mode.set_at(true);
  m_default_mode.set_zt(true);
  m_default_mode.set_ab(true);

  m_current_mode = m_default_mode;

  m_distort_ogl.fbo = std::make_unique<VulkanTexture>(m_device);
  m_distort_ogl.fbo_texture = std::make_unique<VulkanTexture>(m_device);
}

void SpriteVulkan3::graphics_setup_distort() {
  // Create framebuffer to snapshot current render to a texture that can be bound for the distort
  // shader This will represent tex0 from the original GS data
  VkExtent3D extents{m_distort_ogl.fbo_width, m_distort_ogl.fbo_height, 1};
  m_distort_ogl.fbo_texture->createImage(
    extents, 1, VK_IMAGE_TYPE_2D, m_device->getMsaaCount(), 
    VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

  m_distort_ogl.fbo_texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  // samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;

  // Non-instancing
  // ----------------------
  // note: each sprite shares a single vertex per slice, account for that here

  int distort_vert_buffer_len =
      SPRITE_RENDERER_MAX_DISTORT_SPRITES *
      ((5 - 1) * 11 + 1);  // max * ((verts_per_slice - 1) * max_slices + 1)
  VkDeviceSize vertex_device_size = distort_vert_buffer_len * sizeof(SpriteDistortVertex);
  m_distort_ogl.vertex_buffer = std::make_unique<VertexBuffer>(m_device, vertex_device_size, 1, 1 );

  // note: add one extra element per sprite that marks the end of a triangle strip
  int distort_idx_buffer_len = SPRITE_RENDERER_MAX_DISTORT_SPRITES *
                               ((5 * 11) + 1);  // max * ((verts_per_slice * max_slices) + 1)

  VkDeviceSize index_device_size = distort_idx_buffer_len * sizeof(u32);
  m_distort_ogl.index_buffer = std::make_unique<IndexBuffer>(
      m_device, index_device_size, 1, 1);

  m_sprite_distorter_vertices.resize(distort_vert_buffer_len);
  m_sprite_distorter_indices.resize(distort_idx_buffer_len);
  m_sprite_distorter_frame_data.resize(SPRITE_RENDERER_MAX_DISTORT_SPRITES);

  // Instancing
  // ----------------------
  int distort_max_sprite_slices = 0;
  for (int i = 3; i < 12; i++) {
    // For each 'resolution', there can be that many slices
    distort_max_sprite_slices += i;
  }

  std::array<VkVertexInputBindingDescription, 2> bindingDescriptions{};
  bindingDescriptions[0].binding = 0;
  bindingDescriptions[0].stride = sizeof(SpriteDistortVertex);
  bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  bindingDescriptions[1].binding = 1;
  bindingDescriptions[1].stride = sizeof(SpriteDistortInstanceData);
  bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
  m_pipeline_config_info.bindingDescriptions.insert(
      m_pipeline_config_info.bindingDescriptions.end(), bindingDescriptions.begin(), bindingDescriptions.end());

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(SpriteDistortVertex, xyz);
  
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(SpriteDistortVertex, st);
  
  attributeDescriptions[2].binding = 1;
  attributeDescriptions[2].location = 0;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(SpriteDistortInstanceData, x_y_z_s);
  
  attributeDescriptions[3].binding = 1;
  attributeDescriptions[3].location = 1;
  attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[3].offset = offsetof(SpriteDistortInstanceData, sx_sy_sz_t);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());

  VkDeviceSize instanced_vertex_device_size = distort_max_sprite_slices * 5 * sizeof(SpriteDistortVertex);
  m_distort_instanced_ogl.vertex_buffer = std::make_unique<VertexBuffer>(m_device, instanced_vertex_device_size, 1,
                                                                     1);

  VkDeviceSize instanced_device_size = SPRITE_RENDERER_MAX_DISTORT_SPRITES * sizeof(SpriteDistortInstanceData);
  m_distort_instanced_ogl.instance_buffer = std::make_unique<VertexBuffer>(
      m_device, instanced_device_size, 1, 1);

  m_sprite_distorter_vertices_instanced.resize(instanced_device_size);


  for (int i = 3; i < 12; i++) {
    auto vec = std::vector<SpriteDistortInstanceData>();
    vec.resize(SPRITE_RENDERER_MAX_DISTORT_SPRITES);

    m_sprite_distorter_instances_by_res[i] = vec;
  }
}

/*!
 * Draws each distort sprite.
 */
void SpriteVulkan3::distort_draw(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  // First, make sure the distort framebuffer is the correct size
  distort_setup_framebuffer_dims(render_state);

  if (m_distort_stats.total_tris == 0) {
    // No distort sprites to draw, we can end early
    return;
  }

  // Do common distort drawing logic
  distort_draw_common(render_state, prof);

  // Set up shader
  SetupShader(ShaderId::SPRITE_DISTORT);

  Vector4f colorf = Vector4f(m_sprite_distorter_sine_tables.color.x() / 255.0f,
                             m_sprite_distorter_sine_tables.color.y() / 255.0f,
                             m_sprite_distorter_sine_tables.color.z() / 255.0f,
                             m_sprite_distorter_sine_tables.color.w() / 255.0f);
  m_sprite_3d_instanced_fragment_uniform_buffer->SetUniformVectorFourFloat("u_color", 1,
                                                                           colorf.data());

  // Enable prim restart, we need this to break up the triangle strips
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  // Upload vertex data
  m_distort_ogl.vertex_buffer->writeToGpuBuffer(
      m_sprite_distorter_vertices.data(),
      m_sprite_distorter_vertices.size() * sizeof(SpriteDistortInstanceData), 0);

  // Upload element data
  m_distort_ogl.vertex_buffer->writeToGpuBuffer(
      m_sprite_distorter_indices.data(),
      m_sprite_distorter_indices.size() * sizeof(SpriteDistortInstanceData), 0);

  // Draw
  prof.add_draw_call();
  prof.add_tri(m_distort_stats.total_tris);

  //glDrawElements(GL_TRIANGLE_STRIP, m_sprite_distorter_indices.size(), GL_UNSIGNED_INT, (void*)0);

  // Done
}

/*!
 * Draws each distort sprite using instanced rendering.
 */
void SpriteVulkan3::distort_draw_instanced(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  // First, make sure the distort framebuffer is the correct size
  distort_setup_framebuffer_dims(render_state);

  if (m_distort_stats.total_tris == 0) {
    // No distort sprites to draw, we can end early
    return;
  }

  // Do common distort drawing logic
  distort_draw_common(render_state, prof);

  // Set up shader
  SetupShader(ShaderId::SPRITE_DISTORT_INSTANCED);

  Vector4f colorf = Vector4f(m_sprite_distorter_sine_tables.color.x() / 255.0f,
                             m_sprite_distorter_sine_tables.color.y() / 255.0f,
                             m_sprite_distorter_sine_tables.color.z() / 255.0f,
                             m_sprite_distorter_sine_tables.color.w() / 255.0f);
  m_sprite_3d_instanced_fragment_uniform_buffer->SetUniformVectorFourFloat("u_color", 1,
                                                                          colorf.data());

  // Upload vertex data (if it changed)
  if (m_distort_instanced_ogl.vertex_data_changed) {
    m_distort_instanced_ogl.vertex_data_changed = false;

    m_distort_instanced_ogl.vertex_buffer->writeToGpuBuffer(
        m_sprite_distorter_vertices_instanced.data(),
        m_sprite_distorter_vertices_instanced.size() * sizeof(SpriteDistortVertex));
  }

  // Draw each resolution group
  //glBindBuffer(GL_ARRAY_BUFFER, m_distort_instanced_ogl.instance_buffer);
  prof.add_tri(m_distort_stats.total_tris);

  int vert_offset = 0;
  for (int res = 3; res < 12; res++) {
    auto& instances = m_sprite_distorter_instances_by_res[res];
    int num_verts = res * 5;

    if (instances.size() > 0) {
      // Upload instance data
      m_distort_instanced_ogl.instance_buffer->writeToGpuBuffer(instances.data(),
          instances.size() * sizeof(SpriteDistortInstanceData));

      // Draw
      prof.add_draw_call();

      //glDrawArraysInstanced(GL_TRIANGLE_STRIP, vert_offset, num_verts, instances.size());
    }

    vert_offset += num_verts;
  }
}

void SpriteVulkan3::distort_draw_common(SharedVulkanRenderState* render_state, ScopedProfilerNode& /*prof*/) {
  // The distort effect needs to read the current framebuffer, so copy what's been rendered so far
  // to a texture that we can then pass to the shader
 
  //glBindFramebuffer(GL_READ_FRAMEBUFFER, render_state->render_fb);
  //glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_distort_ogl.fbo);
  //
  //glBlitFramebuffer(render_state->render_fb_x,                              // srcX0
  //                  render_state->render_fb_y,                              // srcY0
  //                  render_state->render_fb_x + render_state->render_fb_w,  // srcX1
  //                  render_state->render_fb_y + render_state->render_fb_h,  // srcY1
  //                  0,                                                      // dstX0
  //                  0,                                                      // dstY0
  //                  m_distort_ogl.fbo_width,                                // dstX1
  //                  m_distort_ogl.fbo_height,                               // dstY1
  //                  GL_COLOR_BUFFER_BIT,                                    // mask
  //                  GL_NEAREST                                              // filter
  //);
  //
  //glBindFramebuffer(GL_FRAMEBUFFER, render_state->render_fb);

  // Set up OpenGL state
  m_current_mode.set_depth_write_enable(!m_sprite_distorter_setup.zbuf.zmsk());  // zbuf
  //glBindTexture(GL_TEXTURE_2D, m_distort_ogl.fbo_texture);                       // tex0
  m_current_mode.set_filt_enable(m_sprite_distorter_setup.tex1.mmag());          // tex1
  update_mode_from_alpha1(m_sprite_distorter_setup.alpha.data, m_current_mode);  // alpha1
  // note: clamp and miptbp are skipped since that is set up ahead of time with the distort
  // framebuffer texture

  m_current_mode.set_depth_write_enable(!m_sprite_distorter_setup.zbuf.zmsk());  // zbuf
  m_current_mode.set_filt_enable(m_sprite_distorter_setup.tex1.mmag());          // tex1
  update_mode_from_alpha1(
      m_sprite_distorter_setup.alpha.data,
      m_current_mode);  // alpha1
  background_common::setup_vulkan_from_draw_mode(m_current_mode, m_distort_ogl.fbo_texture.get(), m_pipeline_config_info, false);
}

void SpriteVulkan3::distort_setup_framebuffer_dims(SharedVulkanRenderState* render_state) {
  // Distort framebuffer must be the same dimensions as the default window framebuffer
  if (m_distort_ogl.fbo_width != render_state->render_fb_w ||
      m_distort_ogl.fbo_height != render_state->render_fb_h) {
    m_distort_ogl.fbo_width = render_state->render_fb_w;
    m_distort_ogl.fbo_height = render_state->render_fb_h;

  //TODO: Move this logic to it's own helper function
    VkExtent3D extents{m_distort_ogl.fbo_width, m_distort_ogl.fbo_height, 1};
    m_distort_ogl.fbo_texture->createImage(
        extents, 1, VK_IMAGE_TYPE_2D, m_device->getMsaaCount(),
        VK_FORMAT_R8G8B8_USCALED, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

  m_distort_ogl.fbo_texture->createImageView(
      VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}

void SpriteVulkan3::render_2d_group0(DmaFollower& dma,
                               SharedVulkanRenderState* render_state,
                               ScopedProfilerNode& prof) {
  // opengl sprite frame setup
  //auto shid = render_state->shaders[ShaderId::SPRITE3].id();
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("hvdf_offset", 1, m_3d_matrix_data.hvdf_offset.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("pfog0", m_frame_data.pfog0);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("min_scale", m_frame_data.min_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("max_scale", m_frame_data.max_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_min", m_frame_data.fog_min);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_max", m_frame_data.fog_max);
  // glUniform1f(glGetUniformLocation(shid, "bonus"), m_frame_data.bonus);
  // glUniform4fv(glGetUniformLocation(shid, "hmge_scale"), 1, m_frame_data.hmge_scale.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("deg_to_rad", m_frame_data.deg_to_rad);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("inv_area", m_frame_data.inv_area);
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory("camera", 1, GL_FALSE,
                                                                      m_3d_matrix_data.camera.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xy_array", 8, m_frame_data.xy_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xyz_array", 4, m_frame_data.xyz_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("st_array", 4, m_frame_data.st_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_x", 1, m_frame_data.basis_x.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_y", 1, m_frame_data.basis_y.data());

  u16 last_prog = -1;

  while (sprite_common::looks_like_2d_chunk_start(dma)) {
    m_debug_stats.blocks_2d_grp0++;
    // 4 packets per chunk

    // first is the header
    u32 sprite_count = sprite_common::process_sprite_chunk_header(dma);
    m_debug_stats.count_2d_grp0 += sprite_count;

    // second is the vector data
    u32 expected_vec_size = sizeof(SpriteVecData2d) * sprite_count;
    auto vec_data = dma.read_and_advance();
    ASSERT(expected_vec_size <= sizeof(m_vec_data_2d));
    unpack_to_no_stcycl(&m_vec_data_2d, vec_data, VifCode::Kind::UNPACK_V4_32, expected_vec_size,
                        SpriteDataMem::Vector, false, true);

    // third is the adgif data
    u32 expected_adgif_size = sizeof(AdGifData) * sprite_count;
    auto adgif_data = dma.read_and_advance();
    ASSERT(expected_adgif_size <= sizeof(m_adgif));
    unpack_to_no_stcycl(&m_adgif, adgif_data, VifCode::Kind::UNPACK_V4_32, expected_adgif_size,
                        SpriteDataMem::Adgif, false, true);

    // fourth is the actual run!!!!!
    auto run = dma.read_and_advance();
    ASSERT(run.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(run.vifcode1().kind == VifCode::Kind::MSCAL);

    if (m_enabled) {
      if (run.vifcode1().immediate != last_prog) {
        // one-time setups and flushing
        flush_sprites(render_state, prof, false);
      }

      if (run.vifcode1().immediate == SpriteProgMem::Sprites2dGrp0) {
        if (m_2d_enable) {
          do_block_common(SpriteMode::Mode2D, sprite_count, render_state, prof);
        }
      } else {
        if (m_3d_enable) {
          do_block_common(SpriteMode::Mode3D, sprite_count, render_state, prof);
        }
      }
      last_prog = run.vifcode1().immediate;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Render (for real)

void SpriteVulkan3::flush_sprites(SharedVulkanRenderState* render_state,
                            ScopedProfilerNode& prof,
                            bool double_draw) {
  // Enable prim restart, we need this to break up the triangle strips
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  // upload vertex buffer
  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices_3d.data());

  // two passes through the buckets. first to build the index buffer
  u32 idx_offset = 0;
  for (const auto bucket : m_bucket_list) {
    memcpy(&m_index_buffer_data[idx_offset], bucket->ids.data(), bucket->ids.size() * sizeof(u32));
    bucket->offset_in_idx_buffer = idx_offset;
    idx_offset += bucket->ids.size();
  }

  // now upload it
  m_ogl.index_buffer->writeToGpuBuffer(m_index_buffer_data.data());

  // now do draws!
  for (const auto bucket : m_bucket_list) {
    u32 tbp = bucket->key >> 32;
    DrawMode mode;
    mode.as_int() = bucket->key & 0xffffffff;

    VulkanTexture* tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp);

    if (!tex) {
      fmt::print("Failed to find texture at {}, using random\n", tbp);
      tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
    }
    ASSERT(tex);

    auto settings = background_common::setup_vulkan_from_draw_mode(mode, tex, m_pipeline_config_info, false);

    m_sprite_3d_fragment_uniform_buffer->SetUniform1f("alpha_min", double_draw ? settings.aref_first : 0.016);
    m_sprite_3d_fragment_uniform_buffer->SetUniform1f("alpha_max", 10.f);

    prof.add_draw_call();
    prof.add_tri(2 * (bucket->ids.size() / 5));

    //glDrawElements(GL_TRIANGLE_STRIP, bucket->ids.size(), GL_UNSIGNED_INT,
    //               (void*)(bucket->offset_in_idx_buffer * sizeof(u32)));

    if (double_draw) {
      switch (settings.kind) {
        case DoubleDrawKind::NONE:
          break;
        case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
          prof.add_draw_call();
          prof.add_tri(2 * (bucket->ids.size() / 5));
          m_sprite_3d_instanced_fragment_uniform_buffer->SetUniform1f("alpha_min", -10.f);
          m_sprite_3d_instanced_fragment_uniform_buffer->SetUniform1f("alpha_max",
                                                                     settings.aref_second);
          //glDepthMask(GL_FALSE);
          //glDrawElements(GL_TRIANGLE_STRIP, bucket->ids.size(), GL_UNSIGNED_INT,
          //               (void*)(bucket->offset_in_idx_buffer * sizeof(u32)));
          break;
        default:
          ASSERT(false);
      }
    }
  }

  m_sprite_buckets.clear();
  m_bucket_list.clear();
  m_last_bucket_key = UINT64_MAX;
  m_last_bucket = nullptr;
  m_sprite_idx = 0;
}

void SpriteVulkan3::direct_renderer_reset_state() {
  m_direct.reset_state();
}

void SpriteVulkan3::direct_renderer_render_vif(u32 vif0,
                                         u32 vif1,
                                         const u8* data,
                                         u32 size,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  m_direct.render_vif(vif0, vif1, data, size, render_state, prof);
}
void SpriteVulkan3::direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                            ScopedProfilerNode& prof) {
  //m_direct.flush_pending(render_state, prof);
}
void SpriteVulkan3::SetSprite3UniformVertexFourFloatVector(const char* name,
                                                     u32 numberOfFloats,
                                                     float* data,
                                                     u32 flags) {
  m_sprite_3d_vertex_uniform_buffer->SetUniform4f(name, data[0], data[1], data[2], data[3], flags);
}
void SpriteVulkan3::SetSprite3UniformMatrixFourFloatVector(const char* name,
                                                     u32 numberOfFloats,
                                                     bool isTransponsedMatrix,
                                                     float* data,
                                                     u32 flags) {
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(name, numberOfFloats, isTransponsedMatrix, data, true);
}

void SpriteVulkan3::EnableSprite3GraphicsBlending() {
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}


Sprite3dVertexUniformBuffer::Sprite3dVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                        VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device, sizeof(Sprite3dVertexUniformShaderData), 1, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"hvdf_offset", offsetof(Sprite3dVertexUniformShaderData, hvdf_offset)},
      {"camera", offsetof(Sprite3dVertexUniformShaderData, camera)},
      {"hud_matrix", offsetof(Sprite3dVertexUniformShaderData, hud_matrix)},
      {"hud_hvdf_offset", offsetof(Sprite3dVertexUniformShaderData, hud_hvdf_offset)},
      {"hud_hvdf_user", offsetof(Sprite3dVertexUniformShaderData, hud_hvdf_user)},
      {"pfog0", offsetof(Sprite3dVertexUniformShaderData, pfog0)},
      {"fog_min", offsetof(Sprite3dVertexUniformShaderData, fog_min)},
      {"fog_max", offsetof(Sprite3dVertexUniformShaderData, fog_max)},
      {"min_scale", offsetof(Sprite3dVertexUniformShaderData, min_scale)},
      {"max_scale", offsetof(Sprite3dVertexUniformShaderData, max_scale)},
      {"deg_to_rad", offsetof(Sprite3dVertexUniformShaderData, deg_to_rad)},
      {"inv_area", offsetof(Sprite3dVertexUniformShaderData, inv_area)},
      {"basis_x", offsetof(Sprite3dVertexUniformShaderData, basis_x)},
      {"basis_y", offsetof(Sprite3dVertexUniformShaderData, basis_y)},
      {"xy_array", offsetof(Sprite3dVertexUniformShaderData, xy_array)},
      {"xyz_array", offsetof(Sprite3dVertexUniformShaderData, xyz_array)},
      {"st_array", offsetof(Sprite3dVertexUniformShaderData, st_array)},
      {"height_scale", offsetof(Sprite3dVertexUniformShaderData, height_scale)},
  };
}

Sprite3dFragmentUniformBuffer::Sprite3dFragmentUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(Sprite3dFragmentUniformShaderData),
                          1,
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"alpha_min", offsetof(Sprite3dFragmentUniformShaderData, alpha_min)},
      {"alpha_max", offsetof(Sprite3dFragmentUniformShaderData, alpha_max)},
  };
}

SpriteDistortInstancedFragmentUniformBuffer::SpriteDistortInstancedFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                              VkDeviceSize minOffsetAlignment)
      : UniformVulkanBuffer(device, sizeof(math::Vector4f), 1, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"u_color", 0}
  };
}
