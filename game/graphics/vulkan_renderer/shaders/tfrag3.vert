#version 430 core

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 tex_coord_in;
layout (location = 2) in int time_of_day_index;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  vec4 hvdf_offset;
  mat4 camera;
  float fog_constant;
  float fog_min;
  float fog_max;
  float height_scale;
} ubo;

layout (binding = 10) uniform sampler1D tex_T1; // note, sampled in the vertex shader on purpose.

layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec3 tex_coord;
layout (location = 2) out float fogginess;

const float SCISSOR_ADJUST = 512.0/448.0;

void main() {


    // old system:
    // - load vf12
    // - itof0 vf12
    // - multiply with camera matrix (add trans)
    // - let Q = fogx / vf12.w
    // - xyz *= Q
    // - xyzw += hvdf_offset
    // - clip w.
    // - ftoi4 vf12
    // use in gs.
    // gs is 12.4 fixed point, set up with 2048.0 as the center.

    // the itof0 is done in the preprocessing step.  now we have floats.

    // Step 3, the camera transform
    vec4 transformed = -ubo.camera[3];
    transformed -= ubo.camera[0] * position_in.x;
    transformed -= ubo.camera[1] * position_in.y;
    transformed -= ubo.camera[2] * position_in.z;

    // compute Q
    float Q = ubo.fog_constant / transformed.w;

    // do fog!
    fogginess = 255 - clamp(-transformed.w + ubo.hvdf_offset.w, ubo.fog_min, ubo.fog_max);

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
    transformed.y *= (SCISSOR_ADJUST * ubo.height_scale);
    gl_Position = transformed;

    // time of day lookup
    fragment_color = texelFetch(tex_T1, time_of_day_index, 0);

    // fog hack
    if (fragment_color.r < 0.0075 && fragment_color.g < 0.0075 && fragment_color.b < 0.0075) {
        fogginess = 0;
    }

    // color adjustment
    fragment_color *= 2;
    fragment_color.a *= 2;
    
    tex_coord = tex_coord_in;
}
