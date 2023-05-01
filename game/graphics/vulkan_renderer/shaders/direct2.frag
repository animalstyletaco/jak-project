#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fog;
layout (location = 3) in flat uvec2 tex_info;

layout (push_constant) uniform PushConstant {
  layout(offset = 32) vec4 fog_color;
  layout(offset = 64) float alpha_reject;
  layout(offset = 68) float color_mult;
} pc;

layout (set = 0, binding = 0) uniform sampler2D tex_T0;

vec4 sample_tex(vec2 coord, uint unit) {
    return texture(tex_T0, coord);
}

void main() {
    vec4 T0 = sample_tex(tex_coord.xy / tex_coord.z, tex_info.x);
    // y is tcc
    // z is decal

    if ((tex_info.y & 1u) == 0) {
        if ((tex_info.y & 2u) == 0) {
            // modulate + no tcc
            color.xyz = fragment_color.xyz * T0.xyz;
            color.w = fragment_color.w;
        } else {
            // decal + no tcc
            color.xyz = T0.xyz * 0.5;
            color.w = fragment_color.w;
        }
    } else {
        if ((tex_info.y & 2u) == 0) {
            // modulate + tcc
            color = fragment_color * T0;
        } else {
            // decal + tcc
            color.xyz = T0.xyz * 0.5;
            color.w = T0.w;
        }
    }
    color *= 2;
    color.xyz *= pc.color_mult;
    if (color.a < pc.alpha_reject) {
        discard;
    }
    if ((tex_info.y & 4u) != 0) {
        color.xyz = mix(color.xyz, pc.fog_color.rgb, clamp(pc.fog_color.a * fog, 0, 1));
    }
}
