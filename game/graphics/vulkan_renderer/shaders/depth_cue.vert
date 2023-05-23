#version 430 core

layout (location = 0) in vec2 xy;
layout (location = 1) in vec2 st;

layout(push_constant) uniform PushConstant
{
  layout(offset = 0) uvec4 u_color;
  layout(offset = 16) float u_depth;
}pc;

layout (location = 0) out flat vec4 fragment_color;
layout (location = 1) out vec2 tex_coord;

void main() {
    // Calculate color
    vec4 color = pc.u_color;
    color *= 2; // correct
    
    fragment_color = color;

    // Pass on texture coord
    tex_coord = st;

    // Calculate vertex position
    vec4 position = vec4(xy.x, xy.y, pc.u_depth, 1.0);
    position.xyz = (position.xyz * 2) - 1.0; // convert from [0,1] to clip-space

    gl_Position = position;
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0; //Depth hack for OpenGL to Vulkan depth range conversion
}
