
#include "Shader.h"

#include <regex>

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

VulkanShader::VulkanShader(VkDevice device, const std::string& shader_name)
    : m_device(device), shader_name(shader_name) {
  initialize_shader(device, shader_name);
}

void VulkanShader::initialize_shader(VkDevice device, const std::string& shader_name) {
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

  initialize_shader(m_device, shader_name);
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

VulkanShaderLibrary::VulkanShaderLibrary(std::shared_ptr<GraphicsDeviceVulkan> device)
    : m_device(device) {
  at(ShaderId::SOLID_COLOR).initialize_shader(m_device->getLogicalDevice(), "solid_color");
  at(ShaderId::DIRECT_BASIC).initialize_shader(m_device->getLogicalDevice(), "direct_basic");
  at(ShaderId::DIRECT_BASIC_TEXTURED)
      .initialize_shader(m_device->getLogicalDevice(), "direct_basic_textured");
  at(ShaderId::DEBUG_RED).initialize_shader(m_device->getLogicalDevice(), "debug_red");
  at(ShaderId::SPRITE).initialize_shader(m_device->getLogicalDevice(), "sprite_3d");
  at(ShaderId::SKY).initialize_shader(m_device->getLogicalDevice(), "sky");
  at(ShaderId::SKY_BLEND).initialize_shader(m_device->getLogicalDevice(), "sky_blend");
  at(ShaderId::TFRAG3).initialize_shader(m_device->getLogicalDevice(), "tfrag3");
  at(ShaderId::TFRAG3_NO_TEX).initialize_shader(m_device->getLogicalDevice(), "tfrag3_no_tex");
  at(ShaderId::SPRITE3).initialize_shader(m_device->getLogicalDevice(), "sprite3_3d");
  at(ShaderId::DIRECT2).initialize_shader(m_device->getLogicalDevice(), "direct2");
  at(ShaderId::EYE).initialize_shader(m_device->getLogicalDevice(), "eye");
  at(ShaderId::GENERIC).initialize_shader(m_device->getLogicalDevice(), "generic");
  at(ShaderId::OCEAN_TEXTURE).initialize_shader(m_device->getLogicalDevice(), "ocean_texture");
  at(ShaderId::OCEAN_TEXTURE_MIPMAP)
      .initialize_shader(m_device->getLogicalDevice(), "ocean_texture_mipmap");
  at(ShaderId::OCEAN_COMMON).initialize_shader(m_device->getLogicalDevice(), "ocean_common");
  at(ShaderId::SHRUB).initialize_shader(m_device->getLogicalDevice(), "shrub");
  at(ShaderId::SHADOW).initialize_shader(m_device->getLogicalDevice(), "shadow");
  at(ShaderId::COLLISION).initialize_shader(m_device->getLogicalDevice(), "collision");
  at(ShaderId::MERC2).initialize_shader(m_device->getLogicalDevice(), "merc2");
  at(ShaderId::SPRITE_DISTORT).initialize_shader(m_device->getLogicalDevice(), "sprite_distort");
  at(ShaderId::SPRITE_DISTORT_INSTANCED)
      .initialize_shader(m_device->getLogicalDevice(), "sprite_distort_instanced");
  at(ShaderId::POST_PROCESSING).initialize_shader(m_device->getLogicalDevice(), "post_processing");
  at(ShaderId::DEPTH_CUE).initialize_shader(m_device->getLogicalDevice(), "depth_cue");
  at(ShaderId::EMERC).initialize_shader(m_device->getLogicalDevice(), "emerc");
  at(ShaderId::GLOW_PROBE).initialize_shader(m_device->getLogicalDevice(), "glow_probe");
  at(ShaderId::GLOW_PROBE_READ).initialize_shader(m_device->getLogicalDevice(), "glow_probe_read");
  at(ShaderId::GLOW_PROBE_READ_DEBUG)
      .initialize_shader(m_device->getLogicalDevice(), "glow_probe_read_debug");
  at(ShaderId::GLOW_PROBE_DOWNSAMPLE)
      .initialize_shader(m_device->getLogicalDevice(), "glow_probe_downsample");
  at(ShaderId::GLOW_DRAW).initialize_shader(m_device->getLogicalDevice(), "glow_draw");
  at(ShaderId::ETIE_BASE).initialize_shader(m_device->getLogicalDevice(), "etie_base");
  at(ShaderId::ETIE).initialize_shader(m_device->getLogicalDevice(), "etie");
  at(ShaderId::SHADOW2).initialize_shader(m_device->getLogicalDevice(), "shadow2");
  at(ShaderId::DIRECT_BASIC_TEXTURED_MULTI_UNIT)
      .initialize_shader(m_device->getLogicalDevice(), "direct_basic_textured_multi_unit");
  at(ShaderId::TEX_ANIM).initialize_shader(m_device->getLogicalDevice(), "tex_anim");
  at(ShaderId::GLOW_DEPTH_COPY).initialize_shader(m_device->getLogicalDevice(), "glow_depth_copy");
  at(ShaderId::GLOW_PROBE_ON_GRID)
      .initialize_shader(m_device->getLogicalDevice(), "glow_probe_on_grid");

  for (auto& shader : m_shaders) {
    ASSERT_MSG(shader.okay(), "Shader compiled");
  }
}
