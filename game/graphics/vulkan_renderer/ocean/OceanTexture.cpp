#include "OceanTexture.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

OceanVulkanTexture::OceanVulkanTexture(bool generate_mipmaps,
                                       std::shared_ptr<GraphicsDeviceVulkan> device,
                                       VulkanInitializationInfo& vulkan_info)
    : BaseOceanTexture(m_generate_mipmaps), m_device(device), m_vulkan_info(vulkan_info) {
  m_dbuf_x = m_dbuf_a;
  m_dbuf_y = m_dbuf_b;

  m_tbuf_x = m_tbuf_a;
  m_tbuf_y = m_tbuf_b;

  m_result_texture = std::make_unique<FramebufferVulkanHelper>(
      TEX0_SIZE, TEX0_SIZE, VK_FORMAT_R8G8B8A8_UNORM, device, VK_SAMPLE_COUNT_1_BIT,
      1);  // m_generate_mipmaps ? NUM_MIPS : 1);
  m_temp_texture = std::make_unique<FramebufferVulkanHelper>(TEX0_SIZE, TEX0_SIZE,
                                                             VK_FORMAT_R8G8B8A8_UNORM, device);

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_info);
  InitializeMipmapVertexInputAttributes();
  m_ocean_texture_mipmap_graphics_pipeline_layouts.resize(NUM_MIPS, {device});

  init_pc();
  SetupShader(ShaderId::OCEAN_TEXTURE);

  static_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(math::Vector2f) * NUM_VERTS, 1, 1);
  dynamic_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(Vertex) * NUM_VERTS, 1, 1);

  // Allocated extra size to account for primitive restarts in index buffer
  graphics_index_buffer =
      std::make_unique<IndexBuffer>(m_device, sizeof(u32) * NUM_VERTS * 2, 1, 1);  

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_ocean_texture_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);

  CreatePipelineLayout();
  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);
  VkDescriptorSetLayout descriptorSetLayout =
      m_fragment_descriptor_layout->getDescriptorSetLayout();
  m_vulkan_info.descriptor_pool->allocateDescriptor(&descriptorSetLayout,
                                                    &m_ocean_texture_descriptor_set);
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{NUM_MIPS, descriptorSetLayout};
  m_vulkan_info.descriptor_pool->allocateDescriptor(descriptorSetLayouts.data(),
                                                    m_ocean_mipmap_texture_descriptor_sets.data(),
                                                    m_ocean_mipmap_texture_descriptor_sets.size());

  m_fragment_descriptor_writer->writeImage(
      0, m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info());

  InitializeVertexBuffer();
  // initialize the mipmap drawing
  m_pipeline_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;
}

void OceanVulkanTexture::draw_debug_window() {
  if (m_tex0_gpu) {
    auto vulkan_texture = m_tex0_gpu->get_selected_texture();
    ImGui::Image((void*)vulkan_texture->GetTextureId(),
                 ImVec2(vulkan_texture->getWidth(), vulkan_texture->getHeight()));
  }
}

OceanVulkanTexture::~OceanVulkanTexture() {
  destroy_pc();
}

void OceanVulkanTexture::init_textures(VulkanTexturePool& pool) {
  VulkanTextureInput in;
  in.texture = &m_result_texture->ColorAttachmentTexture();
  in.debug_page_name = "PC-OCEAN";
  in.debug_name = fmt::format("pc-ocean-mip-{}", m_generate_mipmaps);
  in.id = pool.allocate_pc_port_texture();
  u32 id = GetOceanTextureId();
  m_tex0_gpu = pool.give_texture_and_load_to_vram(in, id);
}

void OceanVulkanTextureJak1::handle_ocean_texture(DmaFollower& dma,
                                                  BaseSharedRenderState* render_state,
                                                  ScopedProfilerNode& prof) {
  BaseOceanTextureJak1::handle_ocean_texture(dma, render_state, prof);
}

void OceanVulkanTextureJak2::handle_ocean_texture(DmaFollower& dma,
                                                  BaseSharedRenderState* render_state,
                                                  ScopedProfilerNode& prof) {
  BaseOceanTextureJak2::handle_ocean_texture(dma, render_state, prof);
}

