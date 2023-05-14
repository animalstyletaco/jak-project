#version 430 core


layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec4 fragment_color;
layout (location = 2) in float fog;
layout (location = 3) in flat uvec2 tex_info;

layout (set = 1, binding = 0) uniform sampler2D texture0;

layout(push_constant) uniform PushConstant {
  layout(offset = 16) vec4 fog_color;
  layout(offset = 32) float alpha_reject;
  layout(offset = 36) float color_mult;
	layout(offset = 40) int gfx_hack_no_tex;
}pc;

layout (location = 0) out vec4 color;

void main() {
    // 0x1 is tcc
    // 0x2 is decal
    // 0x4 is fog

    if (pc.gfx_hack_no_tex == 0) {
      vec4 T0 = texture(texture0, tex_coord.xy);
      if ((tex_info.y & 1u) == 0) {
          if ((tex_info.y & 2u) == 0) {
              // modulate + no tcc
              color.rgb = fragment_color.rgb * T0.rgb;
              color.a = fragment_color.a;
          } else {
              // decal + no tcc
              color.rgb = T0.rgb * 0.5;
              color.a = fragment_color.a;
          }
      } else {
          if ((tex_info.y & 2u) == 0) {
              // modulate + tcc
              color = fragment_color * T0;
          } else {
              // decal + tcc
              color.rgb = T0.rgb * 0.5;
              color.a = T0.a;
          }
      }
      color *= 2;
    } else {
      if ((tex_info.y & 1u) == 0) {
          if ((tex_info.y & 2u) == 0) {
              // modulate + no tcc
              color.rgb = fragment_color.rgb;
              color.a = fragment_color.a * 2;
          } else {
              // decal + no tcc
              color.rgb = vec3(1);
              color.a = fragment_color.a * 2;
          }
      } else {
          if ((tex_info.y & 2u) == 0) {
              // modulate + tcc
              color = fragment_color;
          } else {
              // decal + tcc
              color.rgb = vec3(0.5);
              color.a = 1.0;
          }
      }
    }
    color.rgb *= pc.color_mult;

    if (color.a < pc.alpha_reject) {
        discard;
    }
    if ((tex_info.y & 4u) != 0) {
        color.xyz = mix(color.xyz, pc.fog_color.rgb, clamp(pc.fog_color.a * fog, 0, 1));
    }
}
