#version 430 core

layout (location = 0) out vec4 color;
layout (location = 0) in vec3 vtx_color;
layout (location = 1) in vec2 vtx_st;
layout (location = 2) in float fog;

layout (set = 0, binding = 0) uniform sampler2D tex_T0;

layout (set = 0, binding = 1) uniform UniformBufferObject {
  vec4 fog_color;
  int ignore_alpha;
  int decal_enable;
} ubo;

void main() {
    vec4 T0 = texture(tex_T0, vtx_st);

    if (ubo.decal_enable == 0) {
        color.xyz = vtx_color * T0.xyz;
    } else {
        color.xyz = T0.xyz * 0.5;
    }
    color.w = T0.w;
    color *= 2;


    if (ubo.ignore_alpha == 0 && color.w < 0.128) {
        discard;
    }

    color.xyz = mix(color.xyz, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
}
