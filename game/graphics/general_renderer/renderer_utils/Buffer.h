#pragma once

#include "common/math/geometry.h"

class UniformBuffer {
 public:
  UniformBuffer() = default;
  UniformBuffer(uint32_t instance_size) { m_instance_size = instance_size; };
  virtual ~UniformBuffer() = default;

  virtual void SetDataInVkDeviceMemory(uint32_t memory_offset,
                                       uint8_t* value,
                                       uint32_t value_size,
                                       uint32_t flags){};

  virtual uint32_t GetDeviceMemoryOffset(const char* section_name) { return 0; };
  virtual void SetUniform1ui(const char* section_name,
                             uint32_t value,
                             uint32_t instanceIndex = 0,
                             uint32_t flags = 0);
  virtual void SetUniform1i(const char* section_name,
                            int32_t value,
                            uint32_t instanceIndex = 0,
                            uint32_t flags = 0);
  virtual void SetUniform1f(const char* section_name,
                            float value,
                            uint32_t instanceIndex = 0,
                            uint32_t flags = 0);
  virtual void SetUniform3f(const char* section_name,
                            float value1,
                            float value2,
                            float value3,
                            uint32_t instanceIndex = 0,
                            uint32_t flags = 0);
  virtual void SetUniform4f(const char* section_name,
                            float value1,
                            float value2,
                            float value3,
                            float value4,
                            uint32_t instanceIndex = 0,
                            uint32_t flags = 0);

  virtual void SetUniformVectorUnsigned(const char* section_name,
                                        uint32_t size,
                                        uint32_t* value,
                                        uint32_t instanceIndex = 0,
                                        uint32_t flags = 0);

  virtual void SetUniformVectorFourFloat(const char* section_name,
                                         uint32_t vector_count,
                                         float* value,
                                         uint32_t instanceIndex = 0,
                                         uint32_t flags = 0);

  virtual void SetUniformMathVector3f(const char* section_name,
                                      math::Vector3f& value,
                                      uint32_t instanceIndex = 0,
                                      uint32_t flags = 0);

  virtual void SetUniformMathVector4f(const char* section_name,
                                      math::Vector4f& value,
                                      uint32_t instanceIndex = 0,
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

  virtual void Set4x4MatrixDataInVkDeviceMemory(const char* section_name,
                                                uint32_t matrix_count,
                                                bool want_transpose_matrix,
                                                float* matrix_data,
                                                bool is_row_col_order = true);

 protected:
  uint32_t m_instance_size = 0;
};
