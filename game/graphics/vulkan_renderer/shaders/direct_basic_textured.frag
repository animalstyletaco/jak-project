#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fog;
layout (location = 3) in flat uvec4 tex_info;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  float alpha_reject;
  float color_mult;
  float alpha_mult;
  float alpha_sub;
  vec4 fog_color;
} ubo;

layout (set = 0, binding = 1) uniform sampler2D tex_T0;

void main() {
    vec4 T0 = texture(tex_T0, tex_coord.xy / tex_coord.z);
    // y is tcc
    // z is decal

    if (tex_info.y == 0) {
        if (tex_info.z == 0) {
            // modulate + no tcc
            color.xyz = fragment_color.xyz * T0.xyz;
            color.w = fragment_color.w;
        } else {
            // decal + no tcc
            color.xyz = T0.xyz * 0.5;
            color.w = fragment_color.w;
        }
    } else {
        if (tex_info.z == 0) {
            // modulate + tcc
            color = fragment_color * T0;
        } else {
            // decal + tcc
            color.xyz = T0.xyz * 0.5;
            color.w = T0.w;
        }
    }
    color *= 2;
    color.xyz *= ubo.color_mult;
    color.w *= ubo.alpha_mult;
    if (color.a < ubo.alpha_reject) {
        discard;
    }
    if (tex_info.w == 1) {
        color.xyz = mix(color.xyz, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
    }

}
