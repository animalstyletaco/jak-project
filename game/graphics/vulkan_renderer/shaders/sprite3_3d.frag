#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in flat vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in flat uvec2 tex_info;

layout (set = 1, binding = 0) uniform sampler2D tex_T0;

layout(push_constant) uniform PER_OBJECT
{
  layout(offset = 8) float alpha_min;
  layout(offset = 12) float alpha_max;
}pc;

void main() {
    vec4 T0 = texture(tex_T0, tex_coord.xy);
    if (tex_info.y == 0) {
        T0.w = 1.0;
    }
    color = fragment_color * T0;

    if (color.a < pc.alpha_min || color.a > pc.alpha_max) {
        discard;
    }
}
