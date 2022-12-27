#include "Buffer.h"
#include <cassert>

template <class T>
void UniformBuffer::SetMatrixDataInVkDeviceMemory(uint32_t memory_offset,
                                           uint32_t row_count,
                                           uint32_t col_count,
                                           uint32_t matrix_count,
                                           bool want_transpose_matrix,
                                           bool is_row_col_order,
                                           uint32_t flags,
                                           T* matrix_data) {
  uint32_t element_count = row_count * col_count;
  uint32_t memory_size = row_count * col_count * sizeof(*matrix_data) * matrix_count;
  for (uint32_t matrix_id = 0; matrix_id < matrix_count; matrix_id++)
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
      SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&transpose_matrix, memory_size, flags);
    } else {
      SetDataInVkDeviceMemory(memory_offset, (uint8_t*)matrix_data, memory_size, flags);
    }
}

void UniformBuffer::Set4x4MatrixDataInVkDeviceMemory(const char* section_name,
                                                     uint32_t matrix_count,
                                                     bool want_transpose_matrix,
                                                     float* matrix_data,
                                                     bool is_row_col_order) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetMatrixDataInVkDeviceMemory(memory_offset, 4, 4, matrix_count, want_transpose_matrix,
                                is_row_col_order, 0, matrix_data);
}

void UniformBuffer::SetUniformVectorUnsigned(const char* section_name,
                                             uint32_t size,
                                             uint32_t* value,
                                             uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)value, sizeof(unsigned) * size, flags);
}

void UniformBuffer::SetUniformVectorFourFloat(const char* section_name,
                                              uint32_t vector_count,
                                              float* value,
                                              uint32_t flags) {
  uint32_t size = vector_count * 4 * sizeof(float);

  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)value, size, flags);
}

void UniformBuffer::SetUniform1ui(const char* section_name, uint32_t value, uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&value, sizeof(value), flags);
}

void UniformBuffer::SetUniform1i(const char* section_name, int32_t value, uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&value, sizeof(value), flags);
}

void UniformBuffer::SetUniform1f(const char* section_name, float value, uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&value, sizeof(value), flags);
}

void UniformBuffer::SetUniformMathVector3f(const char* section_name,
                                           math::Vector3f& value,
                                           uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&value, sizeof(value), flags);
}

void UniformBuffer::SetUniformMathVector4f(const char* section_name,
                                           math::Vector4f& value,
                                           uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&value, sizeof(value), flags);
}

void UniformBuffer::SetUniform3f(const char* section_name,
                                 float value1,
                                 float value2,
                                 float value3,
                                 uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  float values[] = {value1, value2, value3};
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&values, sizeof(values), flags);
}

void UniformBuffer::SetUniform4f(const char* section_name,
                                 float value1,
                                 float value2,
                                 float value3,
                                 float value4,
                                 uint32_t flags) {
  uint32_t memory_offset = GetDeviceMemoryOffset(section_name);

  float values[] = {value1, value2, value3, value4};
  SetDataInVkDeviceMemory(memory_offset, (uint8_t*)&values, sizeof(values), flags);
}
