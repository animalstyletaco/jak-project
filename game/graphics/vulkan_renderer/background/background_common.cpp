

#include "background_common.h"

#include <immintrin.h>

#include "common/util/os.h"
#include "game/graphics/gfx.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"

using namespace background_common;

DoubleDraw vulkan_background_common::setup_vulkan_from_draw_mode(
  DrawMode mode, VulkanSamplerHelper& sampler, PipelineConfigInfo& pipeline_config_info, bool mipmap) {
  pipeline_config_info.depthStencilInfo.depthTestEnable = VK_FALSE;
  pipeline_config_info.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
  pipeline_config_info.depthStencilInfo.stencilTestEnable = VK_FALSE;

  DoubleDraw double_draw;

  if (mode.get_zt_enable()) {
    pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
    switch (mode.get_depth_test()) {
      case GsTest::ZTest::NEVER:
        pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_NEVER;
        break;
      case GsTest::ZTest::ALWAYS:
        pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        break;
      case GsTest::ZTest::GEQUAL:
        pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        break;
      case GsTest::ZTest::GREATER:
        pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;
        break;
      default:
        ASSERT(false);
    }
  }

  pipeline_config_info.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;

  pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.0f;
  pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.0f;
  pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.0f;
  pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.0f;

  if (mode.get_ab_enable() && mode.get_alpha_blend() != DrawMode::AlphaBlend::DISABLED) {
    pipeline_config_info.colorBlendAttachment.blendEnable = VK_TRUE;
    switch (mode.get_alpha_blend()) {
      case DrawMode::AlphaBlend::SRC_SRC_SRC_SRC:
        pipeline_config_info.colorBlendAttachment.blendEnable = VK_FALSE;
        break;
      case DrawMode::AlphaBlend::SRC_DST_SRC_DST:

        pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; 

        pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; 
        pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

        break;
      case DrawMode::AlphaBlend::SRC_0_SRC_DST:

        pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
        pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

        pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; 

        pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; 
        pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
      case DrawMode::AlphaBlend::SRC_0_FIX_DST:

        pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; 
        pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; 

        pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
      case DrawMode::AlphaBlend::SRC_DST_FIX_DST:
        // Cv = (Cs - Cd) * FIX + Cd
        // Cs * FIX * 0.5
        // Cd * FIX * 0.5

        pipeline_config_info.colorBlendInfo.blendConstants[0] = 0.5f;
        pipeline_config_info.colorBlendInfo.blendConstants[1] = 0.5f;
        pipeline_config_info.colorBlendInfo.blendConstants[2] = 0.5f;
        pipeline_config_info.colorBlendInfo.blendConstants[3] = 0.5f;
        break;
      case DrawMode::AlphaBlend::ZERO_SRC_SRC_DST:
        //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        //glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;

        pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

        pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
      case DrawMode::AlphaBlend::SRC_0_DST_DST:
        pipeline_config_info.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        pipeline_config_info.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        pipeline_config_info.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        pipeline_config_info.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

        pipeline_config_info.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        pipeline_config_info.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        double_draw.color_mult = 0.5f;
        break;
      default:
        ASSERT(false);
    }
  }

  pipeline_config_info.colorBlendInfo.logicOpEnable = VK_FALSE;
  pipeline_config_info.colorBlendInfo.attachmentCount = 1;
  pipeline_config_info.colorBlendInfo.pAttachments = &pipeline_config_info.colorBlendAttachment;

  if (mode.get_clamp_s_enable() || mode.get_clamp_t_enable()) {
    pipeline_config_info.rasterizationInfo.depthClampEnable = VK_TRUE;
  } else {
    pipeline_config_info.rasterizationInfo.depthClampEnable = VK_FALSE;
  }

  VkSamplerCreateInfo& samplerInfo = sampler.GetSamplerCreateInfo();
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.minLod = 0.0f;
  //samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  //ST was used in OpenGL, UV is used in Vulkan
  if (mode.get_clamp_s_enable() ) {
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  if (mode.get_clamp_t_enable()) {
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }

  if (mode.get_filt_enable()) {
    if (mipmap) {
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
  } else {
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
  }

  sampler.CreateSampler();

  // for some reason, they set atest NEVER + FB_ONLY to disable depth writes
  bool alpha_hack_to_disable_z_write = false;


  float alpha_min = 0.;
  if (mode.get_at_enable()) {
    switch (mode.get_alpha_test()) {
      case DrawMode::AlphaTest::ALWAYS:
        break;
      case DrawMode::AlphaTest::GEQUAL:
        alpha_min = mode.get_aref() / 127.f;
        switch (mode.get_alpha_fail()) {
          case GsTest::AlphaFail::KEEP:
            // ok, no need for double draw
            break;
          case GsTest::AlphaFail::FB_ONLY:
            if (mode.get_depth_write_enable()) {
              // darn, we need to draw twice
              double_draw.kind = DoubleDrawKind::AFAIL_NO_DEPTH_WRITE;
              double_draw.aref_second = alpha_min;
            } else {
              alpha_min = 0.f;
            }
            break;
          default:
            ASSERT(false);
        }
        break;
      case DrawMode::AlphaTest::NEVER:
        if (mode.get_alpha_fail() == GsTest::AlphaFail::FB_ONLY) {
          alpha_hack_to_disable_z_write = true;
        } else {
          ASSERT(false);
        }
        break;
      default:
        ASSERT(false);
    }
  }

  //FIXME: Add render pass with depth buffering enabled here
  if (mode.get_depth_write_enable() && !alpha_hack_to_disable_z_write) {
    pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_TRUE;
  } else {
    pipeline_config_info.depthStencilInfo.depthWriteEnable = VK_FALSE;
  }
  double_draw.aref_first = alpha_min;
  return double_draw;
}

DoubleDraw vulkan_background_common::setup_tfrag_shader(
  BaseSharedRenderState* render_state, DrawMode mode, 
    VulkanSamplerHelper& sampler, PipelineConfigInfo& pipeline_info,
    BackgroundCommonFragmentPushConstantShaderData& fragment_push_constant) {
  auto draw_settings = vulkan_background_common::setup_vulkan_from_draw_mode(mode, sampler, pipeline_info, true);
  fragment_push_constant.alpha_min = draw_settings.aref_first;
  fragment_push_constant.alpha_max = 10.f;

  return draw_settings;
}

void vulkan_background_common::first_tfrag_draw_setup(
  const TfragRenderSettings& settings,
  BackgroundCommonVertexUniformShaderData* uniform_vertex_push_constant) {
    uniform_vertex_push_constant->camera = settings.math_camera;
    uniform_vertex_push_constant->hvdf_offset =
        math::Vector4f{settings.hvdf_offset[0], settings.hvdf_offset[1], settings.hvdf_offset[2],
                       settings.hvdf_offset[3]};
    uniform_vertex_push_constant->fog_constant = settings.fog.x();
    uniform_vertex_push_constant->fog_min = settings.fog.y();
    uniform_vertex_push_constant->fog_max = settings.fog.z();
}

void vulkan_background_common::make_all_visible_multidraws(std::vector<std::vector<VkMultiDrawIndexedInfoEXT>>& multiDrawIndexedInfos,
                                                           const std::vector<tfrag3::ShrubDraw>& draws) {
  multiDrawIndexedInfos.clear();
  multiDrawIndexedInfos.resize(draws.size());
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    u64 iidx = draw.first_index_index;
    // Assumes Vertex offset is not used
    VkMultiDrawIndexedInfoEXT multiDrawIndexedInfo{};
    multiDrawIndexedInfo.indexCount = draw.num_indices;
    multiDrawIndexedInfo.firstIndex = iidx;
    multiDrawIndexedInfos[i].push_back(multiDrawIndexedInfo);
  }
}

u32 vulkan_background_common::make_all_visible_multidraws(std::vector<std::vector<VkMultiDrawIndexedInfoEXT>>& multiDrawIndexedInfosCollection,
                                                          const std::vector<tfrag3::StripDraw>& draws) {
  u32 num_tris = 0;
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    auto& multiDrawIndexedInfos = multiDrawIndexedInfosCollection[i];
    u64 iidx = draw.unpacked.idx_of_first_idx_in_full_buffer;
    int num_inds = 0;
    for (auto& grp : draw.vis_groups) {
      num_tris += grp.num_tris;
      num_inds += grp.num_inds;
    }
    //Assumes Vertex offset is not used
    VkMultiDrawIndexedInfoEXT multiDrawIndexedInfo{};
    multiDrawIndexedInfo.indexCount = num_inds;
    multiDrawIndexedInfo.firstIndex = iidx;
    multiDrawIndexedInfos.push_back(multiDrawIndexedInfo);
  }
  return num_tris;
}

