#version 430 core

layout (location = 0) in flat vec4 fragment_color;
layout (location = 1) in vec2 tex_coord;

layout (set = 1, binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 out_color;

void main() {
    // sample texture
    out_color = fragment_color * texture(tex, tex_coord);
}
