#include "ShadowRenderer.h"

#include <cfloat>

#include "third-party/imgui/imgui.h"

ShadowVulkanRenderer::ShadowVulkanRenderer(
    const std::string& name,
    int my_id,
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    VulkanInitializationInfo& vulkan_info)
    : BaseShadowRenderer(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  // set up the vertex array
  m_ogl.vertex_buffer = std::make_unique<VertexBuffer>(
      device, sizeof(Vertex), MAX_VERTICES, 1);

  for (int i = 0; i < 2; i++) {
    m_ogl.index_buffers[i] = std::make_unique<IndexBuffer>(
        device, sizeof(u32), MAX_INDICES, 1);
  }

  m_uniform_buffer = std::make_unique<ShadowRendererUniformBuffer>(
      device, 1);

  // xyz
  InitializeInputVertexAttribute();
}

void ShadowVulkanRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, xyz);
  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescriptions[0]);
}

ShadowVulkanRenderer::~ShadowVulkanRenderer() {
}

void ShadowVulkanRenderer::draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  u32 clear_vertices = m_next_vertex;
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.3, 0.3, 0), 0};
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.3, 0.7, 0), 0};
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.7, 0.3, 0), 0};
  m_vertices[m_next_vertex++] = Vertex{math::Vector3f(0.7, 0.7, 0), 0};
  m_front_indices[m_next_front_index++] = clear_vertices;
  m_front_indices[m_next_front_index++] = clear_vertices + 1;
  m_front_indices[m_next_front_index++] = clear_vertices + 2;
  m_front_indices[m_next_front_index++] = clear_vertices + 3;
  m_front_indices[m_next_front_index++] = clear_vertices + 2;
  m_front_indices[m_next_front_index++] = clear_vertices + 1;

  m_ogl.vertex_buffer->map(m_next_vertex * sizeof(Vertex));
  m_ogl.vertex_buffer->writeToGpuBuffer(m_vertices);
  m_ogl.vertex_buffer->unmap();

  m_pipeline_config_info.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  m_pipeline_config_info.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
  m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  m_pipeline_config_info.rasterizationInfo.lineWidth = 1.0f;
  m_pipeline_config_info.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  m_pipeline_config_info.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  m_pipeline_config_info.rasterizationInfo.depthBiasEnable = VK_FALSE;

  m_pipeline_config_info.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  if (m_debug_draw_volume) {
    m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  } else {
    m_pipeline_config_info.colorBlendAttachment.colorWriteMask = 0;
  }

  m_pipeline_config_info.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.front.writeMask = 0xFF;

  m_pipeline_config_info.depthStencilInfo.back.failOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.back.writeMask = 0xFF;

  // First pass.
  // here, we don't write depth or color.
  // but we increment stencil on depth fail.

  {
    m_uniform_buffer->SetUniform4f("color_uniform",
                0., 0.4, 0., 0.5);
    m_ogl.index_buffers[0]->map(m_next_front_index * sizeof(u32));
    m_ogl.index_buffers[0]->writeToGpuBuffer(m_back_indices);
    m_ogl.index_buffers[0]->unmap();

    m_pipeline_config_info.depthStencilInfo.front.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.back.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    m_pipeline_config_info.depthStencilInfo.back.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;

    //glDrawElements(GL_TRIANGLES, (m_next_front_index - 6), GL_UNSIGNED_INT, nullptr);

    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_uniform_buffer->SetUniform4f("color_uniform", 0.,
          0.0, 0., 0.5);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      //glDrawElements(GL_TRIANGLES, (m_next_front_index - 6), GL_UNSIGNED_INT, nullptr);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    }
    prof.add_draw_call();
    prof.add_tri(m_next_back_index / 3);
  }

  {
    m_uniform_buffer->SetUniform4f("color_uniform",
                0.4, 0.0, 0., 0.5);

    m_ogl.index_buffers[1]->map(m_next_back_index * sizeof(u32));
    m_ogl.index_buffers[1]->writeToGpuBuffer(m_back_indices);
    m_ogl.index_buffers[1]->unmap();

    // Second pass.

    m_pipeline_config_info.depthStencilInfo.front.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.back.compareMask = 0;
    m_pipeline_config_info.depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

    m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    m_pipeline_config_info.depthStencilInfo.back.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    //glDrawElements(GL_TRIANGLES, m_next_back_index, GL_UNSIGNED_INT, nullptr);
    if (m_debug_draw_volume) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      m_uniform_buffer->SetUniform4f("color_uniform", 0., 0.0, 0., 0.5);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
      //glDrawElements(GL_TRIANGLES, (m_next_back_index - 0), GL_UNSIGNED_INT, nullptr);
      m_pipeline_config_info.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    }

    prof.add_draw_call();
    prof.add_tri(m_next_front_index / 3);
  }

  // finally, draw shadow.
  m_uniform_buffer->SetUniform4f("color_uniform", 0.13, 0.13, 0.13, 0.5);
  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  m_pipeline_config_info.depthStencilInfo.front.compareMask = 0xFF;
  m_pipeline_config_info.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;

  m_pipeline_config_info.depthStencilInfo.back.compareMask = 0xFF;
  m_pipeline_config_info.depthStencilInfo.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;

  m_pipeline_config_info.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
  m_pipeline_config_info.depthStencilInfo.back.passOp = VK_STENCIL_OP_KEEP;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(sizeof(u32) * (m_next_front_index - 6)));
  prof.add_draw_call();
  prof.add_tri(2);

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional
  //glDepthMask(GL_TRUE);

  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;
}

ShadowRendererUniformBuffer::ShadowRendererUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                         uint32_t instanceCount,
                                                         VkDeviceSize minOffsetAlignment) :
  UniformVulkanBuffer(device, sizeof(math::Vector4f), instanceCount, minOffsetAlignment){
  section_name_to_memory_offset_map = {
      {"color_uniform", 0}
  };
}