u32 vulkan_background_common::make_all_visible_index_list(DrawSettings* group_out,
                                                          u32* idx_out,
                                                          const std::vector<tfrag3::ShrubDraw>& draws,
                                                          const u32* idx_in) {
  int idx_buffer_ptr = 0;
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    DrawSettings ds;
    ds.draw_index = idx_buffer_ptr;
    memcpy(&idx_out[idx_buffer_ptr], idx_in + draw.first_index_index,
           draw.num_indices * sizeof(u32));
    idx_buffer_ptr += draw.num_indices;
    ds.number_of_draws = idx_buffer_ptr - ds.draw_index;
    group_out[i] = ds;
  }
  return idx_buffer_ptr;
}

u32 vulkan_background_common::make_multidraws_from_vis_string(std::vector<std::vector<VkMultiDrawIndexedInfoEXT>>& multiDrawIndexedInfosCollection,
                                                              const std::vector<tfrag3::StripDraw>& draws,
                                                              const std::vector<u8>& vis_data) {
  u32 num_tris = 0;
  u32 sanity_check = 0;
  multiDrawIndexedInfosCollection.clear();
  multiDrawIndexedInfosCollection.resize(draws.size());
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    auto& multidrawInfoVector = multiDrawIndexedInfosCollection[i];
    u64 iidx = draw.unpacked.idx_of_first_idx_in_full_buffer;
    ASSERT(sanity_check == iidx);
    bool building_run = false;
    u64 run_start = 0;
    for (auto& grp : draw.vis_groups) {
      sanity_check += grp.num_inds;

      bool vis = grp.vis_idx_in_pc_bvh == UINT16_MAX || vis_data[grp.vis_idx_in_pc_bvh];
      if (vis) {
        num_tris += grp.num_tris;
      }

      if (building_run) {
        if (!vis) {
          building_run = false;
          VkMultiDrawIndexedInfoEXT multidrawInfo{};
          multidrawInfo.indexCount = iidx - run_start;
          multidrawInfo.firstIndex = run_start;
          multidrawInfoVector.push_back(multidrawInfo);
        }
      } else {
        if (vis) {
          building_run = true;
          run_start = iidx;
        }
      }

      iidx += grp.num_inds;
    }

    if (building_run) {
      building_run = false;
      VkMultiDrawIndexedInfoEXT multidrawInfo{};
      multidrawInfo.indexCount = iidx - run_start;
      multidrawInfo.firstIndex = run_start;
      multidrawInfoVector.push_back(multidrawInfo);
    }
  }
  return num_tris;
}

