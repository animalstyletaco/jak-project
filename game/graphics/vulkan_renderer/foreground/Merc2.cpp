#include "Merc2.h"

#include "game/graphics/vulkan_renderer/background/background_common.h"

#include "third-party/imgui/imgui.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

MercVulkan2::MercVulkan2(const std::string& name,
             int my_id,
             std::unique_ptr<GraphicsDeviceVulkan>& device,
             VulkanInitializationInfo& vulkan_info) :
  BaseMerc2(name, my_id), BucketVulkanRenderer(device, vulkan_info) {
  std::vector<u8> temp(MAX_SHADER_BONE_VECTORS * sizeof(math::Vector4f));
  //m_bones_buffer = CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, temp);

  //TODO: Figure what the vulkan equivalent of OpenGL's check buffer offset alignment is
  m_graphics_buffer_alignment = 1;
  m_vertex_uniform_buffer = std::make_unique<MercVertexUniformBuffer>(
      m_device, sizeof(MercUniformBufferVertexData),
      1);

  m_fragment_uniform_buffer = std::make_unique<MercFragmentUniformBuffer>(
      m_device, sizeof(MercUniformBufferFragmentData), 1);

    m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, m_vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer = std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout,
                                                                    m_vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  auto vertex_buffer_descriptor_info = m_vertex_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_fragment_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);

  for (int i = 0; i < MAX_LEVELS; i++) {
    auto& draws = m_level_draw_buckets.emplace_back();
    draws.draws.resize(MAX_DRAWS_PER_LEVEL);
  }
  InitializeInputAttributes();
}

/*!
 * Main MercVulkan2 rendering.
 */
void MercVulkan2::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  BaseMerc2::render(dma, render_state, prof);
}

void MercVulkan2::handle_setup_dma(DmaFollower& dma, BaseSharedRenderState* render_state) {
  auto first = dma.read_and_advance();

  // 10 quadword setup packet
  ASSERT(first.size_bytes == 10 * 16);
  // m_stats.str += fmt::format("Setup 0: {} {} {}", first.size_bytes / 16,
  // first.vifcode0().print(), first.vifcode1().print());

  // transferred vifcodes
  {
    auto vif0 = first.vifcode0();
    auto vif1 = first.vifcode1();
    // STCYCL 4, 4
    ASSERT(vif0.kind == VifCode::Kind::STCYCL);
    auto vif0_st = VifCodeStcycl(vif0);
    ASSERT(vif0_st.cl == 4 && vif0_st.wl == 4);
    // STMOD
    ASSERT(vif1.kind == VifCode::Kind::STMOD);
    ASSERT(vif1.immediate == 0);
  }

  // 1 qw with 4 vifcodes.
  u32 vifcode_data[4];
  memcpy(vifcode_data, first.data, 16);
  {
    auto vif0 = VifCode(vifcode_data[0]);
    ASSERT(vif0.kind == VifCode::Kind::BASE);
    ASSERT(vif0.immediate == MercDataMemory::BUFFER_BASE);
    auto vif1 = VifCode(vifcode_data[1]);
    ASSERT(vif1.kind == VifCode::Kind::OFFSET);
    ASSERT((s16)vif1.immediate == MercDataMemory::BUFFER_OFFSET);
    auto vif2 = VifCode(vifcode_data[2]);
    ASSERT(vif2.kind == VifCode::Kind::NOP);
    auto vif3 = VifCode(vifcode_data[3]);
    ASSERT(vif3.kind == VifCode::Kind::UNPACK_V4_32);
    VifCodeUnpack up(vif3);
    ASSERT(up.addr_qw == MercDataMemory::LOW_MEMORY);
    ASSERT(!up.use_tops_flag);
    ASSERT(vif3.num == 8);
  }

  // 8 qw's of low memory data
  memcpy(&m_low_memory, first.data + 16, sizeof(LowMemory));
  m_vertex_uniform_buffer->SetUniformMathVector4f("hvdf_offset", m_low_memory.hvdf_offset);
  m_vertex_uniform_buffer->SetUniformMathVector4f("fog_constants", m_low_memory.fog);
  for (int i = 0; i < 4; i++) {
    m_vertex_uniform_buffer->SetUniformMathVector4f((std::string("perspective") + std::to_string(i)).c_str(), //Ugly declaration
                                                                  m_low_memory.perspective[i]);
  }
  // todo rm.
  m_vertex_uniform_buffer->Set4x4MatrixDataInVkDeviceMemory(
      "perspective_matrix", 1, GL_FALSE, &m_low_memory.perspective[0].x());

  // 1 qw with another 4 vifcodes.
  u32 vifcode_final_data[4];
  memcpy(vifcode_final_data, first.data + 16 + sizeof(LowMemory), 16);
  {
    ASSERT(VifCode(vifcode_final_data[0]).kind == VifCode::Kind::FLUSHE);
    ASSERT(vifcode_final_data[1] == 0);
    ASSERT(vifcode_final_data[2] == 0);
    VifCode mscal(vifcode_final_data[3]);
    ASSERT(mscal.kind == VifCode::Kind::MSCAL);
    ASSERT(mscal.immediate == 0);
  }

  // TODO: process low memory initialization

  auto second = dma.read_and_advance();
  ASSERT(second.size_bytes == 32);  // setting up test register.
  auto nothing = dma.read_and_advance();
  ASSERT(nothing.size_bytes == 0);
  ASSERT(nothing.vif0() == 0);
  ASSERT(nothing.vif1() == 0);
}

