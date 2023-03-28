#include "EyeRenderer.h"

#include "common/util/FileUtil.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

/////////////////////////
// Bucket Renderer
/////////////////////////
// note: eye texture increased to 128x128 (originally 32x32) here.
EyeVulkanRenderer::GpuEyeTex::GpuEyeTex(std::unique_ptr<GraphicsDeviceVulkan>& device) : fb(128, 128, VK_FORMAT_A8B8G8R8_SRGB_PACK32, device) {
  VkSamplerCreateInfo& samplerInfo =
      fb.GetSamplerHelper().GetSamplerCreateInfo();
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = device->getPhysicalDeviceFeatures().samplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;

  fb.GetSamplerHelper().GetSampler();
}

EyeVulkanRenderer::EyeVulkanRenderer(const std::string& name,
                         int id,
                         std::unique_ptr<GraphicsDeviceVulkan>& device,
                         VulkanInitializationInfo& vulkan_info)
    : BaseEyeRenderer(name, id), BucketVulkanRenderer(device, vulkan_info) {
  m_eye_renderer_extents = VkExtent2D{128, 128};

  init_shaders();
  InitializeInputVertexAttribute();

  for (uint32_t i = 0; i < NUM_EYE_PAIRS * 2; i++) {
    m_gpu_eye_textures[i] = std::make_unique<EyeVulkanRenderer::GpuEyeTex>(m_device);
  }

  VkDeviceSize device_size = sizeof(float) * VTX_BUFFER_FLOATS;
  m_gpu_vertex_buffer = std::make_unique<VertexBuffer>(
      m_device, device_size, 1, 1);

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
          .build();

  create_pipeline_layout();
}

void EyeVulkanRenderer::init_shaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::EYE];

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

void EyeVulkanRenderer::create_pipeline_layout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void EyeVulkanRenderer::init_textures(VulkanTexturePool& texture_pool) {
  // set up eyes
  for (int pair_idx = 0; pair_idx < NUM_EYE_PAIRS; pair_idx++) {
    for (int lr = 0; lr < 2; lr++) {
      u32 tidx = pair_idx * 2 + lr;
      u32 tbp = EYE_BASE_BLOCK + pair_idx * 2 + lr;
      VulkanTextureInput in;
      //in.texture = &m_gpu_eye_textures[tidx]->fb.Texture();
      in.debug_page_name = "PC-EYES";
      in.debug_name = fmt::format("{}-eye-gpu-{}", lr ? "left" : "right", pair_idx);
      in.id = texture_pool.allocate_pc_port_texture(m_vulkan_info.m_version);
      m_gpu_eye_textures[tidx]->gpu_texture = texture_pool.give_texture_and_load_to_vram(in, tbp);
      m_gpu_eye_textures[tidx]->tbp = tbp;
    }
  }
}

void EyeVulkanRenderer::render(DmaFollower& dma,
            SharedVulkanRenderState* render_state,
            ScopedProfilerNode& prof) {
  BaseEyeRenderer::render(dma, render_state, prof);
};

void EyeVulkanRenderer::InitializeInputVertexAttribute() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(float) * 4;
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[0].offset = 0;
  m_pipeline_config_info.attributeDescriptions.push_back(attributeDescriptions[0]);
}

