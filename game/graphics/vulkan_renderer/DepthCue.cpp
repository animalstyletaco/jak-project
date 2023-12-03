#include "DepthCue.h"

#include "game/graphics/general_renderer/dma_helpers.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

namespace {
// Converts fixed point (with 4 bits for decimal) to floating point.
float fixed_to_floating_point(int fixed) {
  return fixed / 16.0f;
}

math::Vector2f fixed_to_floating_point(const math::Vector<s32, 2>& fixed_vec) {
  return math::Vector2f(fixed_to_floating_point(fixed_vec.x()),
                        fixed_to_floating_point(fixed_vec.y()));
}
}  // namespace

// Total number of loops depth-cue performs to draw to the framebuffer
constexpr int TOTAL_DRAW_SLICES = 16;

DepthCueVulkan::DepthCueVulkan(const std::string& name,
                               int my_id,
                               std::shared_ptr<GraphicsDeviceVulkan> device,
                               VulkanInitializationInfo& vulkan_info)
    : BaseDepthCue(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  graphics_setup();

  m_draw_slices.resize(TOTAL_DRAW_SLICES);
}

void DepthCueVulkan::graphics_setup() {
  // Gen texture for sampling the framebuffer
  m_ogl.depth_cue_page_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(SpriteVertex), 4, 1);

  m_ogl.on_screen_vertex_buffer =
      std::make_unique<VertexBuffer>(m_device, sizeof(SpriteVertex), 4, 1);

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  m_sampler = std::make_unique<VulkanSamplerHelper>(m_device);
  VkSamplerCreateInfo& samplerInfo = m_sampler->GetSamplerCreateInfo();
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  // samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;

  // Emerc fragment descriptor is the same as standard merc no need for separate object
  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);
  m_descriptor_sets.resize(2);
  create_pipeline_layout();
  InitializeInputAttributes();
}

void DepthCueVulkan::InitializeInputAttributes() {
  // Gen framebuffer for depth-cue-base-page

  std::array<VkVertexInputBindingDescription, 2> bindingDescriptions{};
  bindingDescriptions[0].binding = 0;
  bindingDescriptions[0].stride = sizeof(SpriteVertex);
  bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  bindingDescriptions[1].binding = 1;
  bindingDescriptions[1].stride = sizeof(SpriteVertex);
  bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.insert(
      m_pipeline_config_info.bindingDescriptions.end(), bindingDescriptions.begin(),
      bindingDescriptions.end());

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(SpriteVertex, xy);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(SpriteVertex, st);

  attributeDescriptions[2].binding = 1;
  attributeDescriptions[2].location = 0;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(SpriteVertex, xy);

  attributeDescriptions[3].binding = 1;
  attributeDescriptions[3].location = 1;
  attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[3].offset = offsetof(SpriteVertex, st);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());

  // Activate shader
  auto& shader = m_vulkan_info.shaders[ShaderId::DEPTH_CUE];
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

void DepthCueVulkan::create_pipeline_layout() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      m_fragment_descriptor_layout->getDescriptorSetLayout()};

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(m_depth_cue_push_constant);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  pipelineLayoutInfo.pushConstantRangeCount = 1;

  if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                             &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void DepthCueVulkan::render(DmaFollower& dma,
                            SharedVulkanRenderState* render_state,
                            ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  m_pipeline_config_info.multisampleInfo.rasterizationSamples =
      m_vulkan_info.swap_chain->get_render_pass_sample_count();
  BaseDepthCue::render(dma, render_state, prof);
}

