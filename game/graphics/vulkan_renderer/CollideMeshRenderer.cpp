#include "CollideMeshRenderer.h"

#include "game/graphics/vulkan_renderer/background/background_common.h"

CollideMeshRenderer::CollideMeshRenderer() {
  InitializeInputVertexAttribute();
}

CollideMeshRenderer::~CollideMeshRenderer() {
}

void CollideMeshRenderer::render(SharedRenderState* render_state, ScopedProfilerNode& prof, UniformBuffer& uniform_buffer) {
  if (!render_state->has_pc_data) {
    return;
  }

  auto levels = render_state->loader->get_in_use_levels();
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
  auto& shader = render_state->shaders[ShaderId::COLLISION];
  uniform_buffer.Set4x4MatrixDataInVkDeviceMemory("camera", 1, GL_FALSE,
                     settings.math_camera.data());
  uniform_buffer.SetUniform4f("hvdf_offset", settings.hvdf_offset[0],
              settings.hvdf_offset[1], settings.hvdf_offset[2], settings.hvdf_offset[3]);
  const auto& trans = render_state->camera_pos;
  uniform_buffer.SetUniform4f("camera_position", trans[0], trans[1], trans[2], trans[3]);
  uniform_buffer.SetUniform1f("fog_constant", settings.fog.x());
  uniform_buffer.SetUniform1f("fog_min", settings.fog.y());
  uniform_buffer.SetUniform1f("fog_max", settings.fog.z());

  //glDepthMask(GL_TRUE);

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_TRUE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.blendConstants[0] = 1.0f;
  colorBlending.blendConstants[1] = 1.0f;
  colorBlending.blendConstants[2] = 1.0f;
  colorBlending.blendConstants[3] = 1.0f;

  for (auto lev : levels) {
    uniform_buffer.SetUniform1f("wireframe", 0);
    uniform_buffer.SetUniformVectorUnsigned("collision_mode_mask",
                Gfx::g_global_settings.collision_mode_mask.size(),
                Gfx::g_global_settings.collision_mode_mask.data());
    uniform_buffer.SetUniformVectorUnsigned("collision_event_mask",
                Gfx::g_global_settings.collision_event_mask.size(),
                Gfx::g_global_settings.collision_event_mask.data());
    uniform_buffer.SetUniformVectorUnsigned("collision_material_mask",
                Gfx::g_global_settings.collision_material_mask.size(),
                Gfx::g_global_settings.collision_material_mask.data());
    uniform_buffer.SetUniform1i("collision_skip_mask",
               Gfx::g_global_settings.collision_skip_mask);
    uniform_buffer.SetUniform1f("mode", Gfx::g_global_settings.collision_mode);
    //glDrawArrays(GL_TRIANGLES, 0, lev->level->collision.vertices.size());

    //CreateVertexBuffer(lev->level->collision.vertices);

    if (Gfx::g_global_settings.collision_wireframe) {
      uniform_buffer.SetUniform1i("wireframe", 1);
      VkPipelineColorBlendAttachmentState colorBlendAttachment{};
      colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      colorBlendAttachment.blendEnable = VK_FALSE;
      //imageView.aspectView &= ~VK_IMAGE_ASPECT_DEPTH_BIT;

      VkPipelineRasterizationStateCreateInfo rasterizer{};
      rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
      rasterizer.rasterizerDiscardEnable = VK_FALSE;
      rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
      rasterizer.lineWidth = 1.0f;
      rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
      rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; //TODO: Verify that this is correct
      rasterizer.depthBiasEnable = VK_FALSE;

      //CreateVertexBuffer(lev->level->collision.vertices);
      //glDrawArrays(GL_TRIANGLES, 0, lev->level->collision.vertices.size());
      rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
      colorBlendAttachment.blendEnable = VK_TRUE;
      //imageView.aspectView |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    prof.add_draw_call();
    prof.add_tri(lev->level->collision.vertices.size() / 3);
  }
}

void CollideMeshRenderer::InitializeInputVertexAttribute() {    
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(tfrag3::CollisionMesh::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}

