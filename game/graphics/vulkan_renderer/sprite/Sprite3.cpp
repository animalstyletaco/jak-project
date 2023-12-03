

#include "Sprite3.h"

#include "game/graphics/general_renderer/dma_helpers.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

SpriteVulkan3::SpriteVulkan3(const std::string& name,
                             int my_id,
                             std::shared_ptr<GraphicsDeviceVulkan> device,
                             VulkanInitializationInfo& graphics_info)
    : BaseSprite3(name, my_id),
      BucketVulkanRenderer(device, graphics_info),
      m_direct(name, my_id, device, graphics_info, 1024),
      m_glow_renderer(device, graphics_info),
      m_distort_sampler_helper(device) {
  m_sprite_3d_vertex_uniform_buffer = std::make_unique<Sprite3dVertexUniformBuffer>(m_device, 1);

  graphics_setup();
}

void SpriteVulkan3::graphics_setup() {
  // Set up OpenGL for 'normal' sprites
  graphics_setup_normal();

  // Set up OpenGL for distort sprites
  graphics_setup_distort();

  create_pipeline_layout();
}

void SpriteVulkan3::create_pipeline_layout() {
  // If push constants are needed put them here
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_vertex_descriptor_layout->getDescriptorSetLayout(),
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  std::array<VkPushConstantRange, 2> pushConstantRanges;
  pushConstantRanges[0].offset = 0;
  pushConstantRanges[0].size = sizeof(m_push_constant);
  pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pushConstantRanges[1].offset = sizeof(m_push_constant);
  pushConstantRanges[1].size = sizeof(m_sprite_fragment_push_constant);
  pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  std::vector<VkDescriptorSetLayout> spriteDistortDescriptorSetLayouts{
      m_sprite_distort_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo spriteDistortPipelineLayoutInfo{};
  spriteDistortPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  spriteDistortPipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(spriteDistortDescriptorSetLayouts.size());
  spriteDistortPipelineLayoutInfo.pSetLayouts = spriteDistortDescriptorSetLayouts.data();

  VkPushConstantRange spriteDistortPushConstantRange{};
  spriteDistortPushConstantRange.offset = 0;
  spriteDistortPushConstantRange.size = sizeof(m_sprite_distort_push_constant);
  spriteDistortPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  spriteDistortPipelineLayoutInfo.pPushConstantRanges = &spriteDistortPushConstantRange;
  spriteDistortPipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &spriteDistortPipelineLayoutInfo,
                             nullptr, &m_sprite_distort_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void SpriteVulkan3::SetupShader(ShaderId shaderId) {
  auto& shader = m_vulkan_info.shaders[shaderId];

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

  if (shaderId == ShaderId::SPRITE || shaderId == ShaderId::SPRITE3) {
    m_pipeline_config_info.pipelineLayout = m_pipeline_layout;
    m_pipeline_config_info.attributeDescriptions = m_sprite_attribute_descriptions;
    m_pipeline_config_info.bindingDescriptions = m_sprite_input_binding_descriptions;
  } else if (shaderId == ShaderId::SPRITE_DISTORT_INSTANCED) {
    m_pipeline_config_info.pipelineLayout = m_sprite_distort_pipeline_layout;
    m_pipeline_config_info.attributeDescriptions =
        m_sprite_distort_instanced_attribute_descriptions;
    m_pipeline_config_info.bindingDescriptions =
        m_sprite_distort_instanced_input_binding_descriptions;
  } else {
    m_pipeline_config_info.pipelineLayout = m_sprite_distort_pipeline_layout;
    m_pipeline_config_info.attributeDescriptions = m_sprite_distort_attribute_descriptions;
    m_pipeline_config_info.bindingDescriptions = m_sprite_distort_input_binding_descriptions;
  }
}

void SpriteVulkan3Jak1::render(DmaFollower& dma,
                               SharedVulkanRenderState* render_state,
                               ScopedProfilerNode& prof) {
  m_direct_renderer_call_count = 0;
  m_flush_sprite_call_count = 0;
  m_direct.set_current_index(m_direct_renderer_call_count++);
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseSprite3Jak1::render(dma, render_state, prof);
}

void SpriteVulkan3Jak2::render(DmaFollower& dma,
                               SharedVulkanRenderState* render_state,
                               ScopedProfilerNode& prof) {
  m_direct_renderer_call_count = 0;
  m_flush_sprite_call_count = 0;
  m_direct.set_current_index(m_direct_renderer_call_count++);
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseSprite3Jak2::render(dma, render_state, prof);
}

void SpriteVulkan3::graphics_setup_normal() {
  BaseSprite3::graphics_setup_normal();
  m_ogl.index_buffer =
      std::make_unique<IndexBuffer>(m_device, sizeof(u32), m_index_buffer_data.size(), 1);
  m_ogl.vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, m_vertices_3d.size(), sizeof(SpriteVertex3D), 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);
  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);

  m_vertex_buffer_descriptor_info = m_sprite_3d_vertex_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .build(m_vertex_descriptor_set);

  m_fragment_descriptor_writer->writeImage(
      0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  std::array<VkVertexInputBindingDescription, 1> bindingDescriptions{};
  bindingDescriptions[0].binding = 0;
  bindingDescriptions[0].stride = sizeof(SpriteVertex3D);
  bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  m_sprite_input_binding_descriptions.insert(m_sprite_input_binding_descriptions.end(),
                                             bindingDescriptions.begin(),
                                             bindingDescriptions.end());

  std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(SpriteVertex3D, xyz_sx);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(SpriteVertex3D, quat_sy);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(SpriteVertex3D, rgba);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R16G16_UINT;
  attributeDescriptions[3].offset = offsetof(SpriteVertex3D, flags_matrix);

  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 4;
  attributeDescriptions[4].format = VK_FORMAT_R16G16B16A16_UINT;
  attributeDescriptions[4].offset = offsetof(SpriteVertex3D, info);

  m_sprite_attribute_descriptions.insert(m_sprite_attribute_descriptions.end(),
                                         attributeDescriptions.begin(),
                                         attributeDescriptions.end());
}

void SpriteVulkan3::setup_graphics_for_2d_group_0_render() {
  // opengl sprite frame setup
  // auto shid = render_state->shaders[ShaderId::SPRITE3].id();
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("hvdf_offset", 1,
                                                               m_3d_matrix_data.hvdf_offset.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("pfog0", m_frame_data.pfog0);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("min_scale", m_frame_data.min_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("max_scale", m_frame_data.max_scale);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_min", m_frame_data.fog_min);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("fog_max", m_frame_data.fog_max);
  // glUniform1f(glGetUniformLocation(shid, "bonus"), m_frame_data.bonus);
  // glUniform4fv(glGetUniformLocation(shid, "hmge_scale"), 1, m_frame_data.hmge_scale.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("deg_to_rad", m_frame_data.deg_to_rad);
  m_sprite_3d_vertex_uniform_buffer->SetUniform1f("inv_area", m_frame_data.inv_area);
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "camera", 1, VK_FALSE, m_3d_matrix_data.camera.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xy_array", 8,
                                                               m_frame_data.xy_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("xyz_array", 4,
                                                               m_frame_data.xyz_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("st_array", 4,
                                                               m_frame_data.st_array[0].data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_x", 1,
                                                               m_frame_data.basis_x.data());
  m_sprite_3d_vertex_uniform_buffer->SetUniformVectorFourFloat("basis_y", 1,
                                                               m_frame_data.basis_y.data());

  m_sprite_3d_vertex_uniform_buffer->map();
  m_sprite_3d_vertex_uniform_buffer->flush();
  m_sprite_3d_vertex_uniform_buffer->unmap();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Render (for real)

void SpriteVulkan3::flush_sprites(BaseSharedRenderState* render_state,
                                  ScopedProfilerNode& prof,
                                  bool double_draw) {
  if (m_bucket_list.empty()) {
    return;
  }

  // TODO: see if there is an easier way to accomplish add/update existing sprite_graphics_settings
  // objects
  auto fragment_descriptor_layout = m_fragment_descriptor_layout->getDescriptorSetLayout();
  while (m_flush_sprite_call_count >= m_sprite_graphics_settings_map.size()) {
    m_sprite_graphics_settings_map.insert(
        {m_sprite_graphics_settings_map.size(),
         {m_vulkan_info.descriptor_pool, fragment_descriptor_layout, 0}});
    m_sprite_graphics_settings_map.at(m_sprite_graphics_settings_map.size() - 1)
        .Reinitialize(fragment_descriptor_layout, m_bucket_list.size());
  }

  if (m_sprite_graphics_settings_map.at(m_flush_sprite_call_count).descriptor_image_infos.size() <
      m_bucket_list.size()) {
    m_sprite_graphics_settings_map.at(m_flush_sprite_call_count)
        .Reinitialize(fragment_descriptor_layout, m_bucket_list.size());
  }

  // Enable prim restart, we need this to break up the triangle strips
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;
  m_pipeline_config_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();

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

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_push_constant), &m_push_constant);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffers[] = {m_ogl.vertex_buffer->getBuffer()};
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer, m_ogl.index_buffer->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  // now do draws!
  u32 index = 0;
  for (const auto bucket : m_bucket_list) {
    u32 tbp = bucket->key >> 32;
    DrawMode mode;
    mode.as_int() = bucket->key & 0xffffffff;

    VulkanTexture* tex = m_vulkan_info.texture_pool->lookup_vulkan_texture(tbp);

    if (!tex) {
      lg::warn("Failed to find texture at {}, using random (sprite)", tbp);
      tex = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
    }
    ASSERT(tex);

    auto& sprite_graphics_settings = m_sprite_graphics_settings_map.at(m_flush_sprite_call_count);
    VulkanSamplerHelper& sampler_helper = sprite_graphics_settings.sampler_helpers[index];

    auto settings = vulkan_background_common::setup_vulkan_from_draw_mode(
        mode, sampler_helper, m_pipeline_config_info, false);

    m_sprite_fragment_push_constant.alpha_min = (double_draw) ? settings.aref_first : 0.016;
    m_sprite_fragment_push_constant.alpha_max = 10.f;

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m_push_constant),
                       sizeof(m_sprite_fragment_push_constant), &m_sprite_fragment_push_constant);

    prof.add_draw_call();
    prof.add_tri(2 * (bucket->ids.size() / 5));

    m_graphics_pipeline_layout.updateGraphicsPipeline(m_vulkan_info.render_command_buffer,
                                                      m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    sprite_graphics_settings.descriptor_image_infos[index] = VkDescriptorImageInfo{
        sampler_helper.GetSampler(), tex->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
    write_descriptors_info[0] = m_fragment_descriptor_writer->writeImageDescriptorSet(
        0, &sprite_graphics_settings.descriptor_image_infos[index]);

    m_fragment_descriptor_writer->overwrite(
        sprite_graphics_settings.fragment_descriptor_sets[index]);
    std::vector<VkDescriptorSet> descriptorSets{
        m_vertex_descriptor_set, sprite_graphics_settings.fragment_descriptor_sets[index]};

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, descriptorSets.size(),
                            descriptorSets.data(), 0, NULL);

    vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, bucket->ids.size(), 1,
                     bucket->offset_in_idx_buffer, 0, 0);

    if (double_draw) {
      switch (settings.kind) {
        case DoubleDrawKind::NONE:
          break;
        case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
          prof.add_draw_call();
          prof.add_tri(2 * (bucket->ids.size() / 5));
          m_sprite_fragment_push_constant.alpha_min = -10.f;
          m_sprite_fragment_push_constant.alpha_max = settings.aref_second;

          vkCmdPushConstants(m_vulkan_info.render_command_buffer,
                             m_pipeline_config_info.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                             sizeof(m_push_constant), sizeof(m_sprite_fragment_push_constant),
                             &m_sprite_fragment_push_constant);

          // glDepthMask(GL_FALSE);
          // glDrawElements(GL_TRIANGLE_STRIP, bucket->ids.size(), GL_UNSIGNED_INT,
          //                (void*)(bucket->offset_in_idx_buffer * sizeof(u32)));
          vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, bucket->ids.size(), 1,
                           bucket->offset_in_idx_buffer, 0, 0);
          break;
        default:
          ASSERT(false);
      }
    }
    index++;
  }

  m_sprite_buckets.clear();
  m_bucket_list.clear();
  m_last_bucket_key = UINT64_MAX;
  m_last_bucket = nullptr;
  m_sprite_idx = 0;
  m_flush_sprite_call_count++;
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
  m_direct.set_current_index(m_direct_renderer_call_count++);
  m_direct.render_vif(vif0, vif1, data, size, render_state, prof);
}
void SpriteVulkan3::direct_renderer_flush_pending(BaseSharedRenderState* render_state,
                                                  ScopedProfilerNode& prof) {
  m_direct.set_current_index(m_direct_renderer_call_count++);
  m_direct.flush_pending(render_state, prof);
}

