#include "TFragment.h"

#include "third-party/imgui/imgui.h"

TFragmentVulkan::TFragmentVulkan(const std::string& name,
                     int my_id,
                     std::unique_ptr<GraphicsDeviceVulkan>& device,
                     VulkanInitializationInfo& vulkan_info,
                     const std::vector<tfrag3::TFragmentTreeKind>& trees,
                     bool child_mode,
                     int level_id)
    : BaseTFragment(name, my_id, trees, child_mode, level_id), BucketVulkanRenderer(device, vulkan_info) {
  m_pipeline_layouts.resize(1, m_device);
  m_vertex_shader_uniform_buffer = std::make_unique<BackgroundCommonVertexUniformBuffer>(
      device, 1, 1);
  m_time_of_day_color = std::make_unique<BackgroundCommonFragmentUniformBuffer>(
      device, 1, 1);

  m_vertex_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_VERTEX_BIT)
          .build();

  m_fragment_descriptor_layout =
      DescriptorLayout::Builder(m_device)
          .addBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  m_vertex_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_vertex_descriptor_layout, vulkan_info.descriptor_pool);

  m_fragment_descriptor_writer =
      std::make_unique<DescriptorWriter>(m_fragment_descriptor_layout, vulkan_info.descriptor_pool);

  m_descriptor_sets.resize(2);
  auto vertex_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &vertex_buffer_descriptor_info)
      .build(m_descriptor_sets[0]);
  auto fragment_buffer_descriptor_info = m_vertex_shader_uniform_buffer->descriptorInfo();
  m_vertex_descriptor_writer->writeBuffer(0, &fragment_buffer_descriptor_info)
      .build(m_descriptor_sets[1]);

  m_tfrag3 = std::make_unique<Tfrag3Vulkan>(vulkan_info, m_pipeline_config_info, m_pipeline_layouts[0],
                                      m_vertex_descriptor_writer, m_fragment_descriptor_writer,
                                      m_vertex_shader_uniform_buffer, m_time_of_day_color);
}

void TFragmentVulkan::render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) {
  BaseTFragment::render(dma, render_state, prof);
}

void TFragmentVulkan::InitializeDebugInputVertexAttribute() {
  m_pipeline_config_info.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  m_pipeline_config_info.inputAssemblyInfo.primitiveRestartEnable = VK_TRUE;

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(BaseTfrag3::DebugVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(BaseTfrag3::DebugVertex, position);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(BaseTfrag3::DebugVertex, rgba);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}

void TFragmentVulkan::InitializeInputVertexAttribute() {
  //            glBufferData(GL_ARRAY_BUFFER, verts * sizeof(tfrag3::PreloadedVertex),
  //            nullptr,
  //                         GL_STREAM_DRAW);
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::PreloadedVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  m_pipeline_config_info.bindingDescriptions.push_back(bindingDescription);

  std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::PreloadedVertex, x);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::PreloadedVertex, s);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R16_UINT;
  attributeDescriptions[2].offset = offsetof(tfrag3::PreloadedVertex, color_index);
  m_pipeline_config_info.attributeDescriptions.insert(
      m_pipeline_config_info.attributeDescriptions.end(), attributeDescriptions.begin(),
      attributeDescriptions.end());
}

void TFragmentVulkan::tfrag3_setup_for_level(
    const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
    const std::string& level,
    BaseSharedRenderState* render_state) {
  m_tfrag3->setup_for_level(tree_kinds, level, render_state);
}

void TFragmentVulkan::tfrag3_render_matching_trees(
    int geom,
    const std::vector<tfrag3::TFragmentTreeKind>& trees,
    const TfragRenderSettings& settings,
    BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) {
  m_tfrag3->render_matching_trees(geom, trees, settings, render_state, prof);
}

void TFragmentVulkan::draw_debug_window() {
  m_tfrag3->draw_debug_window();
}

int TFragmentVulkan::tfrag3_lod() {
  return m_tfrag3->lod();
}