u32 vulkan_background_common::make_multidraws_from_vis_and_proto_string(
    std::vector<std::vector<VkMultiDrawIndexedInfoEXT>>& multiDrawIndexedInfosCollection,
                                              const std::vector<tfrag3::StripDraw>& draws,
                                              const std::vector<u8>& vis_data,
                                              const std::vector<u8>& proto_vis_data) {
  u32 num_tris = 0;
  u32 sanity_check = 0;
  multiDrawIndexedInfosCollection.clear();
  multiDrawIndexedInfosCollection.resize(draws.size());
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    u64 iidx = draw.unpacked.idx_of_first_idx_in_full_buffer;
    ASSERT(sanity_check == iidx);
    auto& multiDrawIndexedInfo = multiDrawIndexedInfosCollection[i];
    bool building_run = false;
    u64 run_start = 0;
    for (auto& grp : draw.vis_groups) {
      sanity_check += grp.num_inds;
      bool vis = (grp.vis_idx_in_pc_bvh == UINT16_MAX || vis_data[grp.vis_idx_in_pc_bvh]) &&
                 proto_vis_data[grp.tie_proto_idx];
      if (vis) {
        num_tris += grp.num_tris;
      }

      if (building_run) {
        if (!vis) {
          building_run = false;
          VkMultiDrawIndexedInfoEXT indexInfo{};
          indexInfo.indexCount = iidx - run_start;
          indexInfo.firstIndex = run_start;
          multiDrawIndexedInfo.push_back(indexInfo);
        }
      } else {
        if (vis) {
          building_run = true;
          run_start = iidx;
        }
      }

      iidx += grp.num_inds;
    }

    if (building_run) {
      building_run = false;
      VkMultiDrawIndexedInfoEXT indexInfo{};
      indexInfo.indexCount = iidx - run_start;
      indexInfo.firstIndex = run_start;
      multiDrawIndexedInfo.push_back(indexInfo);
    }
  }
  return num_tris;
}

