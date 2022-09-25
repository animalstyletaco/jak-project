#include "SkyBlendGPU.h"

#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"

SkyBlendGPU::SkyBlendGPU(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device) {
  // generate textures for sky blending

  // setup the framebuffers
  for (int i = 0; i < 2; i++) {
    m_textures[i] = std::make_unique<TextureInfo>(m_device);
    VkExtent3D extents{m_sizes[i], m_sizes[i], 1};
    m_textures[i]->CreateImage(
        extents, 1, VK_IMAGE_TYPE_2D,
        device->getMsaaCount(), VK_FORMAT_A8B8G8R8_SNORM_PACK32, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_textures[i]->CreateImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SNORM_PACK32,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1);

    //glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_textures[i], 0);
    //GLenum draw_buffers[1] = {GL_COLOR_ATTACHMENT0};
    //glDrawBuffers(1, draw_buffers);
  }

  //glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6, nullptr, GL_DYNAMIC_DRAW);

  VkDeviceSize m_vertex_device_size = sizeof(Vertex) * 6;
  m_vertex_buffer = std::make_unique<VertexBuffer>(m_device, m_vertex_device_size, 1, 
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
  // TODO: This value needs to be normalized
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = 0;

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  // we only draw squares
  m_vertex_data[0].x = 0;
  m_vertex_data[0].y = 0;

  m_vertex_data[1].x = 1;
  m_vertex_data[1].y = 0;

  m_vertex_data[2].x = 0;
  m_vertex_data[2].y = 1;

  m_vertex_data[3].x = 1;
  m_vertex_data[3].y = 0;

  m_vertex_data[4].x = 0;
  m_vertex_data[4].y = 1;

  m_vertex_data[5].x = 1;
  m_vertex_data[5].y = 1;
}

SkyBlendGPU::~SkyBlendGPU() {
}

void SkyBlendGPU::init_textures(TexturePool& tex_pool) {
  for (int i = 0; i < 2; i++) {
    TextureInput in;
    in.gpu_texture = m_textures[i].get();
    in.w = m_sizes[i];
    in.h = in.w;
    in.debug_name = fmt::format("PC-SKY-GPU-{}", i);
    in.id = tex_pool.allocate_pc_port_texture();
    u32 tbp = SKY_TEXTURE_VRAM_ADDRS[i];
    m_tex_info[i] = {tex_pool.give_texture_and_load_to_vram(in, tbp), tbp};
  }
}

SkyBlendStats SkyBlendGPU::do_sky_blends(DmaFollower& dma,
                                         SharedRenderState* render_state,
                                         ScopedProfilerNode& prof) {
  SkyBlendStats stats;

  //GLint old_viewport[4];
  //glGetIntegerv(GL_VIEWPORT, old_viewport);

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

  // ST was used in OpenGL, UV is used in Vulkan
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  colorBlendAttachment.blendEnable = VK_TRUE;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

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
    auto tex = render_state->texture_pool->lookup(adgif.tex0().tbp0());
    ASSERT(tex);

    // setup for rendering!
    //glViewport(0, 0, m_sizes[buffer_idx], m_sizes[buffer_idx]);
    //glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_textures[buffer_idx], 0);
    //render_state->shaders[ShaderId::SKY_BLEND].activate();

    // if the first is set, it disables alpha. we can just clear here, so it's easier to find
    // in renderdoc.
    if (is_first_draw) {
      float clear[4] = {0, 0, 0, 0};
      //glClearBufferfv(GL_COLOR, 0, clear);
    }

    // intensities should be 0-128 (maybe higher is okay, but I don't see how this could be
    // generated with the GOAL code.)
    ASSERT(intensity <= 128);

    // todo - could do this on the GPU, but probably not worth it for <20 triangles...
    float intensity_float = intensity / 128.f;
    for (auto& vert : m_vertex_data) {
      vert.intensity = intensity_float;
    }

    //glDisable(GL_DEPTH_TEST);

    // setup draw data
    if (m_vertex_buffer->map() != VK_SUCCESS) {
      lg::error("Failed to get mapped Sky Blend GPU memory");
    }
    m_vertex_buffer->writeToBuffer(m_vertex_data);
    m_vertex_buffer->unmap();

    // Draw a sqaure
    //glDrawArrays(GL_TRIANGLES, 0, 6);

    // 1 draw, 2 triangles
    prof.add_draw_call(1);
    prof.add_tri(2);

    render_state->texture_pool->move_existing_to_vram(m_tex_info[buffer_idx].tex,
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
  }

  //glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);

  return stats;
}
