#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in flat vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in flat uvec2 tex_info;

layout (set = 0, binding = 20) uniform sampler2D tex_T0;
layout (set = 0, binding = 21) uniform sampler2D tex_T1;
layout (set = 0, binding = 22) uniform sampler2D tex_T2;
layout (set = 0, binding = 23) uniform sampler2D tex_T3;
layout (set = 0, binding = 24) uniform sampler2D tex_T4;
layout (set = 0, binding = 25) uniform sampler2D tex_T5;
layout (set = 0, binding = 26) uniform sampler2D tex_T6;
layout (set = 0, binding = 27) uniform sampler2D tex_T7;
layout (set = 0, binding = 28) uniform sampler2D tex_T8;
layout (set = 0, binding = 29) uniform sampler2D tex_T9;

vec4 sample_tex(vec2 coord, uint unit) {
  switch (unit) {
    case 0: return texture(tex_T0, coord);
    case 1: return texture(tex_T1, coord);
    case 2: return texture(tex_T2, coord);
    case 3: return texture(tex_T3, coord);
    case 4: return texture(tex_T4, coord);
    case 5: return texture(tex_T5, coord);
    case 6: return texture(tex_T6, coord);
    case 7: return texture(tex_T7, coord);
    case 8: return texture(tex_T8, coord);
    case 9: return texture(tex_T9, coord);
    default: return vec4(1.0, 0, 1.0, 1.0);
  }
}

void main() {
  vec4 T0 = sample_tex(tex_coord.xy, tex_info.x);
  if (tex_info.y == 0) {
    T0.w = 1.0;
  }
  vec4 tex_color = fragment_color * T0 * 2.0;
  if (tex_color.a < 0.016) {
    discard;
  }
  color = tex_color;
}
