#include "SkyBlendGPU.h"

#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

SkyBlendVulkanGPU::SkyBlendVulkanGPU(std::shared_ptr<GraphicsDeviceVulkan> device,
                                     VulkanInitializationInfo& vulkan_info)
    : m_device(device), m_vulkan_info(vulkan_info) {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  m_push_constant.height_scale = 0.5;
  m_push_constant.scissor_adjust = -512.0 / 416.0;

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);

  create_pipeline_layout();

  // generate textures for sky blending
  auto& shader = m_vulkan_info.shaders[ShaderId::SKY_BLEND];
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

  // setup the framebuffers
  for (int i = 0; i < 2; i++) {
    m_sampler_helpers[i] = std::make_unique<VulkanSamplerHelper>(m_device);
    m_framebuffers[i] = std::make_unique<FramebufferVulkanHelper>(
        m_sizes[i], m_sizes[i], VK_FORMAT_R8G8B8A8_UNORM, m_device);

    VkSamplerCreateInfo& samplerInfo = m_sampler_helpers[i]->GetSamplerCreateInfo();
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = m_device->getMaxSamplerAnisotropy();
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // samplerInfo.maxLod = static_cast<float>(mipLevels);
    samplerInfo.mipLodBias = 0.0f;

    // ST was used in OpenGL, UV is used in Vulkan
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;

    m_sampler_helpers[i]->CreateSampler();
  }

  VkDeviceSize m_vertex_device_size = sizeof(Vertex) * 6;
  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, m_vertex_device_size, 1, 1);

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
  attributeDescriptions[0].offset = 0;
  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescriptions[0]);

  // we only draw squares
  m_vertex_buffer->writeToGpuBuffer(m_vertex_data, sizeof(m_vertex_data), 0);
}

void SkyBlendVulkanGPU::create_pipeline_layout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

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

  m_device->createPipelineLayout(&pipelineLayoutInfo, nullptr,
                                 &m_pipeline_config_info.pipelineLayout);
}

SkyBlendVulkanGPU::~SkyBlendVulkanGPU() {}

void SkyBlendVulkanGPU::init_textures(VulkanTexturePool& tex_pool) {
  for (int i = 0; i < 2; i++) {
    VulkanTextureInput in;
    in.texture = &m_framebuffers[i]->ColorAttachmentTexture();
    in.debug_name = fmt::format("PC-SKY-GPU-{}", i);
    in.id = tex_pool.allocate_pc_port_texture();
    u32 tbp = SKY_TEXTURE_VRAM_ADDRS[i];
    m_tex_info[i] = {tex_pool.give_texture_and_load_to_vram(in, tbp), tbp};
  }
}

SkyBlendStats SkyBlendVulkanGPU::do_sky_blends(DmaFollower& dma,
                                               BaseSharedRenderState* render_state,
                                               ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  SkyBlendStats stats;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  m_vulkan_info.swap_chain->endSwapChainRenderPass(command_buffer);

  while (dma.current_tag().qwc == 6) {
    // assuming that the vif and gif-tag is correct
    auto setup_data = dma.read_and_advance();

    // first is an adgif
    AdgifHelper adgif(setup_data.data + 16);
    ASSERT(adgif.is_normal_adgif());
    ASSERT(adgif.alpha().data == 0x8000000068);  // Cs + Cd

    // next is the actual draw
    auto draw_data = dma.read_and_advance();
    ASSERT(draw_data.size_bytes == 6 * 16);

    GifTag draw_or_blend_tag(draw_data.data);

    // the first draw overwrites the previous frame's draw by disabling alpha blend (ABE = 0)
    bool is_first_draw = !GsPrim(draw_or_blend_tag.prim()).abe();

    // here's we're relying on the format of the drawing to get the alpha/offset.
    u32 coord;
    u32 intensity;
    memcpy(&coord, draw_data.data + (5 * 16), 4);
    memcpy(&intensity, draw_data.data + 16, 4);

    // we didn't parse the render-to-texture setup earlier, so we need a way to tell sky from
    // clouds. we can look at the drawing coordinates to tell - the sky is smaller than the clouds.
    int buffer_idx = 0;
    if (coord == 0x200) {
      // sky
      buffer_idx = 0;
    } else if (coord == 0x400) {
      buffer_idx = 1;
    } else {
      ASSERT(false);  // bad data
    }

    // look up the source texture
    auto tex = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(adgif.tex0().tbp0())
                   ->get_selected_texture();
    ASSERT(tex);

    // if the first is set, it disables alpha. we can just clear here, so it's easier to find
    // in renderdoc.
    if (is_first_draw) {
      float clear[4] = {0, 0, 0, 0};
      // glClearBufferfv(GL_COLOR, 0, clear);
    }

    // TODO: Are there different clear values for other draw calls
    m_framebuffers[buffer_idx]->beginRenderPass(command_buffer);
    m_framebuffers[buffer_idx]->setViewportScissor(command_buffer);

    // intensities should be 0-128 (maybe higher is okay, but I don't see how this could be
    // generated with the GOAL code.)
    ASSERT(intensity <= 128);

    // todo - could do this on the GPU, but probably not worth it for <20 triangles...
    float intensity_float = intensity / 128.f;
    for (auto& vert : m_vertex_data) {
      vert.intensity = intensity_float;
    }

    m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_FALSE;
    m_vertex_buffer->writeToGpuBuffer(m_vertex_data);

    VkDeviceSize offsets[] = {0};
    VkBuffer vertex_buffers = m_vertex_buffer->getBuffer();
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffers, offsets);

    m_descriptor_image_infos[buffer_idx] =
        VkDescriptorImageInfo{m_sampler_helpers[buffer_idx]->GetSampler(), tex->getImageView(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    auto& write_descriptors_info = m_fragment_descriptor_writer->getWriteDescriptorSets();
    write_descriptors_info[0] = m_fragment_descriptor_writer->writeImageDescriptorSet(
        1, &m_descriptor_image_infos[buffer_idx]);

    m_fragment_descriptor_writer->overwrite(m_fragment_descriptor_sets[buffer_idx]);

    m_graphics_pipeline_layout.updateGraphicsPipeline(command_buffer,
                                                      m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(command_buffer);

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, 1,
                            &m_fragment_descriptor_sets[buffer_idx], 0, NULL);

    // Draw a sqaure
    vkCmdDraw(command_buffer, 6, 1, 0, 0);

    // 1 draw, 2 triangles
    prof.add_draw_call(1);
    prof.add_tri(2);

    m_vulkan_info.texture_pool->move_existing_to_vram(m_tex_info[buffer_idx].tex,
                                                      m_tex_info[buffer_idx].tbp);

    if (buffer_idx == 0) {
      if (is_first_draw) {
        stats.sky_draws++;
      } else {
        stats.sky_blends++;
      }
    } else {
      if (is_first_draw) {
        stats.cloud_draws++;
      } else {
        stats.cloud_blends++;
      }
    }

    vkCmdEndRenderPass(command_buffer);
  }

  m_vulkan_info.swap_chain->beginSwapChainRenderPass(command_buffer,
                                                     m_vulkan_info.currentFrame);

  return stats;
}
