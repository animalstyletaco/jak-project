#version 430 core

layout (location = 0) out vec4 color;
layout (location = 0) in vec4 vtx_color;
layout (location = 1) in vec2 vtx_st;
layout (location = 2) in float fog;

layout(push_constant) uniform PushConstant {
  layout(offset = 104) int ignore_alpha;
  layout(offset = 108) int settings;
  layout(offset = 112) vec4 fog_color;
} pc;

layout (set = 1, binding = 0) uniform sampler2D tex_T0;

void main() {
    int decal_enable = pc.settings >> 16;
    int gfx_hack_no_tex = pc.settings & 0xff;

    if(gfx_hack_no_tex == 0){
       vec4 T0 = texture(tex_T0, vtx_st);
       
       if (decal_enable == 0) {
           color = vtx_color * T0 * 2;
       } else {
           color = T0;
       }
       color.a *= 2;
    } else {
      color.rgb = vtx_color.rgb;
      color.a = 1;
    }

    //if (pc.ignore_alpha == 0 && color.w < 0.128) {
    //    discard;
    //}

    color.xyz = mix(color.xyz, pc.fog_color.rgb, clamp(pc.fog_color.a * fog, 0, 1));
}
