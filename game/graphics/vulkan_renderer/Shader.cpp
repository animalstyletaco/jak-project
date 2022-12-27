
#include <regex>

#include "Shader.h"

#include "common/log/log.h"
#include "common/util/Assert.h"
#include "common/util/FileUtil.h"

VkShaderModule VulkanShader::PopulateShader(const std::vector<u8>& code) {
  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size();
  create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shader_module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(m_device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
    lg::error("failed to create shader module!");
    m_is_okay = false;
    throw std::runtime_error("failed to create shader module!");
  }

  return shader_module;
}

VulkanShader::VulkanShader(VkDevice device, const std::string& shader_name, GameVersion version)
    : m_device(device) ,shader_name(shader_name), m_version(version) {
  initialize_shader(device, shader_name, version);
}

void VulkanShader::initialize_shader(VkDevice device, const std::string& shader_name, GameVersion version) {
  if (!m_device) {
    m_device = device;
  }

  auto vert_src = file_util::read_binary_file(
      file_util::get_file_path({shader_folder, shader_name + ".vert.spv"}));
  auto frag_src = file_util::read_binary_file(
      file_util::get_file_path({shader_folder, shader_name + ".frag.spv"}));

  m_vert_shader = PopulateShader(vert_src);
  m_frag_shader = PopulateShader(frag_src);

  m_is_okay = true;
};

VulkanShader::VulkanShader(const VulkanShader& shader) {
  m_device = shader.m_device;
  shader_name = shader.shader_name;
  m_version = shader.m_version;

  initialize_shader(m_device, shader_name, m_version);
}

VulkanShader::~VulkanShader() {
  if (m_is_okay) {
    vkDestroyShaderModule(m_device, m_vert_shader, nullptr);
    vkDestroyShaderModule(m_device, m_frag_shader, nullptr);
    m_is_okay = false;

    m_vert_shader = VK_NULL_HANDLE;
    m_frag_shader = VK_NULL_HANDLE;
  }
}

VulkanShaderLibrary::VulkanShaderLibrary(std::unique_ptr<GraphicsDeviceVulkan>& device, GameVersion version) : m_device(device) {
  at(ShaderId::SOLID_COLOR).initialize_shader(m_device->getLogicalDevice(), "solid_color", version);
  at(ShaderId::DIRECT_BASIC).initialize_shader(m_device->getLogicalDevice(), "direct_basic", version);
  at(ShaderId::DIRECT_BASIC_TEXTURED).initialize_shader(m_device->getLogicalDevice(), "direct_basic_textured", version);
  at(ShaderId::DEBUG_RED).initialize_shader(m_device->getLogicalDevice(), "debug_red", version);
  at(ShaderId::SPRITE).initialize_shader(m_device->getLogicalDevice(), "sprite_3d", version);
  at(ShaderId::SKY).initialize_shader(m_device->getLogicalDevice(), "sky", version);
  at(ShaderId::SKY_BLEND).initialize_shader(m_device->getLogicalDevice(), "sky_blend", version);
  at(ShaderId::TFRAG3).initialize_shader(m_device->getLogicalDevice(), "tfrag3", version);
  at(ShaderId::TFRAG3_NO_TEX).initialize_shader(m_device->getLogicalDevice(), "tfrag3_no_tex", version);
  at(ShaderId::SPRITE3).initialize_shader(m_device->getLogicalDevice(), "sprite3_3d", version);
  at(ShaderId::DIRECT2).initialize_shader(m_device->getLogicalDevice(), "direct2", version);
  at(ShaderId::EYE).initialize_shader(m_device->getLogicalDevice(), "eye", version);
  at(ShaderId::GENERIC).initialize_shader(m_device->getLogicalDevice(), "generic", version);
  at(ShaderId::OCEAN_TEXTURE).initialize_shader(m_device->getLogicalDevice(), "ocean_texture", version);
  at(ShaderId::OCEAN_TEXTURE_MIPMAP).initialize_shader(m_device->getLogicalDevice(), "ocean_texture_mipmap", version);
  at(ShaderId::OCEAN_COMMON).initialize_shader(m_device->getLogicalDevice(), "ocean_common", version);
  at(ShaderId::SHRUB).initialize_shader(m_device->getLogicalDevice(), "shrub", version);
  at(ShaderId::SHADOW).initialize_shader(m_device->getLogicalDevice(), "shadow", version);
  at(ShaderId::COLLISION).initialize_shader(m_device->getLogicalDevice(), "collision", version);
  at(ShaderId::MERC2).initialize_shader(m_device->getLogicalDevice(), "merc2", version);
  at(ShaderId::SPRITE_DISTORT).initialize_shader(m_device->getLogicalDevice(), "sprite_distort", version);
  at(ShaderId::SPRITE_DISTORT_INSTANCED).initialize_shader(m_device->getLogicalDevice(), "sprite_distort_instanced", version);
  at(ShaderId::POST_PROCESSING).initialize_shader(m_device->getLogicalDevice(), "post_processing", version);
  at(ShaderId::DEPTH_CUE).initialize_shader(m_device->getLogicalDevice(), "depth_cue", version);

  for (auto& shader : m_shaders) {
    ASSERT_MSG(shader.okay(), "Shader compiled");
  }
}
