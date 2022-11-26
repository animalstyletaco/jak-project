#version 430 core

layout (location = 0) in vec4 position_in;
layout (location = 1) in vec4 rgba_in;
layout (location = 2) in vec3 tex_coord_in;

layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec3 tex_coord;
layout (location = 2) out float fog;

// putting all texture info stuff here so it's easier to copy-paste
layout (location = 3) in uvec4 tex_info_in;
layout (location = 3) out flat uvec4 tex_info;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  float height_scale;
} ubo;

void main() {
  gl_Position = vec4((position_in.x - 0.5) * 16., -(position_in.y - 0.5) * 32 * ubo.height_scale, position_in.z * 2 - 1., 1.0);
  // scissoring area adjust
  gl_Position.y *= 512.0/448.0;
  fragment_color = vec4(rgba_in.x, rgba_in.y, rgba_in.z, rgba_in.w * 2.);
  tex_coord = tex_coord_in;
  tex_info = tex_info_in;
  fog = 255 - position_in.w;
}