void MercVulkan2::flush_draw_buckets(BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  m_stats.num_draw_flush++;

  for (u32 li = 0; li < m_next_free_level_bucket; li++) {
    auto& lev_bucket = m_level_draw_buckets[li];
    auto* lev = lev_bucket.level;

    int last_tex = -1;
    int last_light = -1;
    m_stats.num_bones_uploaded += m_next_free_bone_vector;

    //void* data = NULL;
    //vkMapMemory(device, m_bones_buffer_memory, 0, m_next_free_bone_vector * sizeof(math::Vector4f), 0, &data);
    //::memcpy(data, m_shader_bone_vector_buffer, m_next_free_bone_vector * sizeof(math::Vector4f));
    //vkUnmapMemory(device, m_bones_buffer_memory, nullptr);

    for (u32 di = 0; di < lev_bucket.next_free_draw; di++) {
      auto& draw = lev_bucket.draws[di];
      auto& textureInfo = lev->textures[draw.texture];
      m_vertex_uniform_buffer->SetUniform1i("ignore_alpha", draw.ignore_alpha);

      if ((int)draw.light_idx != last_light) {
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_direction0", m_lights_buffer[draw.light_idx].direction0);
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_direction1", m_lights_buffer[draw.light_idx].direction1);
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_direction2", m_lights_buffer[draw.light_idx].direction2);
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_color0", m_lights_buffer[draw.light_idx].color0);
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_color1", m_lights_buffer[draw.light_idx].color1);
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_color2", m_lights_buffer[draw.light_idx].color2);
        m_vertex_uniform_buffer->SetUniformMathVector3f("light_ambient", m_lights_buffer[draw.light_idx].ambient);
        last_light = draw.light_idx;
      }
      background_common::setup_vulkan_from_draw_mode(draw.mode, (VulkanTexture*)&textureInfo, m_pipeline_config_info, true);

      m_fragment_uniform_buffer->SetUniform1i("decal_enable", draw.mode.get_decal());

      prof.add_draw_call();
      prof.add_tri(draw.num_triangles);

      //void* data = NULL;
      //vkMapMemory(device, textureInfo.texture_memory, sizeof(math::Vector4f) * draw.first_bone,
      //            128 * sizeof(ShaderMercMat), 0, &data);
      //::memcpy(data, m_skel_matrix_buffer, 128 * sizeof(ShaderMercMat)); //TODO: Need to check that this is thread safe
      //vkUnmapMemory(device, textureInfo.texture_memory, nullptr);

      //Set up index buffer from draw object

      //glDrawElements(GL_TRIANGLE_STRIP, draw.index_count, GL_UNSIGNED_INT,
      //               (void*)(sizeof(u32) * draw.first_index));
    }
  }

  m_next_free_light = 0;
  m_next_free_bone_vector = 0;
  m_next_free_level_bucket = 0;
}

