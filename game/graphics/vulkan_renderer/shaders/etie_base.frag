#version 430 core

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fogginess;

layout (set = 1, binding = 0) uniform UniformBufferObject {
   float alpha_min;
   float alpha_max;
   vec4 fog_color;
} ubo;

layout (set = 1, binding = 1) uniform sampler2D tex_T0;

layout(push_constant) uniform PushConstant {
   int gfx_hack_no_tex;
} pc;

layout (location = 0) out vec4 color;

void main() {
    if (pc.gfx_hack_no_tex == 0) {
      //vec4 T0 = texture(tex_T0, tex_coord);
      vec4 T0 = texture(tex_T0, tex_coord.xy);
      color = fragment_color * T0;
    } else {
      color = fragment_color/2;
    }

    if (color.a < ubo.alpha_min || color.a > ubo.alpha_max) {
        discard;
    }

    color.rgb = mix(color.rgb, ubo.fog_color.rgb, clamp(fogginess * ubo.fog_color.a, 0, 1));

}
