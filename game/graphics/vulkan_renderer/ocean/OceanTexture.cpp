#include "OceanTexture.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

OceanVulkanTexture::OceanVulkanTexture(bool generate_mipmaps,
                           std::unique_ptr<GraphicsDeviceVulkan>& device,
                           VulkanInitializationInfo& vulkan_info)
    : BaseOceanTexture(m_generate_mipmaps),
      m_device(device),
      m_vulkan_info(vulkan_info),
      m_result_texture(TEX0_SIZE,
                       TEX0_SIZE,
                       VK_FORMAT_R8G8B8A8_UNORM,
                       device,
                       m_generate_mipmaps ? NUM_MIPS : 1),
      m_temp_texture(TEX0_SIZE, TEX0_SIZE, VK_FORMAT_R8G8B8A8_UNORM, device) {
  m_dbuf_x = m_dbuf_a;
  m_dbuf_y = m_dbuf_b;

  m_tbuf_x = m_tbuf_a;
  m_tbuf_y = m_tbuf_b;

  GraphicsPipelineLayout::defaultPipelineConfigInfo(m_pipeline_info);
  InitializeMipmapVertexInputAttributes();

  init_pc();
  SetupShader(ShaderId::OCEAN_TEXTURE);

  m_mipmap_graphics_layouts.resize(NUM_MIPS, {m_device});

  m_common_uniform_fragment_buffer =
      std::make_unique<CommonOceanFragmentUniformBuffer>(m_device, 1, 1);

  m_vulkan_pc.static_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(math::Vector2f), NUM_VERTS);
  m_vulkan_pc.dynamic_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(Vertex), NUM_VERTS, 1);
  m_vulkan_pc.graphics_index_buffer =
      std::make_unique<IndexBuffer>(m_device, sizeof(u32), NUM_VERTS * 2, 1); //Allocated extra size to account for primitive restarts in index buffer

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_ocean_texture_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);
  m_ocean_texture_mipmap_graphics_pipeline_layout = std::make_unique<GraphicsPipelineLayout>(m_device);

  CreatePipelineLayout();
  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);
  VkDescriptorSetLayout descriptorSetLayout =
      m_fragment_descriptor_layout->getDescriptorSetLayout();
  m_vulkan_info.descriptor_pool->allocateDescriptor(&descriptorSetLayout,
                                                    &m_ocean_texture_descriptor_set);
  m_vulkan_info.descriptor_pool->allocateDescriptor(&descriptorSetLayout,
                                                    &m_ocean_mipmap_texture_descriptor_set);

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
    ImGui::Image((void*)vulkan_texture->GetTextureId(), ImVec2(vulkan_texture->getWidth(), vulkan_texture->getHeight()));
  }
}

OceanVulkanTexture::~OceanVulkanTexture() {
  destroy_pc();
}

void OceanVulkanTexture::init_textures(VulkanTexturePool& pool) {
  VulkanTextureInput in;
  //in.texture = &m_result_texture.Texture();
  in.debug_page_name = "PC-OCEAN";
  in.debug_name = fmt::format("pc-ocean-mip-{}", m_generate_mipmaps);
  in.id = pool.allocate_pc_port_texture(m_vulkan_info.m_version);
  m_tex0_gpu = pool.give_texture_and_load_to_vram(in, ocean_common::OCEAN_TEX_TBP_JAK1);
}

void OceanVulkanTexture::handle_ocean_texture(DmaFollower& dma,
                                        BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {
  if (render_state->version == GameVersion::Jak1) {
    BaseOceanTexture::handle_ocean_texture_jak1(dma, render_state, prof);
  } else if (render_state->version == GameVersion::Jak2) {
    BaseOceanTexture::handle_ocean_texture_jak2(dma, render_state, prof);
  } else {
    assert(false);
  }
}

/*!
 * Generate mipmaps for the ocean texture.
 * There's a trick here - we reduce the intensity of alpha on the lower lods. This lets texture
 * filtering slowly fade the alpha value out to 0 with distance.
 */
void OceanVulkanTexture::make_texture_with_mipmaps(BaseSharedRenderState* render_state,
                                             ScopedProfilerNode& prof) {
  return;

  SetupShader(ShaderId::OCEAN_TEXTURE_MIPMAP);
  VkSampleCountFlagBits original_sample = m_pipeline_info.multisampleInfo.rasterizationSamples;
  m_pipeline_info.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  m_descriptor_image_info =
      VkDescriptorImageInfo{m_sampler_helper.GetSampler(), m_temp_texture.Texture().getImageView(),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[0] =
      m_fragment_descriptor_writer->writeImageDescriptorSet(0, &m_descriptor_image_info);

  m_fragment_descriptor_writer->overwrite(m_ocean_mipmap_texture_descriptor_set);

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_info.pipelineLayout, 0, 1, &m_ocean_mipmap_texture_descriptor_set, 0,
                          NULL);

  vkCmdEndRenderPass(m_vulkan_info.render_command_buffer);
  for (int i = 0; i < NUM_MIPS; i++) {
    //FramebufferVulkanTexturePair ctxt(m_result_texture, i);
    m_pipeline_info.renderPass = m_result_texture.GetRenderPass(i);
    m_result_texture.beginRenderPass(m_vulkan_info.render_command_buffer, i);
    float alpha_intensity = std::max(0.f, 1.f - 0.51f * i);
    float scale = 1.f / (1 << i);

    m_mipmap_graphics_layouts[i].createGraphicsPipeline(m_pipeline_info);
    m_mipmap_graphics_layouts[i].bind(m_vulkan_info.render_command_buffer);
  
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(scale),
                       (void*)&scale);
    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_info.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(scale), sizeof(alpha_intensity),
                       (void*)&alpha_intensity);

    vkCmdDraw(m_vulkan_info.render_command_buffer, 4, 1, 0, 0);

    vkCmdEndRenderPass(m_vulkan_info.render_command_buffer);
    prof.add_draw_call();
    prof.add_tri(2);
  }
  m_pipeline_info.multisampleInfo.rasterizationSamples = original_sample;
  m_pipeline_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_vulkan_info.swap_chain->beginSwapChainRenderPass(m_vulkan_info.render_command_buffer,
                                                     m_vulkan_info.currentFrame);
}

