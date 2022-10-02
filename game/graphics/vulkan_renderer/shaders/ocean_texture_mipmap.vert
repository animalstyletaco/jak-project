#version 430 core

layout (location = 0) in vec2 position_in;
layout (location = 1) in vec2 tex_coord_in;

layout (location = 0) out vec2 tex_coord;
layout (set = 0, binding = 0) uniform UniformBufferObject {
   float scale;
} ubo;

void main() {
    gl_Position = vec4(position_in.x * ubo.scale - (1 - ubo.scale), position_in.y * ubo.scale - (1 - ubo.scale), 0.5, 1.0);
    tex_coord = tex_coord_in;
}