void EyeVulkanRenderer::setup_draws(DmaFollower& dma, BaseSharedRenderState* render_state) {
  // now, loop over eyes. end condition is a 8 qw transfer to restore gs.
  unsigned index = 0;
  m_eye_draw_map.clear();

  while (dma.current_tag().qwc != 8) {
    m_eye_draw_map.insert(std::pair<uint32_t, SingleEyeDrawsVulkan>(
        index++, {m_device, m_fragment_descriptor_layout, m_vulkan_info}));
    m_eye_draw_map.insert(std::pair<uint32_t, SingleEyeDrawsVulkan>(
        index++, {m_device, m_fragment_descriptor_layout, m_vulkan_info}));

    auto& l_draw = m_eye_draw_map.at(m_eye_draw_map.size() - 2);
    auto& r_draw = m_eye_draw_map.at(m_eye_draw_map.size() - 1);

    l_draw.lr = 0;
    r_draw.lr = 1;

    // eye background setup
    auto adgif0_dma = dma.read_and_advance();
    ASSERT(adgif0_dma.size_bytes == 96);  // 5 adgifs a+d's plus tag
    ASSERT(adgif0_dma.vif0() == 0);
    ASSERT(adgif0_dma.vifcode1().kind == VifCode::Kind::DIRECT);
    AdgifHelper adgif0(adgif0_dma.data + 16);
    auto tex0 = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(adgif0.tex0().tbp0());
    VulkanTexture* vulkan_texture = tex0->get_selected_texture();

    u32 pair_idx = -1;
    // first draw. this is the background. It reads 0,0 of the texture uses that color everywhere.
    // we'll also figure out the eye index here.
    bool using_64 = false;
    {
      auto draw0 = read_eye_draw(dma);
      ASSERT(draw0.sprite.uv0[0] == 0);
      ASSERT(draw0.sprite.uv0[1] == 0);
      ASSERT(draw0.sprite.uv1[0] == 0);
      ASSERT(draw0.sprite.uv1[1] == 0);
      if (draw0.scissor.y1 - draw0.scissor.y0 == 63) {
        using_64 = true;
        l_draw.using_64 = true;
        r_draw.using_64 = true;
      }
      u32 y0 = (draw0.sprite.xyz0[1] - 512) >> 4;
      if (using_64) {
        y0 = (draw0.sprite.xyz0[1] - 1024) >> 5;
        y0 *= 4;
      }
      pair_idx = y0 / SINGLE_EYE_SIZE;
      l_draw.pair = pair_idx;
      r_draw.pair = pair_idx;
      if (tex0) {
        StagingBuffer stagingBuffer{
            m_device, vulkan_texture->getMemorySize(), 1, VK_BUFFER_USAGE_TRANSFER_DST_BIT};

        vulkan_texture->getImageData(stagingBuffer.getBuffer(), vulkan_texture->getWidth(),
                                     vulkan_texture->getHeight(), 0, 0);
        stagingBuffer.map();
        void* imageData = stagingBuffer.getMappedMemory();

        u32 tex_val;
        memcpy(&tex_val, imageData, 4);
        l_draw.clear_color = tex_val;
        r_draw.clear_color = tex_val;
        stagingBuffer.unmap();
      } else {
        l_draw.clear_color = 0;
        r_draw.clear_color = 0;
      }
    }

    // up next is the pupil background
    {
      l_draw.iris = read_eye_draw(dma);
      l_draw.iris_vulkan_graphics.texture = tex0;

      if (dma.current_tag().qwc == 6) {
        // change adgif!
        auto r_iris_adgif = dma.read_and_advance();
        ASSERT(r_iris_adgif.size_bytes == 96);  // 5 adgifs a+d's plus tag
        ASSERT(r_iris_adgif.vif0() == 0);
        ASSERT(r_iris_adgif.vifcode1().kind == VifCode::Kind::DIRECT);
        AdgifHelper r_iris_helper(r_iris_adgif.data + 16);
        r_draw.iris = read_eye_draw(dma);
        r_draw.iris_vulkan_graphics.texture =
            m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(r_iris_helper.tex0().tbp0());
      } else {
        // same adgif
        r_draw.iris = read_eye_draw(dma);
        r_draw.iris_vulkan_graphics.texture = tex0;
      }
    }

    // now we'll draw the pupil on top of that
    auto test1 = dma.read_and_advance();
    (void)test1;
    auto adgif1_dma = dma.read_and_advance();
    ASSERT(adgif1_dma.size_bytes == 96);  // 5 adgifs a+d's plus tag
    ASSERT(adgif1_dma.vif0() == 0);
    ASSERT(adgif1_dma.vifcode1().kind == VifCode::Kind::DIRECT);
    AdgifHelper adgif1(adgif1_dma.data + 16);
    auto tex1 = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(adgif1.tex0().tbp0());

    if (tex1) {
      l_draw.pupil = read_eye_draw(dma);
      l_draw.pupil_vulkan_graphics.texture = tex1;
    }

    if (dma.current_tag().qwc == 6) {
      auto r_pupil_adgif = dma.read_and_advance();
      ASSERT(r_pupil_adgif.size_bytes == 96);  // 5 adgifs a+d's plus tag
      ASSERT(r_pupil_adgif.vif0() == 0);
      ASSERT(r_pupil_adgif.vifcode1().kind == VifCode::Kind::DIRECT);
      AdgifHelper r_pupil_helper(r_pupil_adgif.data + 16);
      r_draw.pupil = read_eye_draw(dma);
      r_draw.pupil_vulkan_graphics.texture =
          m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(r_pupil_helper.tex0().tbp0());
    } else {
      if (tex1) {
        r_draw.pupil = read_eye_draw(dma);
        r_draw.pupil_vulkan_graphics.texture = tex1;
      }
    }

    // and finally the eyelid
    auto test2 = dma.read_and_advance();
    (void)test2;
    auto adgif2_dma = dma.read_and_advance();
    ASSERT(adgif2_dma.size_bytes == 96);  // 5 adgifs a+d's plus tag
    ASSERT(adgif2_dma.vif0() == 0);
    ASSERT(adgif2_dma.vifcode1().kind == VifCode::Kind::DIRECT);
    AdgifHelper adgif2(adgif2_dma.data + 16);
    auto tex2 = m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(adgif2.tex0().tbp0());

    {
      l_draw.lid = read_eye_draw(dma);
      l_draw.lid_vulkan_graphics.texture = tex2;
    }

    if (dma.current_tag().qwc == 6) {
      auto r_lid_adgif = dma.read_and_advance();
      ASSERT(r_lid_adgif.size_bytes == 96);  // 5 adgifs a+d's plus tag
      ASSERT(r_lid_adgif.vif0() == 0);
      ASSERT(r_lid_adgif.vifcode1().kind == VifCode::Kind::DIRECT);
      AdgifHelper r_lid_helper(r_lid_adgif.data + 16);
      r_draw.lid = read_eye_draw(dma);
      r_draw.lid_vulkan_graphics.texture =
          m_vulkan_info.texture_pool->lookup_vulkan_gpu_texture(r_lid_helper.tex0().tbp0());
    } else {
      r_draw.lid = read_eye_draw(dma);
      r_draw.lid_vulkan_graphics.texture = tex2;
    }

    if (render_state->version == GameVersion::Jak1) {
      auto end = dma.read_and_advance();
      ASSERT(end.size_bytes == 0);
      ASSERT(end.vif0() == 0);
      ASSERT(end.vif1() == 0);
    }
  }
}

