#version 430 core

layout (location = 0) in int vertex_index;

layout (push_constant) uniform PushConstant {
 layout(offset = 0) vec4 rgba;
 layout(offset = 16) vec2 uvs[4];
 layout(offset = 48) vec3 positions[4];
} pc;
// TODO flags and stuff

layout (location = 0) out vec2 uv;

void main() {
  gl_Position = vec4(-1. + (pc.positions[vertex_index] * 2), 1);
  uv = pc.uvs[vertex_index];
}
