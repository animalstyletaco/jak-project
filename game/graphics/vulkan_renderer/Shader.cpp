#include "Shader.h"

#include "common/log/log.h"
#include "common/util/Assert.h"
#include "common/util/FileUtil.h"

VkShaderModule Shader::PopulateShader(const std::vector<u8>& code) {
  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size();
  create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shader_module;
  if (vkCreateShaderModule(m_device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
    lg::error("failed to create shader module!");
    m_is_okay = false;
    throw std::runtime_error("failed to create shader module!");
  }

  return shader_module;
}

Shader::Shader(VkDevice device, const std::string& shader_name)
    : m_device(device) ,shader_name(shader_name) {
  // read the shader source
  auto vert_src =
      file_util::read_binary_file(file_util::get_file_path({shader_folder, shader_name + ".vert.spv"}));
  auto frag_src =
      file_util::read_binary_file(file_util::get_file_path({shader_folder, shader_name + ".frag.spv"}));

  m_vert_shader = PopulateShader(vert_src);
  m_frag_shader = PopulateShader(frag_src);

  m_is_okay = true;
}

Shader::~Shader() {
  vkDestroyShaderModule(m_device, m_vert_shader, nullptr);
  vkDestroyShaderModule(m_device, m_frag_shader, nullptr);
}

ShaderLibrary::ShaderLibrary(std::unique_ptr<GraphicsDeviceVulkan>& device) : m_device(device) {
  at(ShaderId::SOLID_COLOR) = {m_device->getLogicalDevice(), "solid_color"};
  at(ShaderId::DIRECT_BASIC) = {m_device->getLogicalDevice(), "direct_basic"};
  at(ShaderId::DIRECT_BASIC_TEXTURED) = {m_device->getLogicalDevice(), "direct_basic_textured"};
  at(ShaderId::DEBUG_RED) = {m_device->getLogicalDevice(), "debug_red"};
  at(ShaderId::SPRITE) = {m_device->getLogicalDevice(), "sprite_3d"};
  at(ShaderId::SKY) = {m_device->getLogicalDevice(), "sky"};
  at(ShaderId::SKY_BLEND) = {m_device->getLogicalDevice(), "sky_blend"};
  at(ShaderId::TFRAG3) = {m_device->getLogicalDevice(), "tfrag3"};
  at(ShaderId::TFRAG3_NO_TEX) = {m_device->getLogicalDevice(), "tfrag3_no_tex"};
  at(ShaderId::SPRITE3) = {m_device->getLogicalDevice(), "sprite3_3d"};
  at(ShaderId::DIRECT2) = {m_device->getLogicalDevice(), "direct2"};
  at(ShaderId::EYE) = {m_device->getLogicalDevice(), "eye"};
  at(ShaderId::GENERIC) = {m_device->getLogicalDevice(), "generic"};
  at(ShaderId::OCEAN_TEXTURE) = {m_device->getLogicalDevice(), "ocean_texture"};
  at(ShaderId::OCEAN_TEXTURE_MIPMAP) = {m_device->getLogicalDevice(), "ocean_texture_mipmap"};
  at(ShaderId::OCEAN_COMMON) = {m_device->getLogicalDevice(), "ocean_common"};
  at(ShaderId::SHRUB) = {m_device->getLogicalDevice(), "shrub"};
  at(ShaderId::SHADOW) = {m_device->getLogicalDevice(), "shadow"};
  at(ShaderId::COLLISION) = {m_device->getLogicalDevice(), "collision"};
  at(ShaderId::MERC2) = {m_device->getLogicalDevice(), "merc2"};
  at(ShaderId::SPRITE_DISTORT) = {m_device->getLogicalDevice(), "sprite_distort"};
  at(ShaderId::SPRITE_DISTORT_INSTANCED) = {m_device->getLogicalDevice(), "sprite_distort_instanced"};
  at(ShaderId::POST_PROCESSING) = {m_device->getLogicalDevice(), "post_processing"};
  at(ShaderId::DEPTH_CUE) = {m_device->getLogicalDevice(), "depth_cue"};

  for (auto& shader : m_shaders) {
    ASSERT_MSG(shader.okay(), "Shader compiled");
  }
}
