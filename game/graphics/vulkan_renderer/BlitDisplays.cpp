#include "BlitDisplays.h"

#include "common/log/log.h"

#include "game/graphics/vulkan_renderer/VulkanRenderer.h"

BlitDisplaysVulkan::BlitDisplaysVulkan(const std::string& name,
                                       int my_id,
                                       std::shared_ptr<GraphicsDeviceVulkan> device,
                                       VulkanInitializationInfo& vulkan_info)
    : BaseBucketRenderer(name, my_id),
      BucketVulkanRenderer(device, vulkan_info),
      m_texture(device),
      m_sampler_helper(device) {
  // set up target texture
  u32 tbp = 0x3300;

  // TODO: Create poulate image here
  m_texture.createImage({32, 32, 1}, 1, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

  m_texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_ASPECT_COLOR_BIT, 1);

  auto& sampler_create_info = m_sampler_helper.GetSamplerCreateInfo();
  sampler_create_info.minFilter = VK_FILTER_NEAREST;
  sampler_create_info.magFilter = VK_FILTER_NEAREST;

  sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  m_sampler_helper.CreateSampler();

  VulkanTextureInput in;
  in.debug_page_name = "PC-BLIT";
  in.debug_name = fmt::format("blit-display");
  in.id = m_vulkan_info.texture_pool->allocate_pc_port_texture();
  m_gpu_tex = m_vulkan_info.texture_pool->give_texture_and_load_to_vram(in, tbp);
  m_tbp = tbp;
}

void BlitDisplaysVulkan::render(DmaFollower& dma,
                                SharedVulkanRenderState* render_state,
                                ScopedProfilerNode& prof, VkCommandBuffer command_buffer) {
  m_command_buffer = command_buffer;
  auto& back = render_state->back_fbo;
  bool valid = back && render_state->isFramebufferValid;

  // loop through all data
  while (dma.current_tag_offset() != render_state->next_bucket) {
    auto data = dma.read_and_advance();

    if (data.vifcode0().kind == VifCode::Kind::PC_PORT) {
      switch (data.vifcode0().immediate) {
        case 0x10: {  // copy buffer->texture (tbp in vif1)
          u32 tbp = data.vifcode1().immediate;
          ASSERT_MSG(tbp == m_tbp, fmt::format("unexpected tbp {}", tbp));
          if (!valid) {
            lg::error("no valid back buffer to blit!");
            break;
          }
          // copy buffer texture -> custom texture
          auto my_tex_id = m_gpu_tex->gpu_textures.at(0);

          VkImageCopy imageCopy{};
          imageCopy.srcOffset.x = my_tex_id->getWidth();
          imageCopy.srcOffset.y = my_tex_id->getHeight();
          imageCopy.srcOffset.z = my_tex_id->getDepth();

          imageCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          imageCopy.srcSubresource.baseArrayLayer = 0;
          imageCopy.srcSubresource.layerCount = 1;
          imageCopy.srcSubresource.mipLevel = 0;

          imageCopy.dstOffset.x = back->ColorAttachmentTexture().getWidth();
          imageCopy.dstOffset.x = back->ColorAttachmentTexture().getHeight();
          imageCopy.dstOffset.x = back->ColorAttachmentTexture().getDepth();

          imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          imageCopy.dstSubresource.baseArrayLayer = 0;
          imageCopy.dstSubresource.layerCount = 1;
          imageCopy.dstSubresource.mipLevel = 0;

          vkCmdCopyImage(m_command_buffer, my_tex_id->getImage(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         back->ColorAttachmentTexture().getImage(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, &imageCopy);

          m_vulkan_info.texture_pool->move_existing_to_vram(m_gpu_tex, m_tbp);
          break;
        }
      }
    }
  }
}

void BlitDisplaysVulkan::draw_debug_window() {}