void MercVulkan2::init_shaders(VulkanShaderLibrary& shaders) {
  auto& shader = shaders[ShaderId::MERC2];

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader.GetVertexShader();
  vertShaderStageInfo.pName = "MercVulkan2 Vertex";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader.GetFragmentShader();
  fragShaderStageInfo.pName = "MercVulkan2 Fragment";

  m_pipeline_config_info.shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
}

void MercVulkan2::InitializeInputAttributes() {
  VkVertexInputBindingDescription mercVertexBindingDescription{};
  mercVertexBindingDescription.binding = 0;
  mercVertexBindingDescription.stride = sizeof(tfrag3::MercVertex);
  mercVertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputBindingDescription mercMatrixBindingDescription{};
  mercMatrixBindingDescription.binding = 1;
  mercMatrixBindingDescription.stride = 128 * sizeof(ShaderMercMat);
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
  attributeDescriptions[4].format = VK_FORMAT_R4G4_UNORM_PACK8;
  attributeDescriptions[4].offset = offsetof(tfrag3::MercVertex, rgba[0]);

  attributeDescriptions[5].binding = 0;
  attributeDescriptions[5].location = 5;
  attributeDescriptions[5].format = VK_FORMAT_R4G4_UNORM_PACK8;
  attributeDescriptions[5].offset = offsetof(tfrag3::MercVertex, mats[0]);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());

  std::array<VkVertexInputAttributeDescription, 2> matrixAttributeDescriptions{};
  matrixAttributeDescriptions[0].binding = 1;
  matrixAttributeDescriptions[0].location = 0;
  matrixAttributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  matrixAttributeDescriptions[0].offset = offsetof(ShaderMercMat, tmat[0]);

  matrixAttributeDescriptions[1].binding = 1;
  matrixAttributeDescriptions[1].location = 1;
  matrixAttributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  matrixAttributeDescriptions[1].offset = offsetof(ShaderMercMat, nmat[0]);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), matrixAttributeDescriptions.begin(),
      matrixAttributeDescriptions.end());

  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  m_pipeline_config_info.depthStencilInfo.depthTestEnable = VK_TRUE;
  m_pipeline_config_info.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
}

MercVertexUniformBuffer::MercVertexUniformBuffer(std::unique_ptr<GraphicsDeviceVulkan>& device,
                                                 uint32_t instanceCount,
                                                 VkDeviceSize minOffsetAlignment)
    : UniformVulkanBuffer(device,
                    sizeof(MercUniformBufferVertexData),
                    instanceCount,
                    minOffsetAlignment) {
  section_name_to_memory_offset_map = {
      {"light_dir0", offsetof(MercUniformBufferVertexData, light_system.light_dir0)},
      {"light_dir1", offsetof(MercUniformBufferVertexData, light_system.light_dir1)},
      {"light_dir2", offsetof(MercUniformBufferVertexData, light_system.light_dir2)},
      {"light_col0", offsetof(MercUniformBufferVertexData, light_system.light_col0)},
      {"light_col1", offsetof(MercUniformBufferVertexData, light_system.light_col1)},
      {"light_col2", offsetof(MercUniformBufferVertexData, light_system.light_col2)},
      {"light_ambient", offsetof(MercUniformBufferVertexData, light_system.light_ambient)},
      {"hvdf_offset", offsetof(MercUniformBufferVertexData, camera_system.hvdf_offset)},
      {"perspective0", offsetof(MercUniformBufferVertexData, camera_system.perspective0)},
      {"perspective1", offsetof(MercUniformBufferVertexData, camera_system.perspective1)},
      {"perspective2", offsetof(MercUniformBufferVertexData, camera_system.perspective2)},
      {"perspective3", offsetof(MercUniformBufferVertexData, camera_system.perspective3)},
      {"fog_constants", offsetof(MercUniformBufferVertexData, camera_system.fog_constants)},
      {"perspective_matrix", offsetof(MercUniformBufferVertexData, perspective_matrix)}};
}

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
      {"decal_enable", offsetof(MercUniformBufferFragmentData, decal_enable)}};
}


MercVulkan2::~MercVulkan2() {
}
