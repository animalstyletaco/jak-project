#include "Merc2.h"

#include "game/graphics/vulkan_renderer/background/background_common.h"

#include "game/graphics/vulkan_renderer/vulkan_utils.h"
#include "game/graphics/gfx.h"

MercVulkan2::MercVulkan2(const std::string& name,
             int my_id,
             std::unique_ptr<GraphicsDeviceVulkan>& device,
             VulkanInitializationInfo& vulkan_info) :
  BaseMerc2(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  //TODO: Figure what the vulkan equivalent of OpenGL's check buffer offset alignment is
  m_graphics_buffer_alignment = 1;

  m_light_control_vertex_uniform_buffer =
      std::make_unique<MercLightControlVertexUniformBuffer>(m_device, MAX_DRAWS_PER_LEVEL, 1);
  m_camera_control_vertex_uniform_buffer = std::make_unique<MercCameraControlVertexUniformBuffer>(m_device, 1, 1);
  m_perspective_matrix_vertex_uniform_buffer = std::make_unique<MercPerspectiveMatrixVertexUniformBuffer>(m_device, 1, 1);
  m_bone_vertex_uniform_buffer = std::make_unique<MercBoneVertexUniformBuffer>(device);

  m_fragment_uniform_buffer = std::make_unique<MercFragmentUniformBuffer>(
      m_device, sizeof(MercUniformBufferFragmentData), 1);

    m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  m_light_control_vertex_buffer_descriptor_info =
      m_light_control_vertex_uniform_buffer->descriptorInfo();
  m_camera_control_vertex_buffer_descriptor_info =
      m_camera_control_vertex_uniform_buffer->descriptorInfo();
  m_perspective_matrix_vertex_buffer_descriptor_info =
      m_perspective_matrix_vertex_uniform_buffer->descriptorInfo();
  m_bone_vertex_buffer_descriptor_info = m_bone_vertex_uniform_buffer
      ->descriptorInfo();

  m_vertex_descriptor_writer->writeBuffer(0, &m_light_control_vertex_buffer_descriptor_info)
      .writeBuffer(1, &m_camera_control_vertex_buffer_descriptor_info)
      .writeBuffer(2, &m_perspective_matrix_vertex_buffer_descriptor_info)
      .writeBuffer(3, &m_bone_vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);

  auto fragment_buffer_descriptor_info = m_fragment_uniform_buffer->descriptorInfo();
  m_fragment_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info, 1)
      .build(m_descriptor_sets[1]);
  m_placeholder_descriptor_image_info =
      *m_vulkan_info.texture_pool->get_placeholder_descriptor_image_info();
  m_fragment_descriptor_writer->writeImage(1, &m_placeholder_descriptor_image_info, 2);

  create_pipeline_layout();
  for (int i = 0; i < MAX_LEVELS; i++) {
    auto& bucket = m_level_draw_buckets.emplace_back();
    bucket.draws.resize(MAX_DRAWS_PER_LEVEL);
    bucket.samplers.resize(MAX_DRAWS_PER_LEVEL, m_device);
    bucket.descriptor_image_infos.resize(MAX_DRAWS_PER_LEVEL);
    bucket.pipeline_layouts.resize(MAX_DRAWS_PER_LEVEL, m_device);
  }

  init_shaders();
  InitializeInputAttributes();
}

/*!
 * Main MercVulkan2 rendering.
 */
void MercVulkan2::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  m_pipeline_config_info.renderPass = m_vulkan_info.swap_chain->getRenderPass();
  BaseMerc2::render(dma, render_state, prof);
}

void MercVulkan2::create_pipeline_layout() {
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
        m_vertex_descriptor_layout->getDescriptorSetLayout(),
        m_fragment_descriptor_layout->getDescriptorSetLayout()};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float);
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;

    if (vkCreatePipelineLayout(m_device->getLogicalDevice(), &pipelineLayoutInfo, nullptr,
                               &m_pipeline_config_info.pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }
}