u32 vulkan_background_common::make_index_list_from_vis_string(DrawSettings* group_out,
                                                              u32* idx_out,
                                                              const std::vector<tfrag3::StripDraw>& draws,
                                                              const std::vector<u8>& vis_data,
                                                              const u32* idx_in,
                                                              u32* num_tris_out) {
  int idx_buffer_ptr = 0;
  u32 num_tris = 0;
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    int vtx_idx = 0;
    DrawSettings ds;
    ds.draw_index = idx_buffer_ptr;
    bool building_run = false;
    int run_start_out = 0;
    int run_start_in = 0;
    for (auto& grp : draw.vis_groups) {
      bool vis = grp.vis_idx_in_pc_bvh == 0xffffffff || vis_data[grp.vis_idx_in_pc_bvh];
      if (vis) {
        num_tris += grp.num_tris;
      }

      if (building_run) {
        if (vis) {
          idx_buffer_ptr += grp.num_inds;
        } else {
          building_run = false;
          memcpy(&idx_out[run_start_out],
                 idx_in + draw.unpacked.idx_of_first_idx_in_full_buffer + run_start_in,
                 (idx_buffer_ptr - run_start_out) * sizeof(u32));
        }
      } else {
        if (vis) {
          building_run = true;
          run_start_out = idx_buffer_ptr;
          run_start_in = vtx_idx;
          idx_buffer_ptr += grp.num_inds;
        }
      }
      vtx_idx += grp.num_inds;
    }

    if (building_run) {
      memcpy(&idx_out[run_start_out],
             idx_in + draw.unpacked.idx_of_first_idx_in_full_buffer + run_start_in,
             (idx_buffer_ptr - run_start_out) * sizeof(u32));
    }

    ds.number_of_draws = idx_buffer_ptr - ds.draw_index;
    group_out[i] = ds;
  }
  *num_tris_out = num_tris;
  return idx_buffer_ptr;
}
u32 vulkan_background_common::make_index_list_from_vis_and_proto_string(
    background_common::DrawSettings* group_out,
    u32* idx_out,
    const std::vector<tfrag3::StripDraw>& draws,
    const std::vector<u8>& vis_data,
    const std::vector<u8>& proto_vis_data,
    const u32* idx_in,
    u32* num_tris_out){
  int idx_buffer_ptr = 0;
  u32 num_tris = 0;
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    int vtx_idx = 0;
    background_common::DrawSettings ds;
    ds.draw_index = idx_buffer_ptr;
    bool building_run = false;
    int run_start_out = 0;
    int run_start_in = 0;
    for (auto& grp : draw.vis_groups) {
      bool vis = (grp.vis_idx_in_pc_bvh == UINT16_MAX || vis_data[grp.vis_idx_in_pc_bvh]) &&
                 proto_vis_data[grp.tie_proto_idx];
      if (vis) {
        num_tris += grp.num_tris;
      }

      if (building_run) {
        if (vis) {
          idx_buffer_ptr += grp.num_inds;
        } else {
          building_run = false;
          memcpy(&idx_out[run_start_out],
                 idx_in + draw.unpacked.idx_of_first_idx_in_full_buffer + run_start_in,
                 (idx_buffer_ptr - run_start_out) * sizeof(u32));
        }
      } else {
        if (vis) {
          building_run = true;
          run_start_out = idx_buffer_ptr;
          run_start_in = vtx_idx;
          idx_buffer_ptr += grp.num_inds;
        }
      }
      vtx_idx += grp.num_inds;
    }

    if (building_run) {
      memcpy(&idx_out[run_start_out],
             idx_in + draw.unpacked.idx_of_first_idx_in_full_buffer + run_start_in,
             (idx_buffer_ptr - run_start_out) * sizeof(u32));
    }

    ds.number_of_draws = idx_buffer_ptr - ds.draw_index;
    group_out[i] = ds;
  }
  *num_tris_out = num_tris;
  return idx_buffer_ptr;
}

