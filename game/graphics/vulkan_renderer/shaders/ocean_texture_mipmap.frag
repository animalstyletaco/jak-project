#version 430 core

layout (location = 0) out vec4 color;
layout (location = 0) in vec2 tex_coord;

layout (set = 1, binding = 0) uniform UniformBufferObject { float alpha_intensity; } ubo;
layout (set = 1, binding = 1) uniform sampler2D tex_T0;

void main() {
    vec4 tex = texture(tex_T0, tex_coord);
    tex.w *= ubo.alpha_intensity;
    color = tex;
}
