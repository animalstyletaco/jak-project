#version 430 core

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec2 tex_coord;

layout (location = 0) out vec4 color;

layout (set = 0, binding = 0) uniform sampler2D tex_T0;

void main() {
    vec4 tex = texture(tex_T0, tex_coord);
    color = fragment_color * tex;
}
