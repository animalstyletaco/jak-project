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
      file_util::read_text_file(file_util::get_file_path({shader_folder, shader_name + ".vert"}));
  auto frag_src =
      file_util::read_text_file(file_util::get_file_path({shader_folder, shader_name + ".frag"}));

  m_vert_shader = PopulateShader(vert_src);
  m_frag_shader = PopulateShader(frag_src);

  m_is_okay = true;
}

template <class T>
void Shader::SetDataInVkDeviceMemory(uint32_t memory_offset,
                                     T& value,
                                     uint32_t value_size,
                                     uint32_t flags = 0) {
  void* data = NULL;
  vkMapMemory(device, device_memory, memory_offset, value_size, flags, &data);
  ::memcpy(data, &value, value_size);
  vkUnmapMemory(device, device_memory);
}

template <class T>
void Shader::SetMatrixDataInVkDeviceMemory(uint32_t memory_offset,
                                   uint32_t row_count,
                                   uint32_t col_count,
                                   uint32_t matrix_count,
                                   bool want_transpose_matrix,
                                   bool is_row_col_order = true,
                                   uint32_t flags = 0,
                                   T* matrix_data) {
  void* data = NULL;

  uint32_t element_count = row_count * col_count;
  uint32_t memory_size = row_count * col_count * sizeof(*matrix_data) * matrix_count;
  vkMapMemory(m_device, device_memory, memory_offset, memory_size, flags, &data);
  for (uint32_t matrix_id = 0; matrix_id < numberof_)
    if (want_transpose_matrix) {
      // TODO: Verify logic
      T transpose_matrix[row_count * col_count * matrix_count];
      if (is_row_col_order) {
        for (uint32_t i = 0; i < matrix_count; i++) {
          for (uint32_t j = 0; j < row_count; j++) {
            for (uint32_t k = 0; k < col_count; k++) {
              transpose_matrix[i * element_count + (k * col_count + j)] =
                  matrix_data[i * element_count + (j * row_count + k)];
            }
          }
        }
      } else {
        for (uint32_t i = 0; i < matrix_count; i++) {
          for (uint32_t j = 0; j < col_count; j++) {
            for (uint32_t k = 0; k < row_count; k++) {
              transpose_matrix[i * element_count + (k * col_count + j)] =
                  matrix_data[i * element_count + (j * row_count + k)];
            }
          }
        }
      }

      ::memcpy(data, &transpose_matrix, memory_size);
    } else {
      ::memcpy(data, matrix_data, memory_size);
    }
  vkUnmapMemory(m_device, m_device_memory);
}

void Shader::Set4x4MatrixDataInVkDeviceMemory(const char* section_name,
                                      uint32_t matrix_count,
                                      bool want_transpose_matrix,
                                      float* matrix_data,
                                      bool is_row_col_order = true) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetMatrixDataInVkDeviceMemory(memory_offset, 4, 4, matrix_count, want_transpose_matrix, is_row_col_order, 0,
                                matrix_data);
}

void Shader::SetUniform1i(const char* section_name, int32_t value, uint32_t flags = 0) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void Shader::SetUniform1f(const char* section_name, float value, uint32_t flags = 0) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, value, sizeof(value), flags);
}

void Shader::SetUniform3f(const char* section_name,
                          float value1,
                          float value2,
                          float value3,
                          uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  void* data = NULL;
  vkMapMemory(m_device, m_device_memory, memory_offset, sizeof(float) * 4, flags, &data);
  float* memory_mapped_data = (float*)data;
  memory_mapped_data[0] = value1;
  memory_mapped_data[1] = value2;
  memory_mapped_data[2] = value3;
  vkUnmapMemory(m_device, m_device_memory);
}

void Shader::SetUniform4f(const char* section_name,
                  float value1,
                  float value2,
                  float value3,
                  float value4,
                  uint32_t flags = 0) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  void* data = NULL;
  vkMapMemory(m_device, m_device_memory, memory_offset, sizeof(float) * 4, flags, &data);
  float* memory_mapped_data = (float*)data;
  memory_mapped_data[0] = value1;
  memory_mapped_data[1] = value2;
  memory_mapped_data[2] = value3;
  memory_mapped_data[3] = value4;
  vkUnmapMemory(m_device, m_device_memory);
}

Shader::~Shader() {
  if (m_device != VK_NULL_HANDLE && m_vert_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, &m_vert_shader, nullptr);
  }
  if (m_device != VK_NULL_HANDLE && m_frag_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, &m_frag_shader, nullptr);
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
