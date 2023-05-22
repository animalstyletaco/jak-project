#version 430 core

layout (location = 0) in vec3 vtx_color;
layout (location = 1) in vec2 vtx_st;
layout (location = 2) in float fog;

layout (location = 0) out vec4 color;

layout(push_constant) uniform PushConstant {
   layout(offset = 120) int gfx_hack_no_tex;
} pc;

layout (set = 1, binding = 0) uniform sampler2D tex_T0;

void main() {
    if (pc.gfx_hack_no_tex == 0) {
      vec4 T0 = texture(tex_T0, vtx_st);

      color.a = T0.a;
      color.rgb = T0.rgb * vtx_color;
      color *= 2;
    } else {
      color.rgb = vtx_color;
      color.a = 1;
    }
}
