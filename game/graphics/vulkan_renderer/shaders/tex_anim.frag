#version 430 core

layout (location = 0) out vec4 color;

layout (set = 0, binding = 0) uniform UniformBufferObject {
   vec4 rgba;
   int enable_tex;
   int tcc;
   ivec4 channel_scramble;
   float alpha_multiply;
   float minimum;
   float maximum;
   float slime_scroll;
} ubo;

layout (location = 0) in vec2 uv;

layout (set = 0, binding = 0) uniform sampler2D tex;

void main() {

  if (ubo.enable_tex == 1) {
    vec4 tex_color = texture(tex, uv);
    vec4 unscambled_tex = vec4(tex_color[ubo.channel_scramble[0]],
    tex_color[ubo.channel_scramble[1]],
    tex_color[ubo.channel_scramble[2]],
    tex_color[ubo.channel_scramble[3]]);
    color = ubo.rgba / 128.;
    if (ubo.tcc == 1) {
      color *= unscambled_tex;
    } else {
      color.xyz *= unscambled_tex.xyz;
    }
  } else {
    color = (ubo.rgba / 128.);
  }

  color *= ubo.alpha_multiply;
}
