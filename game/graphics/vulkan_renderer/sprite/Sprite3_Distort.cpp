#include "Sprite3.h"
#include "game/graphics/general_renderer/dma_helpers.h"

void SpriteVulkan3::graphics_setup_distort() {
  m_sprite_distort_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  // Create framebuffer to snapshot current render to a texture that can be bound for the distort
  // shader This will represent tex0 from the original GS data
  VkExtent3D extents{m_distort_ogl.fbo_width, m_distort_ogl.fbo_height, 1};
  m_distort_ogl.fbo_texture->createImage(extents, 1, VK_IMAGE_TYPE_2D, m_device->getMsaaCount(),
                                         VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

  m_distort_ogl.fbo_texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                             VK_IMAGE_ASPECT_COLOR_BIT, 1);

  VkSamplerCreateInfo& samplerInfo = m_distort_sampler_helper.GetSamplerCreateInfo();
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
  m_distort_ogl.vertex_buffer = std::make_unique<VertexBuffer>(m_device, vertex_device_size, 1, 1);

  // note: add one extra element per sprite that marks the end of a triangle strip
  int distort_idx_buffer_len = SPRITE_RENDERER_MAX_DISTORT_SPRITES *
                               ((5 * 11) + 1);  // max * ((verts_per_slice * max_slices) + 1)

  VkDeviceSize index_device_size = distort_idx_buffer_len * sizeof(u32);
  m_distort_ogl.index_buffer = std::make_unique<IndexBuffer>(m_device, index_device_size, 1, 1);

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
  m_sprite_distort_input_binding_descriptions.insert(
      m_sprite_distort_input_binding_descriptions.end(), bindingDescriptions.begin(),
      bindingDescriptions.end());

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
  m_sprite_distort_attribute_descriptions.insert(m_sprite_distort_attribute_descriptions.end(),
                                                 attributeDescriptions.begin(),
                                                 attributeDescriptions.end());

  VkDeviceSize instanced_vertex_device_size =
      distort_max_sprite_slices * 5 * sizeof(SpriteDistortVertex);
  m_vulkan_distort_instanced_ogl.vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, instanced_vertex_device_size, 1, 1);

  VkDeviceSize instanced_device_size =
      SPRITE_RENDERER_MAX_DISTORT_SPRITES * sizeof(SpriteDistortInstanceData);
  m_vulkan_distort_instanced_ogl.instance_buffer =
      std::make_unique<VertexBuffer>(m_device, instanced_device_size, 1, 1);

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
void SpriteVulkan3::distort_draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
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

  m_sprite_distort_push_constant = Vector4f(m_sprite_distorter_sine_tables.color.x() / 255.0f,
                             m_sprite_distorter_sine_tables.color.y() / 255.0f,
                             m_sprite_distorter_sine_tables.color.z() / 255.0f,
                             m_sprite_distorter_sine_tables.color.w() / 255.0f);

  // Enable prim restart, we need this to break up the triangle strips
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  // Upload vertex data
  m_distort_ogl.vertex_buffer->writeToGpuBuffer(
      m_sprite_distorter_vertices.data(),
      m_sprite_distorter_vertices.size() * sizeof(SpriteDistortInstanceData), 0);

  // Upload element data
  m_distort_ogl.index_buffer->writeToGpuBuffer(
      m_sprite_distorter_indices.data(),
      m_sprite_distorter_indices.size() * sizeof(SpriteDistortInstanceData), 0);

  // Draw
  prof.add_draw_call();
  prof.add_tri(m_distort_stats.total_tris);

  m_pipeline_config_info.pipelineLayout = m_sprite_distort_pipeline_layout;

  m_distorted_pipeline_layout.createGraphicsPipeline(m_pipeline_config_info);
  m_distorted_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffers[] = {m_distort_ogl.vertex_buffer->getBuffer()};
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, vertex_buffers, offsets);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                       m_distort_ogl.index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[0] =
      m_fragment_descriptor_writer->writeImageDescriptorSet(0, &m_sprite_distort_descriptor_image_info);

  m_fragment_descriptor_writer->overwrite(m_sprite_distort_fragment_descriptor_set);
  std::vector<VkDescriptorSet> descriptorSets{m_vertex_descriptor_set,
                                              m_sprite_distort_fragment_descriptor_set};

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, descriptorSets.size(),
                          descriptorSets.data(), 0,
                          NULL);

  // glDrawElements(GL_TRIANGLE_STRIP, m_sprite_distorter_indices.size(), GL_UNSIGNED_INT,
  // (void*)0);
  vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_sprite_distorter_indices.size(), 1, 0, 0, 0);

  // Done
}

/*!
 * Draws each distort sprite using instanced rendering.
 */
