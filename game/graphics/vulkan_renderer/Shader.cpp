#include "Shader.h"

#include "common/log/log.h"
#include "common/util/Assert.h"
#include "common/util/FileUtil.h"

#include "game/graphics/pipelines/vulkan.h"

VkShaderModule Shader::PopulateShader(const std::string& code) {
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
      file_util::read_text_file(file_util::get_file_path({shader_folder, shader_name + ".vert.spv"}));
  auto frag_src =
      file_util::read_text_file(file_util::get_file_path({shader_folder, shader_name + ".frag.spv"}));

  m_vert_shader = PopulateShader(vert_src);
  m_frag_shader = PopulateShader(frag_src);

  m_is_okay = true;
}

Shader::~Shader() {
  if (m_device != VK_NULL_HANDLE && m_vert_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, m_vert_shader, nullptr);
  }
  if (m_device != VK_NULL_HANDLE && m_frag_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, m_frag_shader, nullptr);
  }
}

ShaderLibrary::ShaderLibrary(VkDevice device) : m_device(device) {
  at(ShaderId::SOLID_COLOR) = {m_device, "solid_color"};
  at(ShaderId::DIRECT_BASIC) = {m_device, "direct_basic"};
  at(ShaderId::DIRECT_BASIC_TEXTURED) = {m_device, "direct_basic_textured"};
  at(ShaderId::DEBUG_RED) = {m_device, "debug_red"};
  at(ShaderId::SPRITE) = {m_device, "sprite_3d"};
  at(ShaderId::SKY) = {m_device, "sky"};
  at(ShaderId::SKY_BLEND) = {m_device, "sky_blend"};
  at(ShaderId::TFRAG3) = {m_device, "tfrag3"};
  at(ShaderId::TFRAG3_NO_TEX) = {m_device, "tfrag3_no_tex"};
  at(ShaderId::SPRITE3) = {m_device, "sprite3_3d"};
  at(ShaderId::DIRECT2) = {m_device, "direct2"};
  at(ShaderId::EYE) = {m_device, "eye"};
  at(ShaderId::GENERIC) = {m_device, "generic"};
  at(ShaderId::OCEAN_TEXTURE) = {m_device, "ocean_texture"};
  at(ShaderId::OCEAN_TEXTURE_MIPMAP) = {m_device, "ocean_texture_mipmap"};
  at(ShaderId::OCEAN_COMMON) = {m_device, "ocean_common"};
  at(ShaderId::SHRUB) = {m_device, "shrub"};
  at(ShaderId::SHADOW) = {m_device, "shadow"};
  at(ShaderId::COLLISION) = {m_device, "collision"};
  at(ShaderId::MERC2) = {m_device, "merc2"};
  at(ShaderId::SPRITE_DISTORT) = {m_device, "sprite_distort"};
  at(ShaderId::SPRITE_DISTORT_INSTANCED) = {m_device, "sprite_distort_instanced"};
  at(ShaderId::POST_PROCESSING) = {m_device, "post_processing"};
  at(ShaderId::DEPTH_CUE) = {m_device, "depth_cue"};

  for (auto& shader : m_shaders) {
    ASSERT_MSG(shader.okay(), "Shader compiled");
  }
}
