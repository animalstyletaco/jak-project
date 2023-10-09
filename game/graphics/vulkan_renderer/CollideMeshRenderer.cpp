#include "CollideMeshRenderer.h"

#include "game/graphics/display.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

CollideMeshVulkanRenderer::CollideMeshVulkanRenderer(std::shared_ptr<GraphicsDeviceVulkan> device, VulkanInitializationInfo& vulkan_info) :
      m_device(device),
      m_collision_mesh_vertex_uniform_buffer(device, 1, 1),
      m_pipeline_layout{device},
      m_vulkan_info{vulkan_info} {

  m_push_constant.scissor_adjust =
      (m_vulkan_info.m_version == GameVersion::Jak1) ? (-512.f / 448.f) : (-512.f / 416.f);

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  InitializeInputVertexAttribute();
  init_shaders();

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  create_pipeline_layout();
  m_vertex_descriptor_writer = std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout,
                                                                  m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(1);
  m_vertex_buffer_descriptor_info = m_collision_mesh_vertex_uniform_buffer.descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
}

void CollideMeshVulkanRenderer::init_pat_colors(GameVersion version) {
  for (int i = 0; i < 0x8; ++i) {
    m_colors.pat_mode_colors[i].x() = -1.f;
    m_colors.pat_mode_colors[i].y() = -1.f;
    m_colors.pat_mode_colors[i].z() = -1.f;
  }
  for (int i = 0; i < 0x40; ++i) {
    m_colors.pat_material_colors[i].x() = -1.f;
    m_colors.pat_material_colors[i].y() = -1.f;
    m_colors.pat_material_colors[i].z() = -1.f;
  }
  for (int i = 0; i < 0x40; ++i) {
    m_colors.pat_event_colors[i].x() = -1.f;
    m_colors.pat_event_colors[i].y() = -1.f;
    m_colors.pat_event_colors[i].z() = -1.f;
  }

  switch (version) {
    case GameVersion::Jak1:
      for (int i = 0; i < 23 * 3; ++i) {
        m_colors.pat_material_colors[i / 3].data()[i % 3] = collision::material_colors_jak1[i];
      }
      for (int i = 0; i < 7 * 3; ++i) {
        m_colors.pat_event_colors[i / 3].data()[i % 3] = collision::event_colors_jak1[i];
      }
      for (int i = 0; i < 3 * 3; ++i) {
        m_colors.pat_mode_colors[i / 3].data()[i % 3] = collision::mode_colors_jak1[i];
      }
      break;
    case GameVersion::Jak2:
      break;
  }
}

void CollideMeshVulkanRenderer::create_pipeline_layout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_vertex_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  VkPushConstantRange pushConstantVertexRange{};
  pushConstantVertexRange.offset = 0;
  pushConstantVertexRange.size = sizeof(m_push_constant);
  pushConstantVertexRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  pipelineLayoutInfo.pPushConstantRanges = &pushConstantVertexRange;
  pipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void CollideMeshVulkanRenderer::init_shaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::COLLISION];
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

CollideMeshVulkanRenderer::~CollideMeshVulkanRenderer() {
}

