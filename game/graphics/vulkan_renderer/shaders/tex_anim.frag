#version 430 core

layout (location = 0) out vec4 color;

layout (push_constant) uniform PushConstant {
  layout (offset = 80)  vec4 rgba;
  layout (offset = 96)  ivec4 channel_scramble;
  layout (offset = 112) int enable_tex;
  layout (offset = 116) int tcc;
} pc;

layout (location = 0) in vec2 uv;

layout (set = 0, binding = 0) uniform sampler2D tex;

void main() {

  if (pc.enable_tex == 1) {
    vec4 tex_color = texture(tex, uv);
    vec4 unscambled_tex = vec4(tex_color[pc.channel_scramble[0]],
    tex_color[pc.channel_scramble[1]],
    tex_color[pc.channel_scramble[2]],
    tex_color[pc.channel_scramble[3]]);
    color = pc.rgba / 128.;
    if (pc.tcc == 1) {
      color *= unscambled_tex;
    } else {
      color.xyz *= unscambled_tex.xyz;
    }
  } else {
    color = (pc.rgba / 128.);
  }
}
