#include "OceanNear.h"

#include "third-party/imgui/imgui.h"

OceanNearVulkan::OceanNearVulkan(const std::string& name,
                     int my_id,
                     std::unique_ptr<GraphicsDeviceVulkan>& device,
                     VulkanInitializationInfo& vulkan_info)
    : BaseOceanNear(name, my_id), BucketVulkanRenderer(device, vulkan_info), m_texture_renderer(false, device, vulkan_info), m_common_ocean_renderer(device, vulkan_info) {
  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  m_ocean_vertex_uniform_buffer = std::make_unique<CommonOceanVertexUniformBuffer>(
      m_device, 1, 1);
  m_ocean_fragment_uniform_buffer = std::make_unique<CommonOceanFragmentUniformBuffer>(
      m_device, 1, 1);

  auto vertex_buffer_descriptor_info = m_ocean_vertex_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_ocean_fragment_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);

  for (auto& a : m_vu_data) {
    a.fill(0);
  }
}

void OceanNearVulkan::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  BaseOceanNear::render(dma, render_state, prof);
}

void OceanNearVulkan::init_textures(TexturePoolVulkan& pool) {
  m_texture_renderer.init_textures(pool);
}

static bool is_end_tag(const DmaTag& tag, const VifCode& v0, const VifCode& v1) {
  return tag.qwc == 2 && tag.kind == DmaTag::Kind::CNT && v0.kind == VifCode::Kind::NOP &&
         v1.kind == VifCode::Kind::DIRECT;
}


