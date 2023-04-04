#version 430 core

layout (location = 0) out vec4 color;

layout (push_constant) uniform PushConstant {
   layout(offset = 16) vec4 color_uniform;
} pc;

void main() {
    color = pc.color_uniform;
}
