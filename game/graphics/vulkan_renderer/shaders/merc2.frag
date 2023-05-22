#version 430 core

layout (location = 0) out vec4 color;
layout (location = 0) in vec3 vtx_color;
layout (location = 1) in vec2 vtx_st;
layout (location = 2) in float fog;

layout(push_constant) uniform PushConstant {
  layout(offset = 16) vec4 fog_color;
  layout(offset = 32) int ignore_alpha;
  layout(offset = 36) int decal_enable;
  layout(offset = 40) int gfx_hack_no_tex;
} pc;

layout (set = 1, binding = 0) uniform sampler2D tex_T0;

void main() {
    if(pc.gfx_hack_no_tex == 0){
       vec4 T0 = texture(tex_T0, vtx_st);
       
       if (pc.decal_enable == 0) {
           color.xyz = vtx_color * T0.xyz;
       } else {
           color.xyz = T0.xyz * 0.5;
       }
       color.w = T0.w;
       color *= 2;
    } else {
      color.rgb = vtx_color;
      color.a = 1;
    }


    if (pc.ignore_alpha == 0 && color.w < 0.128) {
        discard;
    }

    color.xyz = mix(color.xyz, pc.fog_color.rgb, clamp(pc.fog_color.a * fog, 0, 1));
}
