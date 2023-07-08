#version 430 core
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec4 rgba_in;

layout (location = 0) out vec4 fragment_color;

#include "vertex_global_settings.glsl"

// this is just for debugging.
void main() {
    vec4 transformed = pc.camera[3];
    transformed += pc.camera[0] * position_in.x;
    transformed += pc.camera[1] * position_in.y;
    transformed += pc.camera[2] * position_in.z;

    // compute Q
    float Q = pc.fog_constant / transformed.w;

    // perspective divide!
    transformed.xyz *= Q;
    // offset
    transformed.xyz += pc.hvdf_offset.xyz;
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
