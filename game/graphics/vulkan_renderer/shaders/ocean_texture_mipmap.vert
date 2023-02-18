#version 430 core

layout (location = 0) in vec2 position_in;
layout (location = 1) in vec2 tex_coord_in;

layout (location = 0) out vec2 tex_coord;

layout(push_constant) uniform PushConstant
{
	layout(offset = 0) float scale;
}pc;


void main() {
    gl_Position = vec4(position_in.x * pc.scale - (1 - pc.scale), position_in.y * pc.scale - (1 - pc.scale), 0.5, 1.0);
    tex_coord = tex_coord_in;
}
