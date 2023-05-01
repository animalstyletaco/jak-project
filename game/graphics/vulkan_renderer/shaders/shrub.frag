#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fogginess;

layout(push_constant) uniform PER_OBJECT
{
	layout (offset = 12) int textureIndex;
}pc;

layout (set = 1, binding = 0) uniform UniformBufferObject {
  float alpha_min;
  float alpha_max;
  vec4 fog_color;
} ubo;

layout (set = 1, binding = 1) uniform sampler2D shrub_texture;

void main() {
    vec4 T0 = texture(shrub_texture, tex_coord.xy / 4096.f);
    color = fragment_color * T0;

    if (color.a < ubo.alpha_min || color.a > ubo.alpha_max) {
        discard;
    }

    color.xyz = mix(color.xyz, ubo.fog_color.rgb, clamp(fogginess * ubo.fog_color.a, 0, 1));
}
