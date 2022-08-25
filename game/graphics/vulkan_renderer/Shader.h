#pragma once

#include <string>

#include "common/common_types.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

class Shader {
 public:
  static constexpr char shader_folder[] = "game/graphics/vulkan_renderer/shaders/";
  Shader(VkDevice device, const std::string& shader_name);
  Shader() = default;
  ~Shader();

  bool okay() const { return m_is_okay; }
  u64 id() const { return m_program; }
  uint32_t GetDeviceMemoryOffset(const char* name);

  VkShaderModule GetFragmentShader() const { return m_frag_shader; };
  VkShaderModule GetVertexShader() const { return m_vert_shader; };

  template <class T>
  void SetDataInVkDeviceMemory(uint32_t memory_offset,
                               T& value,
                               uint32_t value_size,
                               uint32_t flags);

  void SetUniform1i(const char* section_name, int32_t value, uint32_t flags = 0);
  void SetUniform1f(const char* section_name, float value, uint32_t flags = 0);
  void SetUniform3f(const char* section_name,
                    float value1,
                    float value2,
                    float value3,
                    uint32_t flags);
  void SetUniform4f(const char* section_name,
                    float value1,
                    float value2,
                    float value3,
                    float value4,
                    uint32_t flags = 0);

  template <class T>
  void SetMatrixDataInVkDeviceMemory(uint32_t memory_offset,
                                     uint32_t row_count,
                                     uint32_t col_count,
                                     uint32_t matrix_count,
                                     bool want_transpose_matrix,
                                     bool is_row_col_order,
                                     uint32_t flags,
                                     T* matrix_data);

  void Set4x4MatrixDataInVkDeviceMemory(const char* section_name,
                                        uint32_t matrix_count,
                                        bool want_transpose_matrix,
                                        float* matrix_data,
                                        bool is_row_col_order = true);

 private:
  VkShaderModule PopulateShader(const std::string& code);

  VkShaderModule m_frag_shader = VK_NULL_HANDLE;
  VkShaderModule m_vert_shader = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  u64 m_program = 0;
  bool m_is_okay = false;

  std::string shader_name;
};

// note: update the constructor in Shader.cpp
enum class ShaderId {
  SOLID_COLOR = 0,
  DIRECT_BASIC = 1,
  DIRECT_BASIC_TEXTURED = 2,
  DEBUG_RED = 3,
  SKY = 4,
  SKY_BLEND = 5,
  TFRAG3 = 6,
  TFRAG3_NO_TEX = 7,
  SPRITE = 8,
  SPRITE3 = 9,
  DIRECT2 = 10,
  EYE = 11,
  GENERIC = 12,
  OCEAN_TEXTURE = 13,
  OCEAN_TEXTURE_MIPMAP = 14,
  OCEAN_COMMON = 15,
  SHADOW = 16,
  SHRUB = 17,
  COLLISION = 18,
  MERC2 = 19,
  SPRITE_DISTORT = 20,
  SPRITE_DISTORT_INSTANCED = 21,
  POST_PROCESSING = 22,
  DEPTH_CUE = 23,
  MAX_SHADERS
};

class ShaderLibrary {
 public:
  ShaderLibrary();
  ShaderLibrary(VkDevice device);
  Shader& operator[](ShaderId id) { return m_shaders[(int)id]; }
  Shader& at(ShaderId id) { return m_shaders[(int)id]; }

 private:
  Shader m_shaders[(int)ShaderId::MAX_SHADERS];
  VkDevice m_device = VK_NULL_HANDLE;
  VkDeviceMemory m_device_memory;
  VkDeviceSize m_device_size;
};
