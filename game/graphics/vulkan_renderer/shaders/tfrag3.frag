#version 430 core
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fogginess;

layout (set = 1, binding = 0) uniform UniformBufferObject {
  float alpha_min;
  float alpha_max;
  vec4 fog_color;
} ubo;

layout (set = 1, binding = 1) uniform sampler2D texture0;

void main() {
    vec4 T0 = texture(texture0, tex_coord.xy);
    color = fragment_color * T0;

    if (color.a < ubo.alpha_min || color.a > ubo.alpha_max) {
        discard;
    }

    color.rgb = mix(color.rgb, ubo.fog_color.rgb, clamp(fogginess * ubo.fog_color.a, 0, 1));
}
