#version 430 core

layout (location = 0) out vec4 color;
layout (location = 0) in vec2 st;
layout (binding = 0) uniform sampler2D tex_T0;

void main() {
    color = texture(tex_T0, st);
    color.w *= 2;
}
