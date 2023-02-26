#version 430 core

layout (location = 0) in vec4 position_in;
layout (location = 1) in vec4 rgba_in;
layout (location = 2) in vec2 uv_texture_in;
layout (location = 3) in vec2 uv_probe_in;

layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec2 uv_texture;
layout (location = 2) out float discard_flag;

layout(push_constant) uniform PER_OBJECT
{
	layout(offset = 0) float height_scale;
}pc;

layout (set = 0, binding = 0) uniform sampler2D probe_tex;


void main() {
  vec4 transformed = position_in;
  transformed.xy -= (2048.);
  transformed.z /= (8388608);
  transformed.z -= 1;
  transformed.x /= (256);
  transformed.y /= -(128);
  transformed.xyz *= transformed.w;
  // scissoring area adjust
  transformed.y *= pc.height_scale;
  gl_Position = transformed;
  fragment_color = rgba_in;
  uv_texture = uv_texture_in;
  discard_flag = texture(probe_tex, uv_probe_in).a;
}
