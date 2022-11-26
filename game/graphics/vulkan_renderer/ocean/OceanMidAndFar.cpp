#include "OceanMidAndFar.h"

#include "third-party/imgui/imgui.h"

OceanMidAndFar::OceanMidAndFar(const std::string& name,
                               int my_id,
                               std::unique_ptr<GraphicsDeviceVulkan>& device,
                               VulkanInitializationInfo& vulkan_info)
    : BaseOceanMidAndFar(name, my_id), BucketVulkanRenderer(device, vulkan_info),
      m_direct(name, my_id, device, vulkan_info, 4096),
      m_texture_renderer(true, device, vulkan_info), m_mid_renderer(device, vulkan_info) {
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
  m_ocean_uniform_vertex_buffer = std::make_unique<CommonOceanVertexUniformBuffer>(
      m_device, 1, 1);
  m_ocean_uniform_fragment_buffer = std::make_unique<CommonOceanFragmentUniformBuffer>(
      m_device, 1, 1);

  auto vertex_buffer_descriptor_info = m_ocean_uniform_vertex_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_ocean_uniform_fragment_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);
}

void OceanMidAndFar::draw_debug_window() {
  m_texture_renderer.draw_debug_window();
  m_direct.draw_debug_window();
}

void OceanMidAndFar::init_textures(TexturePoolVulkan& pool) {
  m_texture_renderer.init_textures(pool);
}

void OceanMidAndFar::handle_ocean_far(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  auto init_data = dma.read_and_advance();
  ASSERT(init_data.size_bytes == 160);
  u8 init_data_buffer[160];
  memcpy(init_data_buffer, init_data.data, 160);

  // this is a bit of a hack, but it patches the ta0 to 0 in
  // (set! (-> (the-as (pointer gs-texa) s4-0) 8) (new 'static 'gs-texa :ta0 #x80 :ta1 #x80))
  // TODO figure out if we actually have do something here.
  u8 val = 0;
  memcpy(init_data_buffer + 80, &val, 1);
  m_direct.render_gif(init_data_buffer, 160, render_state, prof);

  while (dma.current_tag().kind == DmaTag::Kind::CNT &&
         dma.current_tag_vifcode0().kind == VifCode::Kind::NOP) {
    auto data = dma.read_and_advance();
    ASSERT(data.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(data.vifcode1().kind == VifCode::Kind::DIRECT);
    ASSERT(data.size_bytes / 16 == data.vifcode1().immediate);
    m_direct.render_gif(data.data, data.size_bytes, render_state, prof);
  }
}

bool is_end_tag(const DmaTag& tag, const VifCode& v0, const VifCode& v1) {
  return tag.qwc == 0 && tag.kind == DmaTag::Kind::NEXT && v0.kind == VifCode::Kind::NOP &&
         v1.kind == VifCode::Kind::NOP;
}
void OceanMidAndFar::handle_ocean_mid(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  if (dma.current_tag_vifcode0().kind == VifCode::Kind::BASE) {
    m_mid_renderer.run(dma, render_state, prof, m_ocean_uniform_vertex_buffer,
                       m_ocean_uniform_fragment_buffer);
  } else {
    // not drawing
    return;
  }

  while (!is_end_tag(dma.current_tag(), dma.current_tag_vifcode0(), dma.current_tag_vifcode1())) {
    dma.read_and_advance();
  }
}