void MercVulkan2::flush_draw_buckets(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_stats.num_draw_flush++;

  for (u32 li = 0; li < m_next_free_level_bucket; li++) {
    auto& lev_bucket = m_level_draw_buckets[li];
    auto* lev = lev_bucket.level;

    int last_tex = -1;
    int last_light = -1;
    m_stats.num_bones_uploaded += m_next_free_bone_vector;

    for (u32 di = 0; di < lev_bucket.next_free_draw; di++) {
      auto& draw = lev_bucket.draws[di];
      auto& sampler = lev_bucket.samplers[di];

      auto& textureInfo = lev->textures[draw.texture];
      m_fragment_uniform_buffer->SetUniform1i("ignore_alpha", draw.ignore_alpha, di);

      uint32_t dynamic_descriptors_offset = di * sizeof(MercLightControlVertexUniformBuffer);

      if ((int)draw.light_idx != last_light) {
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_direction0", m_lights_buffer[draw.light_idx].direction0, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_direction1", m_lights_buffer[draw.light_idx].direction1, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_direction2", m_lights_buffer[draw.light_idx].direction2, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_color0", m_lights_buffer[draw.light_idx].color0, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_color1", m_lights_buffer[draw.light_idx].color1, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_color2", m_lights_buffer[draw.light_idx].color2, di);
        m_light_control_vertex_uniform_buffer->SetUniformMathVector3f(
            "light_ambient", m_lights_buffer[draw.light_idx].ambient, di);
        last_light = draw.light_idx;
      }
      vulkan_background_common::setup_vulkan_from_draw_mode(draw.mode, (VulkanTexture*)&textureInfo, sampler, m_pipeline_config_info, true);

      m_fragment_uniform_buffer->SetUniform1i("decal_enable", draw.mode.get_decal());

      prof.add_draw_call();
      prof.add_tri(draw.num_triangles);

      m_bone_vertex_uniform_buffer->map();
      m_bone_vertex_uniform_buffer->writeToCpuBuffer(m_shader_bone_vector_buffer,
                                                     sizeof(math::Vector4f) * draw.index_count,
                                                     sizeof(math::Vector4f) * draw.first_bone);
      m_bone_vertex_uniform_buffer->unmap();

      lev_bucket.pipeline_layouts[di].createGraphicsPipeline(m_pipeline_config_info);
      lev_bucket.pipeline_layouts[di].bind(m_vulkan_info.render_command_buffer);

      lev_bucket.descriptor_image_infos[di] = {
          sampler.GetSampler(),
          textureInfo.getImageView(),
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
      };

      m_fragment_descriptor_writer->writeImage(1, &lev_bucket.descriptor_image_infos[di]);

      m_vulkan_info.swap_chain->drawIndexedCommandBuffer(
          m_vulkan_info.render_command_buffer, lev_bucket.level->merc_vertices.get(),
          lev_bucket.level->merc_indices.get(), m_pipeline_config_info.pipelineLayout,
          m_descriptor_sets, 1, &dynamic_descriptors_offset);
    }
    //TODO: Flush mapped memory here
  }

  m_next_free_light = 0;
  m_next_free_bone_vector = 0;
  m_next_free_level_bucket = 0;
}

/*!
 * Flush a model to draw buckets
 */
