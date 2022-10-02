#version 430 core

layout (location = 0) out vec4 color;
layout (set = 0, binding = 0) uniform UniformBufferObject { vec4 color_uniform; } ubo;

void main() {
    color = ubo.color_uniform;
}
