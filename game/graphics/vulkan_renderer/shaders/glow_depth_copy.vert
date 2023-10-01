#version 430 core

layout (location = 0) in vec4 position_in;
layout (location = 1) in vec4 rgba_in;
layout (location = 2) in vec2 uv;

layout (location = 0) out vec2 tex_coord;

layout(push_constant) uniform PER_OBJECT
{
	layout(offset = 0) float height_scale;
  layout(offset = 4) float scissor_adjust;
}pc;

void main() {
  gl_Position = vec4((position_in.xy * 2) - 1.f, 0.f, 1.f);
  tex_coord.x = uv.x / 512;
  tex_coord.y = 1.f - (uv.y / pc.scissor_adjust);
}
