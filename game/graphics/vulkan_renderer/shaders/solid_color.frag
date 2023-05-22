#version 430 core

layout (location = 0) out vec4 color;

layout(push_constant) uniform PER_OBJECT{
   layout(offset = 0) vec4 fragment_color;
}pc;

void main() {
  color = pc.fragment_color;
}
