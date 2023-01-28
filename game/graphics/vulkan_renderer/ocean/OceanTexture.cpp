#include "OceanTexture.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

OceanVulkanTexture::OceanVulkanTexture(bool generate_mipmaps,
                           std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VulkanInitializationInfo& vulkan_info)
    : BaseOceanTexture(m_generate_mipmaps),
      m_result_texture(TEX0_SIZE,
                       TEX0_SIZE,
                       VK_FORMAT_A8B8G8R8_SRGB_PACK32,
                       device,
                       m_generate_mipmaps ? NUM_MIPS : 1),
      m_temp_texture(TEX0_SIZE, TEX0_SIZE, VK_FORMAT_A8B8G8R8_SRGB_PACK32, device), m_device(device),
      m_vulkan_info(vulkan_info) {
  m_dbuf_x = m_dbuf_a;
  m_dbuf_y = m_dbuf_b;

  m_tbuf_x = m_tbuf_a;
  m_tbuf_y = m_tbuf_b;

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_info);

  init_pc();
  SetupShader(ShaderId::OCEAN_TEXTURE);

  m_common_uniform_vertex_buffer =
      std::make_unique<CommonOceanVertexUniformBuffer>(m_device, 1, 1);
  m_common_uniform_fragment_buffer =
      std::make_unique<CommonOceanFragmentUniformBuffer>(m_device, 1, 1);

  m_ocean_mipmap_uniform_vertex_buffer =
      std::make_unique<OceanMipMapVertexUniformBuffer>(m_device, 1, 1);
  m_ocean_mipmap_uniform_fragment_buffer =
      std::make_unique<OceanMipMapFragmentUniformBuffer>(m_device, 1, 1);

  m_vulkan_pc.dynamic_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(Vertex), NUM_VERTS, 1);
  m_vulkan_pc.graphics_index_buffer =
      std::make_unique<IndexBuffer>(m_device, sizeof(u32), NUM_VERTS, 1);

  InitializeVertexBuffer();
  // initialize the mipmap drawing
  m_pipeline_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;
}

void OceanVulkanTexture::draw_debug_window() {
  if (m_tex0_gpu) {
    auto vulkan_texture = m_tex0_gpu->get_selected_texture();
    ImGui::Image((void*)vulkan_texture->GetTextureId(), ImVec2(vulkan_texture->getWidth(), vulkan_texture->getHeight()));
  }
}

OceanVulkanTexture::~OceanVulkanTexture() {
  destroy_pc();
}

void OceanVulkanTexture::init_textures(VulkanTexturePool& pool) {
  VulkanTextureInput in;
  //in.texture = m_result_texture.texture();
  in.debug_page_name = "PC-OCEAN";
  in.debug_name = fmt::format("pc-ocean-mip-{}", m_generate_mipmaps);
  in.id = pool.allocate_pc_port_texture();
  m_tex0_gpu = pool.give_texture_and_load_to_vram(in, OCEAN_TEX_TBP);
}

void OceanVulkanTexture::handle_ocean_texture(DmaFollower& dma,
                                        BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  BaseOceanTexture::handle_ocean_texture(dma, render_state, prof);
}

/*!
 * Generate mipmaps for the ocean texture.
 * There's a trick here - we reduce the intensity of alpha on the lower lods. This lets texture
 * filtering slowly fade the alpha value out to 0 with distance.
 */
void OceanVulkanTexture::make_texture_with_mipmaps(BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) {
  m_ocean_mipmap_uniform_fragment_buffer->SetUniform1f("alpha_intensity", 1.0);
  m_ocean_mipmap_uniform_vertex_buffer->SetUniform1f("tex_T0", 0);

  for (int i = 0; i < NUM_MIPS; i++) {
    FramebufferVulkanTexturePairContext ctxt(m_result_texture, i);
    m_ocean_mipmap_uniform_fragment_buffer->SetUniform1f("alpha_intensity", std::max(0.f, 1.f - 0.51f * i));
    m_ocean_mipmap_uniform_vertex_buffer->SetUniform1f("scale", 1.f / (1 << i));
    //glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    prof.add_draw_call();
    prof.add_tri(2);
  }

  //FIXME: Draw here
}

