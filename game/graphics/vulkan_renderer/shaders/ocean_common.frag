#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fog;

layout(push_constant) uniform PER_OBJECT
{
	layout (offset = 12) int bucket;
}pc;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  float color_mult;
  float alpha_mult;
  vec4 fog_color;
} ubo;


layout (set = 0, binding = 1) uniform sampler2D tex_T0;

void main() {
    vec4 T0 = texture(tex_T0, tex_coord.xy / tex_coord.z);
    if (pc.bucket == 0) {
        color.rgb = fragment_color.rgb * T0.rgb;
        color.a = fragment_color.a;
        color.rgb = mix(color.rgb, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
    } else if (pc.bucket == 1 || pc.bucket == 2 || pc.bucket == 4) {
        color = fragment_color * T0;
    } else if (pc.bucket == 3) {
        color = fragment_color * T0;
        color.rgb = mix(color.rgb, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
    }

}