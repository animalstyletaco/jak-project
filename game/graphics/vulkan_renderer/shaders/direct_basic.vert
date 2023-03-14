// Shader for the DirectRenderer. Inputs are RGBA + position.

#version 430 core

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec4 rgba_in;

layout (location = 0) out vec4 fragment_color;

layout(push_constant) uniform PER_OBJECT
{
	layout(offset = 0) float height_scale;
  layout(offset = 4) float scissor_adjust;
}pc;

void main() {
  // Note: position.y is multiplied by 32 instead of 16 to undo the half-height for interlacing stuff.
  gl_Position = vec4((position_in.x - 0.5) * 16., -(position_in.y - 0.5) * 32 * pc.height_scale, position_in.z * 2 - 1., 1.0);
  // scissoring area adjust
  gl_Position.y *= pc.scissor_adjust;
  fragment_color = vec4(rgba_in.x, rgba_in.y, rgba_in.z, rgba_in.w * 2.);
}
