#include "OceanTexture.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

OceanVulkanTexture::OceanVulkanTexture(bool generate_mipmaps,
                           std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VulkanInitializationInfo& vulkan_info)
    : BaseOceanTexture(m_generate_mipmaps),
      m_result_texture(TEX0_SIZE,
                       TEX0_SIZE,
                       VK_FORMAT_A8B8G8R8_SINT_PACK32,
                       device,
                       m_generate_mipmaps ? NUM_MIPS : 1),
      m_temp_texture(TEX0_SIZE, TEX0_SIZE, VK_FORMAT_A8B8G8R8_SINT_PACK32, device), m_device(device),
      m_vulkan_info(vulkan_info) {
  m_dbuf_x = m_dbuf_a;
  m_dbuf_y = m_dbuf_b;

  m_tbuf_x = m_tbuf_a;
  m_tbuf_y = m_tbuf_b;

  init_pc();
  SetupShader();

  // initialize the mipmap drawing
}

OceanVulkanTexture::~OceanVulkanTexture() {
  destroy_pc();
}

void OceanVulkanTexture::init_textures(TexturePoolVulkan& pool) {
  TextureInput in;
  in.gpu_texture = (u64)m_result_texture.texture();
  in.w = TEX0_SIZE;
  in.h = TEX0_SIZE;
  in.debug_page_name = "PC-OCEAN";
  in.debug_name = fmt::format("pc-ocean-mip-{}", m_generate_mipmaps);
  in.id = pool.allocate_pc_port_texture();
  m_tex0_gpu = pool.give_texture_and_load_to_vram(in, OCEAN_TEX_TBP);
}

void OceanVulkanTexture::handle_ocean_texture(DmaFollower& dma,
                                        SharedVulkanRenderState* render_state,
                                        ScopedProfilerNode& prof,
                                        std::unique_ptr<CommonOceanVertexUniformBuffer>& uniform_vertex_buffer,
                                        std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_fragment_buffer) {

  InitializeVertexBuffer();
  BaseOceanTexture::handle_ocean_texture(dma, render_state, prof);
}

/*!
 * Generate mipmaps for the ocean texture.
 * There's a trick here - we reduce the intensity of alpha on the lower lods. This lets texture
 * filtering slowly fade the alpha value out to 0 with distance.
 */
void OceanVulkanTexture::make_texture_with_mipmaps(SharedVulkanRenderState* render_state,
                                             ScopedProfilerNode& prof,
                                             std::unique_ptr<CommonOceanFragmentUniformBuffer>& uniform_buffer) {
  uniform_buffer->SetUniform1f("alpha_intensity", 1.0);
  uniform_buffer->SetUniform1f("tex_T0", 0);

  for (int i = 0; i < NUM_MIPS; i++) {
    FramebufferVulkanTexturePairContext ctxt(m_result_texture, i);
    uniform_buffer->SetUniform1f("alpha_intensity", std::max(0.f, 1.f - 0.51f * i));
    uniform_buffer->SetUniform1f("scale", 1.f / (1 << i));
    //glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    prof.add_draw_call();
    prof.add_tri(2);
  }

  //FIXME: Draw here
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

void OceanVulkanTexture::SetupShader() {
  auto& shader = m_vulkan_info.shaders[ShaderId::OCEAN_TEXTURE_MIPMAP];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "Ocean Texture Vertex";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "Ocean Texture Fragment";

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

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
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
}

void OceanVulkanTexture::set_gpu_texture(TextureInput&) {
}

void OceanVulkanTexture::move_existing_to_vram(GpuTexture* tex, u32 slot_addr) {
  m_vulkan_info.texture_pool->move_existing_to_vram(tex, slot_addr);
}

void OceanVulkanTexture::setup_framebuffer_context(int) {
}
