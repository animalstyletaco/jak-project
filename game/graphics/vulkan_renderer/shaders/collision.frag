#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;

void main() {
    if (fragment_color.a <= 0) discard;
    color = fragment_color;
}