u32 vulkan_background_common::make_all_visible_index_list(DrawSettings* group_out,
                                                          u32* idx_out,
                                                          const std::vector<tfrag3::StripDraw>& draws,
                                                          const u32* idx_in,
                                                          u32* num_tris_out) {
  int idx_buffer_ptr = 0;
  u32 num_tris = 0;
  for (size_t i = 0; i < draws.size(); i++) {
    const auto& draw = draws[i];
    DrawSettings ds;
    ds.draw_index = idx_buffer_ptr;
    u32 num_inds = 0;
    for (auto& grp : draw.vis_groups) {
      num_inds += grp.num_inds;
      num_tris += grp.num_tris;
    }
    memcpy(&idx_out[idx_buffer_ptr], idx_in + draw.unpacked.idx_of_first_idx_in_full_buffer,
           num_inds * sizeof(u32));
    idx_buffer_ptr += num_inds;
    ds.number_of_draws = idx_buffer_ptr - ds.draw_index;
    group_out[i] = ds;
  }
  *num_tris_out = num_tris;
  return idx_buffer_ptr;
}

VkDescriptorImageInfo vulkan_background_common::create_placeholder_descriptor_image_info(
    std::unique_ptr<VulkanTexture>& texture, std::unique_ptr<VulkanSamplerHelper>& sampler, VkImageType image_type ) {
  if (image_type != VK_IMAGE_TYPE_1D && image_type != VK_IMAGE_TYPE_2D) {
    return VkDescriptorImageInfo{};
  }

  texture->createImage(
      {TIME_OF_DAY_COLOR_COUNT, 1, 1}, 1, image_type, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  VkImageViewType image_view_type = (image_type == VK_IMAGE_TYPE_1D) ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D;
  texture->createImageView(image_view_type, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_ASPECT_COLOR_BIT, 1);
  sampler->CreateSampler();

  return VkDescriptorImageInfo{
      sampler->GetSampler(), texture->getImageView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}

BackgroundCommonEtieBaseVertexUniformBuffer::BackgroundCommonEtieBaseVertexUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(BackgroundCommonEtieBaseVertexUniformShaderData),
                          instanceCount,
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"cam_no_persp", offsetof(BackgroundCommonEtieBaseVertexUniformShaderData, cam_no_persp)},
      {"perspective0", offsetof(BackgroundCommonEtieBaseVertexUniformShaderData, perspective0)},
      {"perspective1", offsetof(BackgroundCommonEtieBaseVertexUniformShaderData, perspective1)}};
}

BackgroundCommonEtieVertexUniformBuffer::BackgroundCommonEtieVertexUniformBuffer(
    std::unique_ptr<GraphicsDeviceVulkan>& device,
    uint32_t instanceCount,
    VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(BackgroundCommonEtieVertexUniformShaderData),
                          instanceCount,    
                          minOffsetAlignment) {
  section_name_to_memory_offset_map = {
    {"cam_no_persp", offsetof(BackgroundCommonEtieVertexUniformShaderData, cam_no_persp)},
    {"perspective0", offsetof(BackgroundCommonEtieVertexUniformShaderData, perspective0)},
    {"perspective1", offsetof(BackgroundCommonEtieVertexUniformShaderData, perspective1)},
    {"envmap_tod_tint", offsetof(BackgroundCommonEtieVertexUniformShaderData, envmap_tod_tint)}
  };
}