void SpriteVulkan3::distort_draw_instanced(BaseSharedRenderState* render_state,
                                           ScopedProfilerNode& prof) {
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

  m_sprite_distort_push_constant = Vector4f(m_sprite_distorter_sine_tables.color.x() / 255.0f,
                             m_sprite_distorter_sine_tables.color.y() / 255.0f,
                             m_sprite_distorter_sine_tables.color.z() / 255.0f,
                             m_sprite_distorter_sine_tables.color.w() / 255.0f);

  // Upload vertex data (if it changed)
  if (m_distort_instanced_ogl.vertex_data_changed) {
    m_distort_instanced_ogl.vertex_data_changed = false;

    m_vulkan_distort_instanced_ogl.vertex_buffer->writeToGpuBuffer(
        m_sprite_distorter_vertices_instanced.data(),
        m_sprite_distorter_vertices_instanced.size() * sizeof(SpriteDistortVertex));
  }

  // Draw each resolution group
  // glBindBuffer(GL_ARRAY_BUFFER, m_distort_instanced_ogl.instance_buffer);
  prof.add_tri(m_distort_stats.total_tris);

  int vert_offset = 0;
  for (int res = 3; res < 12; res++) {
    auto& instances = m_sprite_distorter_instances_by_res[res];
    int num_verts = res * 5;

    if (instances.size() > 0) {
      // Upload instance data
      m_vulkan_distort_instanced_ogl.instance_buffer->writeToGpuBuffer(
          instances.data(), instances.size() * sizeof(SpriteDistortInstanceData));

      // Draw
      prof.add_draw_call();

      // glDrawArraysInstanced(GL_TRIANGLE_STRIP, vert_offset, num_verts, instances.size());
      vkCmdDraw(m_vulkan_info.render_command_buffer, num_verts, instances.size(), vert_offset, 0);
    }

    vert_offset += num_verts;
  }
}

void SpriteVulkan3::distort_draw_common(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& /*prof*/) {
  // The distort effect needs to read the current framebuffer, so copy what's been rendered so far
  // to a texture that we can then pass to the shader

  std::array<VkImageBlit, 1> imageBlits{};
  imageBlits[0].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBlits[0].srcSubresource.mipLevel = 0;
  imageBlits[0].srcSubresource.baseArrayLayer = 1;
  imageBlits[0].srcSubresource.layerCount = 1;

  imageBlits[0].srcOffsets[0].x = render_state->render_fb_x;
  imageBlits[0].srcOffsets[0].y = render_state->render_fb_y;
  imageBlits[0].srcOffsets[0].z = 0;
  imageBlits[0].srcOffsets[1].x = render_state->render_fb_x + render_state->render_fb_w;
  imageBlits[0].srcOffsets[1].y = render_state->render_fb_y + render_state->render_fb_h;
  imageBlits[0].srcOffsets[1].z = 0;

  imageBlits[0].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBlits[0].dstSubresource.mipLevel = 0;
  imageBlits[0].dstSubresource.baseArrayLayer = 1;
  imageBlits[0].dstSubresource.layerCount = 1;

  imageBlits[0].dstOffsets[0].x = 0;
  imageBlits[0].dstOffsets[0].y = 0;
  imageBlits[0].dstOffsets[0].z = 0;
  imageBlits[0].dstOffsets[1].x = m_distort_ogl.fbo_width;
  imageBlits[0].dstOffsets[1].y = m_distort_ogl.fbo_width;
  imageBlits[0].dstOffsets[1].z = 0;

  VulkanTexture& color_framebuffer = m_vulkan_info.swap_chain->GetColorAttachmentImagesAtIndex(m_vulkan_info.currentFrame);
  vkCmdBlitImage(m_vulkan_info.render_command_buffer, color_framebuffer.getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
                 m_distort_ogl.fbo->color_texture.getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, imageBlits.size(), imageBlits.data(), VK_FILTER_NEAREST);

  // Set up OpenGL state
  m_current_mode.set_depth_write_enable(!m_sprite_distorter_setup.zbuf.zmsk());  // zbuf
  // glBindTexture(GL_TEXTURE_2D, m_distort_ogl.fbo_texture);                       // tex0
  m_current_mode.set_filt_enable(m_sprite_distorter_setup.tex1.mmag());          // tex1
  update_mode_from_alpha1(m_sprite_distorter_setup.alpha.data, m_current_mode);  // alpha1
  // note: clamp and miptbp are skipped since that is set up ahead of time with the distort
  // framebuffer texture

  m_current_mode.set_depth_write_enable(!m_sprite_distorter_setup.zbuf.zmsk());  // zbuf
  m_current_mode.set_filt_enable(m_sprite_distorter_setup.tex1.mmag());          // tex1
  update_mode_from_alpha1(m_sprite_distorter_setup.alpha.data,
                          m_current_mode);  // alpha1
  vulkan_background_common::setup_vulkan_from_draw_mode(
      m_current_mode, m_distort_sampler_helper,
      m_pipeline_config_info, false);
}

void SpriteVulkan3::distort_setup_framebuffer_dims(BaseSharedRenderState* render_state) {
  // Distort framebuffer must be the same dimensions as the default window framebuffer
  if (m_distort_ogl.fbo_width != render_state->render_fb_w ||
      m_distort_ogl.fbo_height != render_state->render_fb_h) {
    m_distort_ogl.fbo_width = render_state->render_fb_w;
    m_distort_ogl.fbo_height = render_state->render_fb_h;

    // TODO: Move this logic to it's own helper function
    VkExtent3D extents{m_distort_ogl.fbo_width, m_distort_ogl.fbo_height, 1};
    m_distort_ogl.fbo_texture->createImage(extents, 1, VK_IMAGE_TYPE_2D, m_device->getMsaaCount(),
                                           VK_FORMAT_R8G8B8_USCALED, VK_IMAGE_TILING_LINEAR,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    m_distort_ogl.fbo_texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8_UNORM,
                                               VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }
}
