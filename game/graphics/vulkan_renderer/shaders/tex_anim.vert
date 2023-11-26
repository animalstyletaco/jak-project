#version 430 core

layout (location = 0) in int vertex_index;

layout (set = 0, binding = 0) uniform UniformBufferObject {
   vec2 uvs[4];
   vec3 positions[4];
} ubo;

// TODO flags and stuff

layout (location = 0) out vec2 uv;

void main() {
  gl_Position = vec4(-1. + (ubo.positions[vertex_index] * 2), 1);
  uv = ubo.uvs[vertex_index];
}
