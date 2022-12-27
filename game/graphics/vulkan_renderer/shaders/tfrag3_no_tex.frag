#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (set = 1, binding = 0) uniform sampler2D tex_T0;

layout (set = 1, binding = 0) uniform UniformBufferObject {
   float alpha_min;
   float alpha_max;
} ubo;

void main() {
    color = fragment_color;
}
