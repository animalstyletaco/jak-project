#pragma once

#include <string>

#include "common/common_types.h"
#include "common/versions/versions.h"
#include "game/graphics/general_renderer/ShaderCommon.h"
#include "game/graphics/vulkan_renderer/vulkan_utils/GraphicsDeviceVulkan.h"

class VulkanShader {
 public:
  static constexpr char shader_folder[] = "game/graphics/vulkan_renderer/shaders/";

  VulkanShader() = default;
  VulkanShader(VkDevice device, const std::string& shader_name);
  VulkanShader(const VulkanShader& shader);
  void initialize_shader(VkDevice device,
                         const std::string& shader_name);
  ~VulkanShader();

  VkShaderModule GetVertexShader() { return m_vert_shader; };
  VkShaderModule GetFragmentShader() { return m_frag_shader; };

  bool okay() const { return m_is_okay; }
  u64 id() const { return m_program; }

 private:
  VkShaderModule PopulateShader(const std::vector<u8>& code);

  VkShaderModule m_frag_shader = VK_NULL_HANDLE;
  VkShaderModule m_vert_shader = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;

  u64 m_program = 0;
  bool m_is_okay = false;

  std::string shader_name;
  GameVersion m_version;
};

class VulkanShaderLibrary {
 public:
  VulkanShaderLibrary(std::unique_ptr<GraphicsDeviceVulkan>& device);
  VulkanShader& operator[](ShaderId id) { return m_shaders[(int)id]; }
  VulkanShader& at(ShaderId id) { return m_shaders[(int)id]; }

 private:
  VulkanShader m_shaders[(int)ShaderId::MAX_SHADERS];
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
};
