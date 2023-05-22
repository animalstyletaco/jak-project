#version 430 core
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fogginess;

layout (push_constant) uniform PushConstant {
  layout(offset = 104) float alpha_min;
  layout(offset = 108) float alpha_max;
  layout(offset = 112) vec4 fog_color;
} pc;

layout (set = 1, binding = 0) uniform sampler2D texture0;

void main() {
    vec4 T0 = texture(texture0, tex_coord.xy);
    color = fragment_color * T0;

    if (color.a < pc.alpha_min || color.a > pc.alpha_max) {
        discard;
    }

    color.rgb = mix(color.rgb, pc.fog_color.rgb, clamp(fogginess * pc.fog_color.a, 0, 1));
}
