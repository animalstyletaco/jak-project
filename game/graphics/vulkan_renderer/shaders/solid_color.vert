#version 430 core

layout (location = 0) in vec2 position_in;

void main() {
  gl_Position = vec4(position_in, 0, 1.0);
  gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0; //Depth hack for OpenGL to Vulkan depth range conversion
}