/*!
 * Generate mipmaps for the ocean texture.
 * There's a trick here - we reduce the intensity of alpha on the lower lods. This lets texture
 * filtering slowly fade the alpha value out to 0 with distance.
 */
void OceanVulkanTexture::make_texture_with_mipmaps(BaseSharedRenderState* render_state,
                                                   ScopedProfilerNode& prof) {
  vkCmdEndRenderPass(m_command_buffer);
  // FIXME: image layout is to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL at this point but
  // texture.initialLayout is set to VK_IMAGE_LAYOUT_UNDEFINED
  // m_temp_texture->ColorAttachmentTexture().SetImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  // m_result_texture->ColorAttachmentTexture().SetImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_result_texture->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, NUM_MIPS);
  m_temp_texture->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands();

  VkImageBlit blit{};
  blit.srcOffsets[0] = {0, 0, 0};
  blit.srcOffsets[1] = {TEX0_SIZE, TEX0_SIZE, 1};
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.mipLevel = 0;
  blit.srcSubresource.baseArrayLayer = 0;
  blit.srcSubresource.layerCount = 1;
  blit.dstOffsets[0] = {0, 0, 0};
  blit.dstOffsets[1] = {TEX0_SIZE, TEX0_SIZE, 1};
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.mipLevel = 0;
  blit.dstSubresource.baseArrayLayer = 0;
  blit.dstSubresource.layerCount = 1;

  vkCmdBlitImage(commandBuffer, m_temp_texture->ColorAttachmentTexture().getImage(),
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 m_result_texture->ColorAttachmentTexture().getImage(),
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
  m_device->endSingleTimeCommands(commandBuffer);

  m_result_texture->GenerateMipmaps();
  m_vulkan_info.swap_chain->beginSwapChainRenderPass(m_command_buffer,
                                                     m_vulkan_info.currentFrame);
  // m_result_texture->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, NUM_MIPS);
}

void OceanVulkanTexture::flush(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  ASSERT(m_pc.vtx_idx == 2112);

  vkCmdEndRenderPass(m_command_buffer);
  if (m_result_texture->GetCurrentSampleCount() != m_device->getMsaaCount()) {
    if (!m_generate_mipmaps) {
      m_result_texture->initializeFramebufferAtLevel(m_device->getMsaaCount(), 1);
    } else {
      m_temp_texture->initializeFramebufferAtLevel(m_device->getMsaaCount(), 1);
    }
  }

  auto& framebuffer_helper = (m_generate_mipmaps) ? m_temp_texture : m_result_texture;
  m_pipeline_info.renderPass = framebuffer_helper->GetRenderPass();
  m_pipeline_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();
  framebuffer_helper->beginRenderPass(m_command_buffer);

  static_vertex_buffer->writeToGpuBuffer(
      m_pc.vertex_positions.data(),
      m_pc.vertex_positions.size() * sizeof(m_pc.vertex_positions[0]));
  dynamic_vertex_buffer->writeToGpuBuffer(
      m_pc.vertex_dynamic.data(), m_pc.vertex_dynamic.size() * sizeof(m_pc.vertex_dynamic[0]));
  graphics_index_buffer->writeToGpuBuffer(m_pc.index_buffer.data(),
                                          m_pc.index_buffer.size() * sizeof(m_pc.index_buffer[0]));

  VkDeviceSize offsets[] = {0, 0};
  std::array<VkBuffer, 2> vertex_buffers = {static_vertex_buffer->getBuffer(),
                                            dynamic_vertex_buffer->getBuffer()};
  vkCmdBindVertexBuffers(m_command_buffer, 0, vertex_buffers.size(),
                         vertex_buffers.data(), offsets);

  vkCmdBindIndexBuffer(m_command_buffer,
                       graphics_index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

  SetupShader(ShaderId::OCEAN_TEXTURE);

  m_ocean_texture_graphics_pipeline_layout->createGraphicsPipelineIfNeeded(m_pipeline_info);
  m_ocean_texture_graphics_pipeline_layout->updateGraphicsPipeline(m_command_buffer, m_pipeline_info);
  m_ocean_texture_graphics_pipeline_layout->bind(m_command_buffer);

  GsTex0 tex0(m_envmap_adgif.tex0_data);
  auto lookup = m_vulkan_info.texture_pool->lookup_vulkan_texture(tex0.tbp0());
  if (!lookup) {
    lookup = m_vulkan_info.texture_pool->get_placeholder_vulkan_texture();
  }

  // no decal
  // yes tcc
  VkSamplerCreateInfo& sampler_create_info = m_sampler_helper.GetSamplerCreateInfo();
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_create_info.magFilter = VK_FILTER_LINEAR;
  sampler_create_info.minFilter = VK_FILTER_LINEAR;
  m_sampler_helper.CreateSampler();

  m_descriptor_image_info =
      VkDescriptorImageInfo{m_sampler_helper.GetSampler(), lookup->getImageView(),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[0] =
      m_fragment_descriptor_writer->writeImageDescriptorSet(0, &m_descriptor_image_info);

  m_fragment_descriptor_writer->overwrite(m_ocean_texture_descriptor_set);
  vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_info.pipelineLayout, 0, 1, &m_ocean_texture_descriptor_set, 0,
                          NULL);

  vkCmdDrawIndexed(m_command_buffer, m_pc.index_buffer.size(), 1, 0, 0, 0);

  prof.add_draw_call();
  prof.add_tri(NUM_STRIPS * NUM_STRIPS * 2);

  vkCmdEndRenderPass(m_command_buffer);
  m_vulkan_info.swap_chain->beginSwapChainRenderPass(m_command_buffer, m_vulkan_info.currentFrame,
                                                     VK_SUBPASS_CONTENTS_INLINE);
}

void OceanVulkanTexture::InitializeMipmapVertexInputAttributes() {
  // Ocean Texture
  m_ocean_texture_input_binding_attribute_descriptions.resize(2);
  m_ocean_texture_input_binding_attribute_descriptions[0].binding = 0;
  m_ocean_texture_input_binding_attribute_descriptions[0].stride = sizeof(math::Vector2f);
  m_ocean_texture_input_binding_attribute_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  m_ocean_texture_input_binding_attribute_descriptions[1].binding = 1;
  m_ocean_texture_input_binding_attribute_descriptions[1].stride = sizeof(BaseOceanTexture::Vertex);
  m_ocean_texture_input_binding_attribute_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;


  m_ocean_texture_input_attribute_descriptions.resize(3);
  m_ocean_texture_input_attribute_descriptions[0].binding = 0;
  m_ocean_texture_input_attribute_descriptions[0].location = 0;
  m_ocean_texture_input_attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  m_ocean_texture_input_attribute_descriptions[0].offset = 0;

  m_ocean_texture_input_attribute_descriptions[2].binding = 1;
  m_ocean_texture_input_attribute_descriptions[2].location = 1;
  m_ocean_texture_input_attribute_descriptions[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  m_ocean_texture_input_attribute_descriptions[2].offset = offsetof(BaseOceanTexture::Vertex, rgba);

  m_ocean_texture_input_attribute_descriptions[1].binding = 1;
  m_ocean_texture_input_attribute_descriptions[1].location = 2;
  m_ocean_texture_input_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  m_ocean_texture_input_attribute_descriptions[1].offset = offsetof(BaseOceanTexture::Vertex, s);


  // Mipmap
  m_ocean_texture_mipmap_input_binding_attribute_description.binding = 0;
  m_ocean_texture_mipmap_input_binding_attribute_description.stride = sizeof(MipMap::Vertex);
  m_ocean_texture_mipmap_input_binding_attribute_description.inputRate =
      VK_VERTEX_INPUT_RATE_VERTEX;

  m_ocean_texture_mipmap_input_attribute_descriptions.resize(2);
  m_ocean_texture_mipmap_input_attribute_descriptions[0].binding = 0;
  m_ocean_texture_mipmap_input_attribute_descriptions[0].location = 0;
  m_ocean_texture_mipmap_input_attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  m_ocean_texture_mipmap_input_attribute_descriptions[0].offset = offsetof(MipMap::Vertex, x);

  m_ocean_texture_mipmap_input_attribute_descriptions[1].binding = 0;
  m_ocean_texture_mipmap_input_attribute_descriptions[1].location = 1;
  m_ocean_texture_mipmap_input_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  m_ocean_texture_mipmap_input_attribute_descriptions[1].offset = offsetof(MipMap::Vertex, s);
}

void OceanVulkanTexture::CreatePipelineLayout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo oceanTexturePipelineLayoutInfo{};
  oceanTexturePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  oceanTexturePipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  oceanTexturePipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  m_device->createPipelineLayout(&oceanTexturePipelineLayoutInfo, nullptr,
                                 &m_ocean_texture_pipeline_layout);

  VkPipelineLayoutCreateInfo oceanTextureMipmapPipelineLayoutInfo{};
  oceanTextureMipmapPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  oceanTextureMipmapPipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  oceanTextureMipmapPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  VkPushConstantRange pushConstantVertexRange = {};
  pushConstantVertexRange.offset = 0;
  pushConstantVertexRange.size = sizeof(float);
  pushConstantVertexRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkPushConstantRange pushConstantFragmentRange = {};
  pushConstantFragmentRange.offset = pushConstantVertexRange.size;
  pushConstantFragmentRange.size = sizeof(float);
  pushConstantFragmentRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkPushConstantRange, 2> pushConstantRanges = {pushConstantVertexRange,
                                                           pushConstantFragmentRange};

  oceanTextureMipmapPipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
  oceanTextureMipmapPipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

  m_device->createPipelineLayout(&oceanTextureMipmapPipelineLayoutInfo, nullptr,
                                 &m_ocean_texture_mipmap_pipeline_layout);
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

  if (shaderId == ShaderId::OCEAN_TEXTURE) {
    m_pipeline_info.pipelineLayout = m_ocean_texture_pipeline_layout;
    m_pipeline_info.attributeDescriptions = m_ocean_texture_input_attribute_descriptions;
    m_pipeline_info.bindingDescriptions = m_ocean_texture_input_binding_attribute_descriptions;
  } else {
    m_pipeline_info.pipelineLayout = m_ocean_texture_mipmap_pipeline_layout;
    m_pipeline_info.attributeDescriptions = m_ocean_texture_mipmap_input_attribute_descriptions;
    m_pipeline_info.bindingDescriptions = {
        m_ocean_texture_mipmap_input_binding_attribute_description};
  }

  m_pipeline_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

void OceanVulkanTexture::InitializeVertexBuffer() {
  std::vector<MipMap::Vertex> vertices = {
      {-1, -1, 0, 0}, {-1, 1, 0, 1}, {1, -1, 1, 0}, {1, 1, 1, 1}};

  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, sizeof(vertices), 1, 1);
  m_vertex_buffer->writeToGpuBuffer(vertices.data());

  VkPipelineInputAssemblyStateCreateInfo& inputAssembly = m_pipeline_info.inputAssemblyInfo;
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo& depthStencil = m_pipeline_info.depthStencilInfo;
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;

  VkPipelineColorBlendAttachmentState& colorBlendAttachment = m_pipeline_info.colorBlendAttachment;
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkSamplerCreateInfo& sampler_create_info = m_sampler_helper.GetSamplerCreateInfo();
  sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.anisotropyEnable = VK_TRUE;

  sampler_create_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  sampler_create_info.unnormalizedCoordinates = VK_FALSE;
  sampler_create_info.compareEnable = VK_FALSE;
  sampler_create_info.compareOp = VK_COMPARE_OP_ALWAYS;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  sampler_create_info.mipLodBias = 0.0f;

  sampler_create_info.magFilter = VK_FILTER_LINEAR;
  sampler_create_info.minFilter = VK_FILTER_LINEAR;

  m_sampler_helper.CreateSampler();
}

void OceanVulkanTexture::move_existing_to_vram(u32 slot_addr) {
  m_vulkan_info.texture_pool->move_existing_to_vram(m_tex0_gpu, slot_addr);
}

void OceanVulkanTexture::setup_framebuffer_context(int) {}
