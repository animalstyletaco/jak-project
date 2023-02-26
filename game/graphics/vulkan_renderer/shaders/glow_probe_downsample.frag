#version 430 core

layout (location = 0) in vec2 tex_coord;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 0) uniform sampler2D tex;

void main() {
  vec2 texture_coords = vec2(tex_coord.x, tex_coord.y);

  // sample framebuffer texture
  out_color = texture(tex, texture_coords);
}
