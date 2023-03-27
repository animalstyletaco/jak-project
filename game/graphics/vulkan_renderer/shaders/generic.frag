#version 430 core


layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec4 fragment_color;
layout (location = 2) in float fog;
layout (location = 3) in flat uvec2 tex_info;

layout (set = 1, binding = 0) uniform UniformBufferObject {
  float alpha_reject;
  float color_mult;
  vec4 fog_color;
} ubo;

layout (set = 1, binding = 1) uniform sampler2D texture0;

layout (location = 0) out vec4 color;

void main() {
    vec4 T0 = texture(texture0, tex_coord.xy);
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
    color.xyz *= ubo.color_mult;

    if (color.a < ubo.alpha_reject) {
        discard;
    }
    if ((tex_info.y & 4u) != 0) {
        color.xyz = mix(color.xyz, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
    }
}