void OceanVulkanTexture::flush(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  ASSERT(m_pc.vtx_idx == 2112);

  m_vulkan_pc.dynamic_vertex_buffer->writeToGpuBuffer(m_pc.vertex_dynamic.data());
  m_vulkan_pc.graphics_index_buffer->writeToGpuBuffer(m_pc.index_buffer.data());

  SetupShader(ShaderId::OCEAN_TEXTURE);

  GsTex0 tex0(m_envmap_adgif.tex0_data);
  auto lookup = m_vulkan_info.texture_pool->lookup_vulkan_texture(tex0.tbp0());
  if (!lookup) {
    lookup = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
  }
  // no decal
  // yes tcc
  m_sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  m_sampler_create_info.magFilter = VK_FILTER_LINEAR;
  m_sampler_create_info.minFilter = VK_FILTER_LINEAR;

  // glDrawArrays(GL_TRIANGLE_STRIP, 0, NUM_VERTS);

  //glDrawElements(GL_TRIANGLE_STRIP, m_pc.index_buffer.size(), GL_UNSIGNED_INT, (void*)0);

  prof.add_draw_call();
  prof.add_tri(NUM_STRIPS * NUM_STRIPS * 2);
}

void OceanVulkanTexture::InitializeMipmapVertexInputAttributes() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(MipMap::Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(MipMap::Vertex, x);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(MipMap::Vertex, s);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}

void OceanVulkanTexture::SetupShader(ShaderId shaderId) {
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

  m_pipeline_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

void OceanVulkanTexture::InitializeVertexBuffer() {
  std::vector<MipMap::Vertex> vertices = {
      {-1, -1, 0, 0}, {-1, 1, 0, 1}, {1, -1, 1, 0}, {1, 1, 1, 1}};

  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, sizeof(vertices), 1, 1);
  m_vertex_buffer->writeToGpuBuffer(vertices.data());

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  m_sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  m_sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  m_sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  m_sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  m_sampler_create_info.anisotropyEnable = VK_TRUE;
  // samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  m_sampler_create_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  m_sampler_create_info.unnormalizedCoordinates = VK_FALSE;
  m_sampler_create_info.compareEnable = VK_FALSE;
  m_sampler_create_info.compareOp = VK_COMPARE_OP_ALWAYS;
  m_sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  m_sampler_create_info.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  m_sampler_create_info.mipLodBias = 0.0f;

  m_sampler_create_info.magFilter = VK_FILTER_LINEAR;
  m_sampler_create_info.minFilter = VK_FILTER_LINEAR;
}

void OceanVulkanTexture::set_gpu_texture(TextureInput&) {
}

void OceanVulkanTexture::move_existing_to_vram(u32 slot_addr) {
  m_vulkan_info.texture_pool->move_existing_to_vram(m_tex0_gpu, slot_addr);
}

void OceanVulkanTexture::setup_framebuffer_context(int) {
}

OceanMipMapVertexUniformBuffer::OceanMipMapVertexUniformBuffer(
  std::unique_ptr<GraphicsDeviceVulkan>& device,
  uint32_t instanceCount,
  VkDeviceSize minOffsetAlignment) : UniformVulkanBuffer(device, sizeof(float), instanceCount, minOffsetAlignment){
  section_name_to_memory_offset_map = {{"scale", 0}};
}

OceanMipMapFragmentUniformBuffer::OceanMipMapFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                   uint32_t instanceCount,
                                   VkDeviceSize minOffsetAlignment) : UniformVulkanBuffer(device, sizeof(float), instanceCount, minOffsetAlignment){
  section_name_to_memory_offset_map = {
      {"alpha_intensity", 0}};
};