void DepthCueVulkan::setup(BaseSharedRenderState* render_state, ScopedProfilerNode& /*prof*/) {
  if (m_debug.cache_setup && (m_ogl.last_draw_region_w == render_state->draw_region_w &&
                              m_ogl.last_draw_region_h == render_state->draw_region_h &&
                              // Also recompute when certain debug settings change
                              m_ogl.last_override_sharpness == m_debug.override_sharpness &&
                              m_ogl.last_custom_sharpness == m_debug.sharpness &&
                              m_ogl.last_force_original_res == m_debug.force_original_res &&
                              m_ogl.last_res_scale == m_debug.res_scale)) {
    // Draw region didn't change, everything is already set up
    return;
  }

  m_ogl.last_draw_region_w = render_state->draw_region_w;
  m_ogl.last_draw_region_h = render_state->draw_region_h;
  m_ogl.last_override_sharpness = m_debug.override_sharpness;
  m_ogl.last_custom_sharpness = m_debug.sharpness;
  m_ogl.last_force_original_res = m_debug.force_original_res;
  m_ogl.last_res_scale = m_debug.res_scale;

  // ASSUMPTIONS
  // --------------------------
  // Assert some assumptions that most of the data for each depth-cue draw is the same.
  // The way the game wants to render this effect is very inefficient in OpenGL, we can use these
  // assumptions to only alter state once, do setup once, and group multiple draw calls.
  const DrawSlice& first_slice = m_draw_slices[0];

  // 1. Assume each draw slice has the exact same width of 32
  //    We'll compare each slice to the first as we go
  float slice_width = fixed_to_floating_point(first_slice.on_screen_draw.xyzf2_2.x() -
                                              first_slice.on_screen_draw.xyzf2_1.x());
  // NOTE: Y-coords will range between [0,1/2 output res], usually 224 but not always.
  // We'll capture it here so we can convert to coords to [0,1] ranges later.
  float slice_height = fixed_to_floating_point(first_slice.on_screen_draw.xyzf2_2.y());

  ASSERT(slice_width == 32.0f);
  ASSERT(first_slice.on_screen_draw.xyzf2_1.y() == 0);

  // 2. Assume that the framebuffer is sampled as a 1024x256 texel view and that the game thinks the
  // framebuffer is 512 pixels wide.
  int fb_sample_width = (int)pow(2, first_slice.depth_cue_page_setup.tex01.tw());
  int fb_sample_height = (int)pow(2, first_slice.depth_cue_page_setup.tex01.th());
  int fb_width = first_slice.depth_cue_page_setup.tex01.tbw() * 64;

  ASSERT(fb_sample_width == 1024);
  ASSERT(fb_sample_height == 256);
  ASSERT(fb_width == 512);
  ASSERT(fb_width * 2 == fb_sample_width);

  // 3. Finally, assert that all slices match the above assumptions
  for (const DrawSlice& slice : m_draw_slices) {
    float _slice_width = fixed_to_floating_point(slice.on_screen_draw.xyzf2_2.x() -
                                                 slice.on_screen_draw.xyzf2_1.x());
    float _slice_height = fixed_to_floating_point(slice.on_screen_draw.xyzf2_2.y());

    ASSERT(slice_width == _slice_width);
    ASSERT(slice_height == _slice_height);
    ASSERT(slice.on_screen_draw.xyzf2_1.y() == 0);

    int _fb_sample_width = (int)pow(2, slice.depth_cue_page_setup.tex01.tw());
    int _fb_sample_height = (int)pow(2, slice.depth_cue_page_setup.tex01.th());
    int _fb_width = slice.depth_cue_page_setup.tex01.tbw() * 64;

    ASSERT(fb_sample_width == _fb_sample_width);
    ASSERT(fb_sample_height == _fb_sample_height);
    ASSERT(fb_width == _fb_width);
  }

  // FRAMEBUFFER SAMPLE TEXTURE
  // --------------------------
  // We need a copy of the framebuffer to sample from. If the framebuffer wasn't using
  // multisampling, this wouldn't be necessary and the framebuffer could just be bound. Instead,
  // we'll just blit to a new texture.
  //
  // The original game code would have created this as a view into the framebuffer whose width is 2x
  // as large, however this isn't necessary for the effect to work.
  int pc_fb_sample_width = render_state->draw_region_w;
  int pc_fb_sample_height = render_state->draw_region_h;

  m_ogl.framebuffer_sample_width = pc_fb_sample_width;
  m_ogl.framebuffer_sample_height = pc_fb_sample_height;

  m_ogl.framebuffer_sample_fbo = std::make_unique<FramebufferVulkanHelper>(
      m_ogl.framebuffer_sample_width, m_ogl.framebuffer_sample_height, VK_FORMAT_R8G8B8A8_UNORM,
      m_device);

  // DEPTH CUE BASE PAGE FRAMEBUFFER
  // --------------------------
  // Next, we need a framebuffer to draw slices of the sample texture to. The depth-cue effect
  // usually does this in 16 vertical slices that are 32 pixels wide each. The destination
  // drawn to is smaller than the source by a very small amount (defined by sharpness in the
  // GOAL code), which kicks in the bilinear filtering effect. Normally, a 32x224 texture will
  // be reused for each slice but for the sake of efficient rendering, we'll create a framebuffer
  // that can store all 16 slices side-by-side and draw all slices to it all at once.
  int pc_depth_cue_fb_width = render_state->draw_region_w;
  int pc_depth_cue_fb_height = render_state->draw_region_h;

  if (m_debug.force_original_res) {
    pc_depth_cue_fb_width = 512;
  }

  pc_depth_cue_fb_width *= m_debug.res_scale;

  m_ogl.fbo_width = pc_depth_cue_fb_width;
  m_ogl.fbo_height = pc_depth_cue_fb_height;

  m_ogl.fbo = std::make_unique<FramebufferVulkanHelper>(m_ogl.fbo_width, m_ogl.fbo_width,
                                                        VK_FORMAT_R8G8B8A8_UNORM, m_device);

  // DEPTH CUE BASE PAGE VERTEX DATA
  // --------------------------
  // Now that we have a framebuffer to draw each slice to, we need the actual vertex data.
  // We'll take the exact data DMA'd and scale it up the PC dimensions.
  std::vector<SpriteVertex> depth_cue_page_vertices;

  // U-coordinates here will range from [0,512], but the maximum U value in the original is
  // 1024 since the sample texel width is usually 1024. Since we're not using a texture with
  // 2x the width, the maximum U value used to convert UVs to [0,1] should be 512.
  float max_u = fb_sample_width / 2.0f;
  ASSERT(max_u == 512.0f);

  for (const auto& slice : m_draw_slices) {
    math::Vector2f xyoffset = fixed_to_floating_point(
        math::Vector2<s32>((s32)slice.depth_cue_page_setup.xyoffset1.ofx(),
                           (s32)slice.depth_cue_page_setup.xyoffset1.ofy()));

    math::Vector2f xy1 = fixed_to_floating_point(slice.depth_cue_page_draw.xyzf2_1.xy());
    math::Vector2f xy2 = fixed_to_floating_point(slice.depth_cue_page_draw.xyzf2_2.xy());
    math::Vector2f uv1 = fixed_to_floating_point(slice.depth_cue_page_draw.uv_1.xy());
    math::Vector2f uv2 = fixed_to_floating_point(slice.depth_cue_page_draw.uv_2.xy());

    ASSERT(xy1.x() == 0);
    ASSERT(xy1.y() == 0);
    ASSERT(xy2.x() <= 32.0f);
    ASSERT(xy2.y() <= slice_height);

    if (m_debug.override_sharpness) {
      // Undo sharpness from GOAL code and apply custom
      xy2.x() = 32.0f * m_debug.sharpness;
      xy2.y() = 224.0f * m_debug.sharpness;
    }

    // Apply xyoffset GS register
    xy1.x() += xyoffset.x() / 4096.0f;
    xy1.y() += xyoffset.y() / 4096.0f;
    xy2.x() += xyoffset.x() / 4096.0f;
    xy2.y() += xyoffset.y() / 4096.0f;

    // U-coord will range from [0,512], which is half of the original framebuffer sample width
    // Let's also use it to determine the X offset into the depth-cue framebuffer since the
    // original draw assumes each slice is at 0,0.
    float x_offset = (uv1.x() / 512.0f) * (xy2.x() / 32.0f);

    build_sprite(depth_cue_page_vertices,
                 // Top-left
                 (xy1.x() / 512.0f) + x_offset,  // x1
                 xy1.y() / slice_height,         // y1
                 uv1.x() / max_u,                // s1
                 uv1.y() / slice_height,         // t1
                 // Bottom-right
                 (xy2.x() / 512.0f) + x_offset,  // x2
                 xy2.y() / slice_height,         // y2
                 uv2.x() / max_u,                // s2
                 uv2.y() / slice_height          // t2
    );
  }

  m_ogl.depth_cue_page_vertex_buffer->writeToGpuBuffer(depth_cue_page_vertices.data());

  // ON SCREEN VERTEX DATA
  // --------------------------
  // Finally, we need to draw pixels from the depth-cue-base-page back to the on-screen
  // framebuffer. We'll take the same approach as above.
  std::vector<SpriteVertex> on_screen_vertices;

  for (const auto& slice : m_draw_slices) {
    math::Vector2f xyoffset = fixed_to_floating_point(math::Vector2<s32>(
        (s32)slice.on_screen_setup.xyoffset1.ofx(), (s32)slice.on_screen_setup.xyoffset1.ofy()));

    math::Vector2f xy1 = fixed_to_floating_point(slice.on_screen_draw.xyzf2_1.xy());
    math::Vector2f xy2 = fixed_to_floating_point(slice.on_screen_draw.xyzf2_2.xy());
    math::Vector2f uv1 = fixed_to_floating_point(slice.on_screen_draw.uv_1.xy());
    math::Vector2f uv2 = fixed_to_floating_point(slice.on_screen_draw.uv_2.xy());

    ASSERT(uv1.x() == 0);
    ASSERT(uv1.y() == 0);
    ASSERT(uv2.x() <= 32.0f);
    ASSERT(uv2.y() <= slice_height);

    if (m_debug.override_sharpness) {
      // Undo sharpness from GOAL code and apply custom
      uv2.x() = 32.0f * m_debug.sharpness;
      uv2.y() = 224.0f * m_debug.sharpness;
    }

    // Apply xyoffset GS register
    xy1.x() += xyoffset.x() / 4096.0f;
    xy1.y() += xyoffset.y() / 4096.0f;
    xy2.x() += xyoffset.x() / 4096.0f;
    xy2.y() += xyoffset.y() / 4096.0f;

    // X-coord will range from [0,512], which is half of the original framebuffer sample width
    // Let's also use it to determine the U offset into the on-screen framebuffer since the
    // original draw assumes each slice is at 0,0.
    float u_offset = (xy1.x() / 512.0f) * (uv2.x() / 32.0f);

    build_sprite(on_screen_vertices,
                 // Top-left
                 xy1.x() / 512.0f,               // x1
                 xy1.y() / slice_height,         // y1
                 (uv1.x() / 512.0f) + u_offset,  // s1
                 uv1.y() / slice_height,         // t1
                 // Bottom-right
                 xy2.x() / 512.0f,               // x2
                 xy2.y() / slice_height,         // y2
                 (uv2.x() / 512.0f) + u_offset,  // s2
                 uv2.y() / slice_height          // t2
    );
  }

  m_ogl.on_screen_vertex_buffer->writeToGpuBuffer(on_screen_vertices.data());
}

