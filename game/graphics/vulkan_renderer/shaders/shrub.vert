#version 430 core

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 tex_coord_in;
layout (location = 2) in int time_of_day_index;
layout (location = 3) in vec3 rgba_base;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  vec4 hvdf_offset;
  mat4 camera;
  float fog_constant;
  float fog_min;
  float fog_max;
  float height_scale;
} ubo;

layout(push_constant) uniform PER_OBJECT
{
	layout(offset = 0) int textureIndex;
}pc;

const int TIME_OF_DAY_COLORS = 8192;
layout (set = 0, binding = 1) uniform sampler1D tex_T1[TIME_OF_DAY_COLORS]; // note, sampled in the vertex shader on purpose.

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
    // start with the vertex color (only rgb, VIF filled in the 255.)
    fragment_color =  vec4(rgba_base, 1);
    // get the time of day multiplier
    vec4 tod_color = texelFetch(tex_T1[pc.textureIndex], time_of_day_index, 0);
    // combine
    fragment_color *= tod_color * 4;
    fragment_color.a *= 2;

    tex_coord = tex_coord_in;
}
