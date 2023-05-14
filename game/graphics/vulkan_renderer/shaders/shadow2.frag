#version 430 core

layout (location = 0) out vec4 color;

layout(push_constant) uniform PushConstant {
   layout(offset = 96) vec4 color_uniform;
}pc;

void main() {
  color = 0.5 * pc.color_uniform;
}
