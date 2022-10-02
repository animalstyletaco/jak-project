#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) noperspective in vec3 tex_coord;
layout (set = 0, binding = 0) uniform sampler2D tex_T0;

void main() {
    vec4 T0 = texture(tex_T0, tex_coord.xy / tex_coord.z);
    T0.w = 1.0;
    color = fragment_color * T0 * 1.0;
}