void CollideMeshVulkanRenderer::render(SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  if (!render_state->has_pc_data) {
    return;
  }

  std::vector<LevelDataVulkan*> levels = m_vulkan_info.loader->get_in_use_levels();
  if (levels.empty()) {
    return;
  }

  m_collision_mesh_vertex_uniform_buffer.Set4x4MatrixDataInVkDeviceMemory(
      "camera", 1, VK_FALSE,
                     render_state->camera_matrix[0].data());
  m_collision_mesh_vertex_uniform_buffer.SetUniform4f(
      "hvdf_offset", render_state->camera_hvdf_off[0], render_state->camera_hvdf_off[1],
      render_state->camera_hvdf_off[2], render_state->camera_hvdf_off[3]);
  const auto& trans = render_state->camera_pos;
  m_collision_mesh_vertex_uniform_buffer.SetUniform4f("camera_position", trans[0], trans[1],
                                                      trans[2], trans[3]);
  m_collision_mesh_vertex_uniform_buffer.SetUniform1f("fog_constant", render_state->camera_fog.x());
  m_collision_mesh_vertex_uniform_buffer.SetUniform1f("fog_min", render_state->camera_fog.y());
  m_collision_mesh_vertex_uniform_buffer.SetUniform1f("fog_max", render_state->camera_fog.z());
  m_collision_mesh_vertex_uniform_buffer.SetUniform1ui("version", static_cast<u32>(render_state->version));

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 1.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 1.0f;

  m_vulkan_info.swap_chain->setViewportScissor(m_vulkan_info.render_command_buffer);

  for (LevelDataVulkan* level : levels) {
    m_push_constant.wireframe = 0;
    ::memcpy(m_push_constant.collision_mode_mask, Gfx::g_global_settings.collision_mode_mask.data(),
             Gfx::g_global_settings.collision_mode_mask.size());
    ::memcpy(m_push_constant.collision_event_mask,
             Gfx::g_global_settings.collision_event_mask.data(),
             Gfx::g_global_settings.collision_event_mask.size());
    ::memcpy(m_push_constant.collision_material_mask,
             Gfx::g_global_settings.collision_material_mask.data(),
             Gfx::g_global_settings.collision_material_mask.size());
    m_push_constant.collision_skip_mask = Gfx::g_global_settings.collision_skip_mask;
    m_push_constant.mode = Gfx::g_global_settings.collision_mode;

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, sizeof(m_push_constant),
                       &m_push_constant);

    m_pipeline_layout.updateGraphicsPipeline(
        m_vulkan_info.render_command_buffer, m_pipeline_config_info);
    m_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    VkDeviceSize offsets[] = {0};
    VkBuffer vertex_buffer_vulkan = level->collide_vertices->getBuffer();
    vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, &vertex_buffer_vulkan,
                           offsets);

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, m_descriptor_sets.size(),
                            m_descriptor_sets.data(), 0, nullptr);

    vkCmdDraw(m_vulkan_info.render_command_buffer, level->collide_vertices->getBufferSize(), 0, 0,
              0);

    if (Gfx::g_global_settings.collision_wireframe) {
      m_push_constant.wireframe = 1;
      vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, sizeof(m_push_constant),
                         &m_push_constant);

      VkPipelineColorBlendAttachmentState& colorBlendAttachment = m_pipeline_config_info.colorBlendAttachment;
      m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
          VK_COLOR_COMPONENT_A_BIT;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      //imageView.aspectView &= ~VK_IMAGE_ASPECT_DEPTH_BIT;

      m_pipeline_config_info.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
      m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
      m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
      m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; //TODO: Verify that this is correct
      m_pipeline_config_info.rasterizationInfo.depthBiasEnable = VK_FALSE;

      m_pipeline_layout.updateGraphicsPipeline(
          m_vulkan_info.render_command_buffer, m_pipeline_config_info);
      m_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

      vkCmdDraw(m_vulkan_info.render_command_buffer, level->collide_vertices->getBufferSize(), 0, 0,
                0);

      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      colorBlendAttachment.blendEnable = VK_TRUE;
      //imageView.aspectView |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    prof.add_draw_call();
    prof.add_tri(level->level->collision.vertices.size() / 3);
  }
}

void CollideMeshVulkanRenderer::InitializeInputVertexAttribute() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(tfrag3::CollisionMesh::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    // TODO: This value needs to be normalized
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(tfrag3::CollisionMesh::Vertex, flags);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R16G16B16_SNORM;
    attributeDescriptions[2].offset = offsetof(tfrag3::CollisionMesh::Vertex, nx);

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 2;
    attributeDescriptions[3].format = VK_FORMAT_R32_UINT;
    attributeDescriptions[3].offset = offsetof(tfrag3::CollisionMesh::Vertex, pat);
    m_pipeline_config_info.attributeDescriptions.insert(
        m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(), attributeDescriptions.end());
}

CollisionMeshVertexUniformBuffer::CollisionMeshVertexUniformBuffer(
    std::shared_ptr<GraphicsDeviceVulkan> device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(CollisionMeshVertexUniformShaderData),
                          instanceCount,
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"hvdf_offset", offsetof(CollisionMeshVertexUniformShaderData, hvdf_offset)},
      {"camera", offsetof(CollisionMeshVertexUniformShaderData, camera)},
      {"camera_position", offsetof(CollisionMeshVertexUniformShaderData, camera_position)},
      {"fog_constant", offsetof(CollisionMeshVertexUniformShaderData, fog_constant)},
      {"fog_min", offsetof(CollisionMeshVertexUniformShaderData, fog_min)},
      {"fog_max", offsetof(CollisionMeshVertexUniformShaderData, fog_max)},
  };
}

CollisionMeshVertexPatternUniformBuffer::CollisionMeshVertexPatternUniformBuffer(
    std::shared_ptr<GraphicsDeviceVulkan> device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device, sizeof(PatColors),
                          instanceCount,
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"pat_mode_colors", offsetof(PatColors, pat_mode_colors)},
      {"pat_material_colors", offsetof(PatColors, pat_material_colors)},
      {"pat_event_colors", offsetof(PatColors, pat_event_colors)},
  };
}
