#include "CollideMeshRenderer.h"

#include "game/graphics/display.h"
#include "game/graphics/vulkan_renderer/background/background_common.h"

CollideMeshVulkanRenderer::CollideMeshVulkanRenderer(std::unique_ptr<GraphicsDeviceVulkan>& device, VulkanInitializationInfo& vulkan_info) :
      m_device(device),
      m_collision_mesh_vertex_uniform_buffer(device, sizeof(CollisionMeshVertexUniformShaderData), 1, 1),
      m_pipeline_layout{device},
      m_vulkan_info{vulkan_info} {

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_config_info);
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  InitializeInputVertexAttribute();
  init_shaders();

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  create_pipeline_layout();
  m_vertex_descriptor_writer = std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout,
                                                                  m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(1);
  m_vertex_buffer_descriptor_info = m_collision_mesh_vertex_uniform_buffer.descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &m_vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
}

void CollideMeshVulkanRenderer::create_pipeline_layout() {
  // If push constants are needed put them here

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_vertex_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

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

  TfragRenderSettings settings;
  memcpy(settings.math_camera.data(), render_state->camera_matrix[0].data(), 64);
  settings.hvdf_offset = render_state->camera_hvdf_off;
  settings.fog = render_state->camera_fog;
  settings.tree_idx = 0;
  for (int i = 0; i < 4; i++) {
    settings.planes[i] = render_state->camera_planes[i];
  }

  m_collision_mesh_vertex_uniform_buffer.Set4x4MatrixDataInVkDeviceMemory(
      "camera", 1, GL_FALSE,
                     settings.math_camera.data());
  m_collision_mesh_vertex_uniform_buffer.SetUniform4f(
      "hvdf_offset", settings.hvdf_offset[0],
              settings.hvdf_offset[1], settings.hvdf_offset[2], settings.hvdf_offset[3]);
  const auto& trans = render_state->camera_pos;
  m_collision_mesh_vertex_uniform_buffer.SetUniform4f("camera_position", trans[0], trans[1],
                                                      trans[2], trans[3]);
  m_collision_mesh_vertex_uniform_buffer.SetUniform1f("fog_constant", settings.fog.x());
  m_collision_mesh_vertex_uniform_buffer.SetUniform1f("fog_min", settings.fog.y());
  m_collision_mesh_vertex_uniform_buffer.SetUniform1f("fog_max", settings.fog.z());

  //glDepthMask(GL_TRUE);

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
    m_collision_mesh_vertex_uniform_buffer.SetUniform1f("wireframe", 0);
    m_collision_mesh_vertex_uniform_buffer.SetUniformVectorUnsigned(
        "collision_mode_mask",
                Gfx::g_global_settings.collision_mode_mask.size(),
                Gfx::g_global_settings.collision_mode_mask.data());
    m_collision_mesh_vertex_uniform_buffer.SetUniformVectorUnsigned(
        "collision_event_mask",
                Gfx::g_global_settings.collision_event_mask.size(),
                Gfx::g_global_settings.collision_event_mask.data());
    m_collision_mesh_vertex_uniform_buffer.SetUniformVectorUnsigned(
        "collision_material_mask",
                Gfx::g_global_settings.collision_material_mask.size(),
                Gfx::g_global_settings.collision_material_mask.data());
    m_collision_mesh_vertex_uniform_buffer.SetUniform1i("collision_skip_mask",
               Gfx::g_global_settings.collision_skip_mask);
    m_collision_mesh_vertex_uniform_buffer.SetUniform1f("mode",
                                                        Gfx::g_global_settings.collision_mode);

    m_collision_mesh_vertex_uniform_buffer.map();
    m_collision_mesh_vertex_uniform_buffer.flush();
    m_collision_mesh_vertex_uniform_buffer.unmap();

    m_pipeline_layout.createGraphicsPipeline(m_pipeline_config_info);
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
      m_collision_mesh_vertex_uniform_buffer.SetUniform1i("wireframe", 1);
      m_collision_mesh_vertex_uniform_buffer.map();
      m_collision_mesh_vertex_uniform_buffer.flush();
      m_collision_mesh_vertex_uniform_buffer.unmap();

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

      m_pipeline_layout.createGraphicsPipeline(m_pipeline_config_info);
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
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VkDeviceSize instanceSize,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device, instanceSize, instanceCount, minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"hvdf_offset", offsetof(CollisionMeshVertexUniformShaderData, hvdf_offset)},
      {"camera", offsetof(CollisionMeshVertexUniformShaderData, camera)},
      {"camera_position", offsetof(CollisionMeshVertexUniformShaderData, camera_position)},
      {"fog_constant", offsetof(CollisionMeshVertexUniformShaderData, fog_constant)},
      {"fog_min", offsetof(CollisionMeshVertexUniformShaderData, fog_min)},
      {"fog_max", offsetof(CollisionMeshVertexUniformShaderData, fog_max)},
      {"wireframe", offsetof(CollisionMeshVertexUniformShaderData, wireframe)},
      {"mode", offsetof(CollisionMeshVertexUniformShaderData, mode)},
      {"collision_mode_mask", offsetof(CollisionMeshVertexUniformShaderData, collision_mode_mask)},
      {"collision_event_mask",
       offsetof(CollisionMeshVertexUniformShaderData, collision_event_mask)},
      {"collision_material_mask",
       offsetof(CollisionMeshVertexUniformShaderData, collision_material_mask)},
      {"collision_skip_mask", offsetof(CollisionMeshVertexUniformShaderData, collision_skip_mask)}
  };
}