void MercVulkan2::flush_pending_model(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  if (!m_current_model) {
    return;
  }

  const LevelDataVulkan* lev = m_current_model->level;
  const tfrag3::MercModel* model = m_current_model->model;

  int bone_count = model->max_bones + 1;

  if (m_next_free_light >= MAX_LIGHTS) {
    fmt::print("MERC2 out of lights, consider increasing MAX_LIGHTS\n");
    flush_draw_buckets(render_state, prof);
  }

  if (m_next_free_bone_vector + m_graphics_buffer_alignment + bone_count * 8 >
      MAX_SHADER_BONE_VECTORS) {
    fmt::print("MERC2 out of bones, consider increasing MAX_SHADER_BONE_VECTORS\n");
    flush_draw_buckets(render_state, prof);
  }

  // find a level bucket
  LevelDrawBucketVulkan* lev_bucket = nullptr;
  for (u32 i = 0; i < m_next_free_level_bucket; i++) {
    if (m_level_draw_buckets[i].level == lev) {
      lev_bucket = &m_level_draw_buckets[i];
      break;
    }
  }

  if (!lev_bucket) {
    // no existing bucket
    if (m_next_free_level_bucket >= m_level_draw_buckets.size()) {
      // out of room, flush
      // fmt::print("MERC2 out of levels, consider increasing MAX_LEVELS\n");
      flush_draw_buckets(render_state, prof);
      // and retry the whole thing.
      flush_pending_model(render_state, prof);
      return;
    }
    // alloc a new one
    lev_bucket = &m_level_draw_buckets[m_next_free_level_bucket++];
    lev_bucket->reset();
    lev_bucket->level = lev;
  }

  if (lev_bucket->next_free_draw + model->max_draws >= lev_bucket->draws.size()) {
    // out of room, flush
    fmt::print("MERC2 out of draws, consider increasing MAX_DRAWS_PER_LEVEL\n");
    flush_draw_buckets(render_state, prof);
    // and retry the whole thing.
    flush_pending_model(render_state, prof);
    return;
  }

  u32 first_bone = alloc_bones(bone_count);

  // allocate lights
  u32 lights = alloc_lights(m_current_lights);
  //
  for (size_t ei = 0; ei < model->effects.size(); ei++) {
    if (!(m_current_effect_enable_bits & (1 << ei))) {
      continue;
    }

    u8 ignore_alpha = (m_current_ignore_alpha_bits & (1 << ei));
    auto& effect = model->effects[ei];
    for (auto& mdraw : effect.draws) {
      Draw* draw = &lev_bucket->draws[lev_bucket->next_free_draw++];
      draw->first_index = mdraw.first_index;
      draw->index_count = mdraw.index_count;
      draw->mode = mdraw.mode;
      draw->texture = mdraw.tree_tex_id;
      draw->first_bone = first_bone;
      draw->light_idx = lights;
      draw->num_triangles = mdraw.num_triangles;
      draw->ignore_alpha = ignore_alpha;
    }
  }

  m_current_model = std::nullopt;
}


/*!
 * Once-per-frame initialization
 */
void MercVulkan2::init_for_frame(BaseSharedRenderState* render_state) {
  // reset state
  m_current_model = std::nullopt;
  m_stats = {};

  // set uniforms that we know from render_state
  m_camera_control_vertex_uniform_buffer->SetUniform4f(
      "fog_constants", render_state->fog_color[0] / 255.f,
              render_state->fog_color[1] / 255.f, render_state->fog_color[2] / 255.f,
              render_state->fog_intensity / 255);
  m_fragment_uniform_buffer->SetUniform1ui("gfx_hack_no_tex", Gfx::g_global_settings.hack_no_tex);
}

void MercVulkan2::set_merc_uniform_buffer_data(const DmaTransfer& dma) {
  memcpy(&m_low_memory, dma.data + 16, sizeof(LowMemory));
  m_camera_control_vertex_uniform_buffer->SetUniformMathVector4f("hvdf_offset",
                                                                 m_low_memory.hvdf_offset);
  m_camera_control_vertex_uniform_buffer->SetUniformMathVector4f("fog_constants", m_low_memory.fog);
  for (int i = 0; i < 4; i++) {
    m_camera_control_vertex_uniform_buffer->SetUniformMathVector4f(
        (std::string("perspective") + std::to_string(i)).c_str(),  // Ugly declaration
        m_low_memory.perspective[i]);
  }
  // todo rm.
  m_camera_control_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "perspective_matrix", 1, GL_FALSE,
      &m_low_memory.perspective[0].x());
}

/*!
 * Handle the merc renderer switching to a different model.
 */
void MercVulkan2::init_pc_model(const DmaTransfer& setup, BaseSharedRenderState* render_state) {
  // determine the name. We've packed this in a separate PC-port specific packet.
  std::string name((const char*)setup.data);

  // get the model from the loader
  m_current_model = m_vulkan_info.loader->get_merc_model(name.c_str());

  // update stats
  m_stats.num_models++;
  if (m_current_model) {
    for (const auto& effect : m_current_model->model->effects) {
      m_stats.num_effects++;
      m_stats.num_predicted_draws += effect.draws.size();
      for (const auto& draw : effect.draws) {
        m_stats.num_predicted_tris += draw.num_triangles;
      }
    }
  }
}

