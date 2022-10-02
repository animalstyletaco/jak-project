// Debug shader for drawing things in red.  Uses the same conventions as direct_basic, see there for more details

#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;

void main() {
  color = fragment_color;
}