void OceanVulkanTexture::flush(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  ASSERT(m_pc.vtx_idx == 2112);
  return;

  m_pipeline_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_info.multisampleInfo.rasterizationSamples = m_device->getMsaaCount();

  m_vulkan_pc.dynamic_vertex_buffer->writeToGpuBuffer(m_pc.vertex_dynamic.data());
  m_vulkan_pc.graphics_index_buffer->writeToGpuBuffer(m_pc.index_buffer.data(), m_pc.index_buffer.size());

  VkDeviceSize offsets[] = {0, 0};
  std::array<VkBuffer, 2> vertex_buffers = {m_vulkan_pc.dynamic_vertex_buffer->getBuffer(),
                                            m_vulkan_pc.static_vertex_buffer->getBuffer()};
  vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, vertex_buffers.size(), vertex_buffers.data(), offsets);

  vkCmdBindIndexBuffer(m_vulkan_info.render_command_buffer,
                       m_vulkan_pc.graphics_index_buffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

  SetupShader(ShaderId::OCEAN_TEXTURE);

  m_ocean_texture_graphics_pipeline_layout->createGraphicsPipeline(m_pipeline_info);
  m_ocean_texture_graphics_pipeline_layout->bind(m_vulkan_info.render_command_buffer);

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
      VkDescriptorImageInfo{m_sampler_helper.GetSampler(), m_temp_texture.Texture().getImageView(),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
  write_descriptors_info[0] =
      m_fragment_descriptor_writer->writeImageDescriptorSet(0, &m_descriptor_image_info);

  m_fragment_descriptor_writer->overwrite(m_ocean_texture_descriptor_set);

  vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        m_pipeline_info.pipelineLayout, 0, 1,
                        &m_ocean_texture_descriptor_set, 0,
                        NULL);

  vkCmdDraw(m_vulkan_info.render_command_buffer, NUM_VERTS, 1, 0, 0);
  vkCmdDrawIndexed(m_vulkan_info.render_command_buffer, m_pc.index_buffer.size(), 1, 0, 0, 0);

  prof.add_draw_call();
  prof.add_tri(NUM_STRIPS * NUM_STRIPS * 2);
}

void OceanVulkanTexture::InitializeMipmapVertexInputAttributes() {
  //Ocean Texture
  m_ocean_texture_input_binding_attribute_descriptions.resize(2);
  m_ocean_texture_input_binding_attribute_descriptions[0].binding = 0;
  m_ocean_texture_input_binding_attribute_descriptions[0].stride = sizeof(BaseOceanTexture::Vertex);
  m_ocean_texture_input_binding_attribute_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  m_ocean_texture_input_binding_attribute_descriptions[1].binding = 1;
  m_ocean_texture_input_binding_attribute_descriptions[1].stride = sizeof(math::Vector2f);
  m_ocean_texture_input_binding_attribute_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  m_ocean_texture_input_attribute_descriptions.resize(3);
  m_ocean_texture_input_attribute_descriptions[0].binding = 0;
  m_ocean_texture_input_attribute_descriptions[0].location = 0;
  m_ocean_texture_input_attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  m_ocean_texture_input_attribute_descriptions[0].offset = offsetof(BaseOceanTexture::Vertex, s);

  m_ocean_texture_input_attribute_descriptions[1].binding = 0;
  m_ocean_texture_input_attribute_descriptions[1].location = 1;
  m_ocean_texture_input_attribute_descriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  m_ocean_texture_input_attribute_descriptions[1].offset = offsetof(BaseOceanTexture::Vertex, rgba);

  m_ocean_texture_input_attribute_descriptions[2].binding = 1;
  m_ocean_texture_input_attribute_descriptions[2].location = 2;
  m_ocean_texture_input_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  m_ocean_texture_input_attribute_descriptions[2].offset = 0;


  //Mipmap
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
  oceanTexturePipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  oceanTexturePipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &oceanTexturePipelineLayoutInfo,
                             nullptr, &m_ocean_texture_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

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

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &oceanTextureMipmapPipelineLayoutInfo,
                             nullptr, &m_ocean_texture_mipmap_pipeline_layout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
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
    m_pipeline_info.bindingDescriptions = {m_ocean_texture_mipmap_input_binding_attribute_description};
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

void OceanVulkanTexture::set_gpu_texture(TextureInput&) {
}

void OceanVulkanTexture::move_existing_to_vram(u32 slot_addr) {
  m_vulkan_info.texture_pool->move_existing_to_vram(m_tex0_gpu, slot_addr);
}

void OceanVulkanTexture::setup_framebuffer_context(int) {
}
