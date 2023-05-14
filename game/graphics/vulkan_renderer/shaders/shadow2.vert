#version 430 core

layout (location = 0) in vec3 position_in;

layout(push_constant) uniform PushConstant
{
  layout(offset = 0) vec4 perspective_x;
  layout(offset = 16) vec4 perspective_y;
  layout(offset = 32) vec4 perspective_z;
  layout(offset = 48) vec4 perspective_w;
  layout(offset = 64) vec4 hvdf_offset;
  layout(offset = 80) float scissor_adjust;
  layout(offset = 84) float height_scale;
  layout(offset = 88) int clear_mode;
  layout(offset = 92) float fog;
}pc;



void main() {
  if (pc.clear_mode == 1) {
    // this is just copied from shadow 1, needs revisiting.
    gl_Position = vec4((position_in.x - 0.5) * 16., -(position_in.y - 0.5) * 32, position_in.z * 2 - 1., 1.0);
    gl_Position.y *= pc.scissor_adjust;
  } else {
    vec4 transformed = -pc.perspective_w;
    transformed -= pc.perspective_x * position_in.x;
    transformed -= pc.perspective_y * position_in.y;
    transformed -= pc.perspective_z * position_in.z;
    transformed.xyz *= pc.fog / transformed.w;


    transformed.xyz += pc.hvdf_offset.xyz;
    transformed.xy -= (2048.);
    transformed.z /= (8388608);
    transformed.z -= 1;
    transformed.x /= (256);
    transformed.y /= -(128);
    transformed.xyz *= transformed.w;
    transformed.y *= pc.scissor_adjust * pc.height_scale;
    gl_Position = transformed;
  }
}