void MercVulkan2::init_shaders() {
  auto& shader = m_vulkan_info.shaders[ShaderId::MERC2];

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

void MercVulkan2::InitializeInputAttributes() {
  VkVertexInputBindingDescription mercVertexBindingDescription{};
  mercVertexBindingDescription.binding = 0;
  mercVertexBindingDescription.stride = sizeof(tfrag3::MercVertex);
  mercVertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputBindingDescription mercMatrixBindingDescription{};
  mercMatrixBindingDescription.binding = 1;
  mercMatrixBindingDescription.stride = sizeof(ShaderMercMat);
  mercMatrixBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions = {mercVertexBindingDescription,
                                                mercMatrixBindingDescription};

  std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::MercVertex, pos);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::MercVertex, normal[0]);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(tfrag3::MercVertex, weights[0]);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[3].offset = offsetof(tfrag3::MercVertex, st[0]);

  // FIXME: Make sure format for byte and shorts are correct
  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 4;
  attributeDescriptions[4].format = VK_FORMAT_R8G8B8A8_SNORM;
  attributeDescriptions[4].offset = offsetof(tfrag3::MercVertex, rgba[0]);

  attributeDescriptions[5].binding = 0;
  attributeDescriptions[5].location = 5;
  attributeDescriptions[5].format = VK_FORMAT_R8G8B8_UINT;
  attributeDescriptions[5].offset = offsetof(tfrag3::MercVertex, mats[0]);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
}

MercLightControlVertexUniformBuffer::MercLightControlVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                      uint32_t instanceCount,
                                      VkDeviceSize minOffsetAlignment)
  : UniformVulkanBuffer(device,
                    sizeof(MercLightControlUniformBufferVertexData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"light_dir0", offsetof(MercLightControlUniformBufferVertexData, light_dir0)},
      {"light_dir1", offsetof(MercLightControlUniformBufferVertexData, light_dir1)},
      {"light_dir2", offsetof(MercLightControlUniformBufferVertexData, light_dir2)},
      {"light_col0", offsetof(MercLightControlUniformBufferVertexData, light_col0)},
      {"light_col1", offsetof(MercLightControlUniformBufferVertexData, light_col1)},
      {"light_col2", offsetof(MercLightControlUniformBufferVertexData, light_col2)},
      {"light_ambient", offsetof(MercLightControlUniformBufferVertexData, light_ambient)}
  };
}

MercCameraControlVertexUniformBuffer::MercCameraControlVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                       uint32_t instanceCount,
                                       VkDeviceSize minOffsetAlignment)
  : UniformVulkanBuffer(device,
                    sizeof(MercCameraControlUniformBufferVertexData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"hvdf_offset", offsetof(MercCameraControlUniformBufferVertexData, hvdf_offset)},
      {"perspective0", offsetof(MercCameraControlUniformBufferVertexData, perspective0)},
      {"perspective1", offsetof(MercCameraControlUniformBufferVertexData, perspective1)},
      {"perspective2", offsetof(MercCameraControlUniformBufferVertexData, perspective2)},
      {"perspective3", offsetof(MercCameraControlUniformBufferVertexData, perspective3)},
      {"fog_constants", offsetof(MercCameraControlUniformBufferVertexData, fog_constants)},
  };
}

MercPerspectiveMatrixVertexUniformBuffer::MercPerspectiveMatrixVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                           uint32_t instanceCount,
                                           VkDeviceSize minOffsetAlignment)
  : UniformVulkanBuffer(device,
                    sizeof(MercPerspectiveMatrixUniformBufferVertexData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"perspective_matrix",
       offsetof(MercPerspectiveMatrixUniformBufferVertexData, perspective_matrix)}
  };
};

MercFragmentUniformBuffer::MercFragmentUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                     uint32_t instanceCount,
                                                     VkDeviceSize minOffsetAlignment) :
  UniformVulkanBuffer(device,
                    sizeof(MercUniformBufferFragmentData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"fog_color", offsetof(MercUniformBufferFragmentData, fog_color)},
      {"ignore_alpha", offsetof(MercUniformBufferFragmentData, ignore_alpha)},
      {"decal_enable", offsetof(MercUniformBufferFragmentData, decal_enable)},
      {"gfx_hack_no_tex", offsetof(MercUniformBufferFragmentData, gfx_hack_no_tex)}};
}

MercVulkan2::MercBoneVertexUniformBuffer::MercBoneVertexUniformBuffer(
                                                     std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                     VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                          sizeof(BaseMerc2::ShaderMercMat),
                          128,
                          minOffsetAlignment) {
}


MercVulkan2::~MercVulkan2() {
}
