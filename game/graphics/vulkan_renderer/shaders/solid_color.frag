#version 430 core

layout (location = 0) out vec4 color;

layout (set = 0, binding = 0) uniform UniformBufferObject { vec4 fragment_color; }ubo;

void main() {
  color = ubo.fragment_color;
}