void SpriteVulkan3::SetSprite3UniformVertexFourFloatVector(const char* name,
                                                           u32 numberOfFloats,
                                                           float* data,
                                                           u32 flags) {
  m_sprite_3d_vertex_uniform_buffer->SetUniform4f(name, data[0], data[1], data[2], data[3], flags);
}

void SpriteVulkan3::SetSprite3UniformMatrixFourFloatVector(const char* name,
                                                           u32 numberOfMatrices,
                                                           bool isTransponsedMatrix,
                                                           float* data,
                                                           u32 flags) {
  m_sprite_3d_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      name, numberOfMatrices, isTransponsedMatrix, data, true);
}

void SpriteVulkan3::SetSprite3UniformVertexUserHvdfVector(const char* name,
                                                          u32 totalBytes,
                                                          float* data,
                                                          u32 flags) {
  u32 offset = m_sprite_3d_vertex_uniform_buffer->GetDeviceMemoryOffset(name);
  m_sprite_3d_vertex_uniform_buffer->SetDataInVkDeviceMemory(offset, (u8*)data, totalBytes, flags);
}

void SpriteVulkan3::EnableSprite3GraphicsBlending() {
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}

SpriteVulkan3::~SpriteVulkan3() {
  std::vector<VkDescriptorSet> vertex_descriptor_sets{m_vertex_descriptor_set};
  std::vector<VkDescriptorSet> m_sprite_distort_fragment_descriptor_sets{
      m_sprite_distort_fragment_descriptor_set};

  m_vulkan_info.descriptor_pool->freeDescriptors(vertex_descriptor_sets);
  m_vulkan_info.descriptor_pool->freeDescriptors(m_sprite_distort_fragment_descriptor_sets);
}

Sprite3dVertexUniformBuffer::Sprite3dVertexUniformBuffer(
    std::shared_ptr<GraphicsDeviceVulkan> device,
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
      {"st_array", offsetof(Sprite3dVertexUniformShaderData, st_array)}};
}

void SpriteVulkan3::glow_renderer_cancel_sprite() {
  m_glow_renderer.cancel_sprite();
}

SpriteGlowOutput* SpriteVulkan3::glow_renderer_alloc_sprite() {
  return m_glow_renderer.alloc_sprite();
}

void SpriteVulkan3::glow_renderer_flush(BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  m_glow_renderer.flush(render_state, prof);
}