void EyeVulkanRenderer::run_gpu(BaseSharedRenderState* render_state) {
  if (m_eye_draw_map.empty()) {
    return;
  }

  //glBindBuffer(GL_ARRAY_BUFFER, m_gl_vertex_buffer);
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;


  // the first thing we'll do is prepare the vertices
  StagingBuffer vertexStagingBuffer(m_device, sizeof(float) * VTX_BUFFER_FLOATS, 1,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  m_device->copyBuffer(m_gpu_vertex_buffer->getBuffer(), vertexStagingBuffer.getBuffer(),
                       sizeof(float) * VTX_BUFFER_FLOATS);

  vertexStagingBuffer.map();
  float* vertex_data = reinterpret_cast<float*>(vertexStagingBuffer.getMappedMemory());

  int buffer_idx = 0;
  for (const auto& [index, draw] : m_eye_draw_map) {
    if (draw.using_64) {
      buffer_idx =
          add_draw_to_buffer_64(buffer_idx, draw.iris, vertex_data, draw.pair, draw.lr);
      buffer_idx =
          add_draw_to_buffer_64(buffer_idx, draw.pupil, vertex_data, draw.pair, draw.lr);
      buffer_idx =
          add_draw_to_buffer_64(buffer_idx, draw.lid, vertex_data, draw.pair, draw.lr);
    } else {
      buffer_idx =
          add_draw_to_buffer_32(buffer_idx, draw.iris, vertex_data, draw.pair, draw.lr);
      buffer_idx =
          add_draw_to_buffer_32(buffer_idx, draw.pupil, vertex_data, draw.pair, draw.lr);
      buffer_idx =
          add_draw_to_buffer_32(buffer_idx, draw.lid, vertex_data, draw.pair, draw.lr);
    }
  }
  ASSERT(buffer_idx <= VTX_BUFFER_FLOATS);
  int check = buffer_idx;

  m_device->copyBuffer(vertexStagingBuffer.getBuffer(), m_gpu_vertex_buffer->getBuffer(),
                     sizeof(float) * VTX_BUFFER_FLOATS);
  vertexStagingBuffer.unmap();

  //We store separate frame buffers in vulkan swap chain abstractions
  auto& frame_buffer_texture_pair = m_gpu_eye_textures[m_eye_draw_map.at(0).tex_slot()]->fb;

  vkCmdEndRenderPass(m_vulkan_info.render_command_buffer);
  frame_buffer_texture_pair.beginSwapChainRenderPass(m_vulkan_info.render_command_buffer);
  m_pipeline_config_info.renderPass = frame_buffer_texture_pair.GetRenderPass();

  buffer_idx = 0;
  for (size_t draw_idx = 0; draw_idx < m_eye_draw_map.size(); draw_idx++) {
    auto& draw = m_eye_draw_map.at(draw_idx);
    const auto& out_tex = m_gpu_eye_textures[draw.tex_slot()];

    // first, the clear
    float clear[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
      clear[i] = ((draw.clear_color >> (8 * i)) & 0xff) / 255.f;
      m_pipeline_config_info.colorBlendInfo.blendConstants[0] = clear[i];
    }

    // iris
    if (draw.iris_vulkan_graphics.texture) {
      // set alpha
      // set Z
      // set texture
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      //draw.iris_gl_tex;
      draw.iris_vulkan_graphics.descriptor_image_info =
          VkDescriptorImageInfo{frame_buffer_texture_pair.GetSamplerHelper().GetSampler(),
                                draw.iris_vulkan_graphics.texture->get_selected_texture()->getImageView(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      frame_buffer_texture_pair.setViewportScissor(m_vulkan_info.render_command_buffer);
      ExecuteVulkanDraw(m_vulkan_info.render_command_buffer, draw.iris_vulkan_graphics, buffer_idx / 4, 4);
    }
    buffer_idx += 4 * 4;

    if (draw.pupil_vulkan_graphics.texture) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
      m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
      m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

      m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

      m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

      //draw.pupil_gl_tex;
      draw.pupil_vulkan_graphics.descriptor_image_info =
          VkDescriptorImageInfo{frame_buffer_texture_pair.GetSamplerHelper().GetSampler(),
                                draw.pupil_vulkan_graphics.texture->get_selected_texture()->getImageView(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      frame_buffer_texture_pair.setViewportScissor(m_vulkan_info.render_command_buffer);
      ExecuteVulkanDraw(m_vulkan_info.render_command_buffer, draw.pupil_vulkan_graphics,
                        buffer_idx / 4, 4);
    }
    buffer_idx += 4 * 4;

    if (draw.lid_vulkan_graphics.texture) {
      m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
      //draw.lid_gl_tex;
      draw.lid_vulkan_graphics.descriptor_image_info =
          VkDescriptorImageInfo{frame_buffer_texture_pair.GetSamplerHelper().GetSampler(),
                                draw.lid_vulkan_graphics.texture->get_selected_texture()->getImageView(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      frame_buffer_texture_pair.setViewportScissor(m_vulkan_info.render_command_buffer);
      ExecuteVulkanDraw(m_vulkan_info.render_command_buffer, draw.lid_vulkan_graphics,
                        buffer_idx / 4, 4);
      //glDrawArrays(GL_TRIANGLE_STRIP, buffer_idx / 4, 4);
    }
    buffer_idx += 4 * 4;

    // finally, give to "vram"
    m_vulkan_info.texture_pool->move_existing_to_vram(out_tex->gpu_texture, out_tex->tbp);
  }

  vkCmdEndRenderPass(m_vulkan_info.render_command_buffer);
  m_vulkan_info.swap_chain->beginSwapChainRenderPass(m_vulkan_info.render_command_buffer, m_vulkan_info.currentFrame);

  ASSERT(check == buffer_idx);
}

std::optional<u64> EyeVulkanRenderer::lookup_eye_texture(u8 eye_id) {
  eye_id = (eye_id % 40);
  if ((s32)eye_id >= NUM_EYE_PAIRS * 2) {
    fmt::print("lookup eye failed for {} (1)\n", eye_id);
    return {};
  }
  //auto* gpu_tex = m_gpu_eye_textures[eye_id].gpu_tex;
  //if (gpu_tex) {
  //  return gpu_tex->gpu_textures.at(0).gl;
  //} else {
  //  fmt::print("lookup eye failed for {}\n", eye_id);
  //  return {};
  //}
  return {};
}

void EyeVulkanRenderer::ExecuteVulkanDraw(VkCommandBuffer commandBuffer,
                                          EyeVulkanGraphics& eye,
                                          uint32_t firstVertex,
                                          uint32_t vertexCount) {
  auto& write_descriptors_info = eye.descriptor_writer.getWriteDescriptorSets();
  write_descriptors_info[0] = eye.descriptor_writer.writeImageDescriptorSet(
      0, &eye.descriptor_image_info, 1);

  eye.descriptor_writer.overwrite(eye.descriptor_set);

  eye.pipeline_layout.createGraphicsPipeline(m_pipeline_config_info);
  eye.pipeline_layout.bind(commandBuffer);

  VkDeviceSize offsets[] = {0};
  VkBuffer vertex_buffer_vulkan = m_gpu_vertex_buffer->getBuffer();
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertex_buffer_vulkan, offsets);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline_config_info.pipelineLayout, 0, 1, &eye.descriptor_set, 0,
                          nullptr);

  vkCmdDraw(commandBuffer, vertexCount, 0, firstVertex, 0);
}

void EyeVulkanRenderer::run_dma_draws_in_gpu(DmaFollower& dma, BaseSharedRenderState* render_state) {
  setup_draws(dma, render_state);
  run_gpu(render_state);
}

EyeVulkanRenderer::~EyeVulkanRenderer() {
}
