#version 430 core

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec4 rgba_in;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  vec4 hvdf_offset;
  mat4 camera;
  float fog_constant;
} ubo;

layout (location = 0) out vec4 fragment_color;

layout(push_constant) uniform PushConstant
{
  layout(offset = 0)float height_scale;
  layout(offset = 4)float scissor_adjust;
}pc;

// this is just for debugging.
void main() {
    vec4 transformed = ubo.camera[3];
    transformed += ubo.camera[0] * position_in.x;
    transformed += ubo.camera[1] * position_in.y;
    transformed += ubo.camera[2] * position_in.z;

    // compute Q
    float Q = ubo.fog_constant / transformed.w;

    // perspective divide!
    transformed.xyz *= Q;
    // offset
    transformed.xyz += ubo.hvdf_offset.xyz;
    // correct xy offset
    transformed.xy -= (2048.);
    // correct z scale
    transformed.z /= (8388608);
    transformed.z -= 1;
    // correct xy scale
    transformed.x /= (256);
    transformed.y /= -(128);
    // hack
    transformed.xyz *= transformed.w;
    // scissoring area adjust
    transformed.y *= (pc.height_scale * pc.scissor_adjust);
    gl_Position = transformed;

    // time of day lookup
    fragment_color = rgba_in;
}
