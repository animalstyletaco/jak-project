#version 430 core

layout (location = 0) out vec4 color;
layout (location = 0) in vec2 tex_coord;

layout(push_constant) uniform PushConstant
{
	layout(offset = 4) float alpha_intensity;
}pc;

layout (set = 0, binding = 0) uniform sampler2D tex_T0;

void main() {
    vec4 tex = texture(tex_T0, tex_coord);
    tex.w *= pc.alpha_intensity;
    color = tex;
}