void DepthCueVulkan::draw(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  m_pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  m_pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  m_pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  m_pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;

  std::array<VkImageResolve, 1> imageResolves{};
  imageResolves[0].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageResolves[0].srcSubresource.mipLevel = 0;
  imageResolves[0].srcSubresource.baseArrayLayer = 0;
  imageResolves[0].srcSubresource.layerCount = 1;

  imageResolves[0].srcOffset.x = render_state->draw_offset_x;
  imageResolves[0].srcOffset.y = render_state->draw_offset_y;
  imageResolves[0].srcOffset.z = 0;

  imageResolves[0].dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageResolves[0].dstSubresource.mipLevel = 0;
  imageResolves[0].dstSubresource.baseArrayLayer = 0;
  imageResolves[0].dstSubresource.layerCount = 1;

  imageResolves[0].dstOffset.x = 0;
  imageResolves[0].dstOffset.y = 0;
  imageResolves[0].dstOffset.z = 0;

  imageResolves[0].extent.width = m_ogl.framebuffer_sample_width;
  imageResolves[0].extent.height = m_ogl.framebuffer_sample_height;
  imageResolves[0].extent.depth = 1;

  VulkanTexture& color_texture =
      m_vulkan_info.swap_chain->GetColorAttachmentImageAtIndex(m_vulkan_info.currentFrame);
  vkCmdResolveImage(
      m_vulkan_info.render_command_buffer, color_texture.getImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_ogl.fbo->ColorAttachmentTexture().getImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageResolves.size(), imageResolves.data());

  // Next, we need to draw from the framebuffer sample texture to the depth-cue-base-page
  // framebuffer
  {
    const auto& depth_cue_page_draw = m_draw_slices[0].depth_cue_page_draw;

    math::Vector4f colorf(
        depth_cue_page_draw.rgbaq.x() / 255.0f, depth_cue_page_draw.rgbaq.y() / 255.0f,
        depth_cue_page_draw.rgbaq.z() / 255.0f, depth_cue_page_draw.rgbaq.w() / 255.0f);

    m_depth_cue_push_constant.u_color = colorf;
    m_depth_cue_push_constant.u_depth = 1.0f;

    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;

    m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    prof.add_draw_call();
    prof.add_tri(2 * TOTAL_DRAW_SLICES);

    m_graphics_pipeline_layout.updateGraphicsPipeline(m_vulkan_info.render_command_buffer,
                                                      m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    VkViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = m_ogl.fbo_width;
    viewport.height = m_ogl.fbo_height;
    VkRect2D extents = {{0, 0}, m_vulkan_info.swap_chain->getSwapChainExtent()};

    vkCmdSetViewport(m_vulkan_info.render_command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(m_vulkan_info.render_command_buffer, 0, 1, &extents);

    VkDeviceSize offsets[] = {0};
    VkBuffer vertex_buffers[] = {m_ogl.depth_cue_page_vertex_buffer->getBuffer()};
    vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_depth_cue_push_constant),
                       (void*)&m_depth_cue_push_constant);

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, 1, &m_descriptor_sets[0], 0,
                            NULL);

    vkCmdDraw(m_vulkan_info.render_command_buffer, 6 * TOTAL_DRAW_SLICES, 1, 0, 0);
  }

  // Finally, the contents of depth-cue-base-page need to be overlayed onto the on-screen
  // framebuffer
  {
    const auto& on_screen_draw = m_draw_slices[0].on_screen_draw;

    math::Vector4f colorf(on_screen_draw.rgbaq.x() / 255.0f, on_screen_draw.rgbaq.y() / 255.0f,
                          on_screen_draw.rgbaq.z() / 255.0f, on_screen_draw.rgbaq.w() / 255.0f);
    if (m_debug.override_alpha) {
      colorf.w() = m_debug.draw_alpha / 2.0f;
    }
    m_depth_cue_push_constant.u_color = colorf;

    if (m_debug.depth == 1.0f) {
      m_depth_cue_push_constant.u_depth = m_debug.depth;
    } else {
      // Scale debug depth expontentially to make the slider easier to use
      m_depth_cue_push_constant.u_depth = pow(m_debug.depth, 8);
    }

    m_pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    m_pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    m_pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_pipeline_config_info.colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    m_pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;

    prof.add_draw_call();
    prof.add_tri(2 * TOTAL_DRAW_SLICES);

    m_graphics_pipeline_layout.updateGraphicsPipeline(m_vulkan_info.render_command_buffer,
                                                      m_pipeline_config_info);
    m_graphics_pipeline_layout.bind(m_vulkan_info.render_command_buffer);

    VkViewport viewport;
    viewport.x = render_state->draw_offset_x;
    viewport.y = render_state->draw_offset_y;
    viewport.width = render_state->draw_region_w;
    viewport.height = render_state->draw_region_h;
    VkRect2D extents = {{render_state->draw_offset_x, render_state->draw_offset_y},
                        m_vulkan_info.swap_chain->getSwapChainExtent()};

    vkCmdSetViewport(m_vulkan_info.render_command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(m_vulkan_info.render_command_buffer, 0, 1, &extents);

    VkDeviceSize offsets[] = {0};
    VkBuffer vertex_buffers[] = {m_ogl.on_screen_vertex_buffer->getBuffer()};
    vkCmdBindVertexBuffers(m_vulkan_info.render_command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdPushConstants(m_vulkan_info.render_command_buffer, m_pipeline_config_info.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_depth_cue_push_constant),
                       (void*)&m_depth_cue_push_constant);

    vkCmdBindDescriptorSets(m_vulkan_info.render_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_config_info.pipelineLayout, 0, 1, &m_descriptor_sets[1], 0,
                            NULL);

    vkCmdDraw(m_vulkan_info.render_command_buffer, 6 * TOTAL_DRAW_SLICES, 1, 0, 0);
  }

  // Done
  m_pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_TRUE;
}
