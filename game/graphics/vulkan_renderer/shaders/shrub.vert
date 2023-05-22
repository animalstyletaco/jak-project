#version 430 core
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 tex_coord_in;
layout (location = 2) in int time_of_day_index;
layout (location = 3) in vec3 rgba_base;

layout(push_constant) uniform PER_OBJECT
{
  layout(offset = 0) mat4 camera;
  layout(offset = 64) vec4 hvdf_offset;
  layout(offset = 80) float fog_constant;
  layout(offset = 84) float fog_min;
  layout(offset = 88) float fog_max;
  layout(offset = 92) float height_scale;
  layout(offset = 96) float scissor_adjust;
  layout(offset = 100) int decal;
}pc;

layout (set = 0, binding = 0) uniform sampler1D tex_T1; // note, sampled in the vertex shader on purpose.

layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec3 tex_coord;
layout (location = 2) out float fogginess;

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
    vec4 transformed = -pc.camera[3];
    transformed -= pc.camera[0] * position_in.x;
    transformed -= pc.camera[1] * position_in.y;
    transformed -= pc.camera[2] * position_in.z;

    // compute Q
    float Q = pc.fog_constant / transformed.w;

    // do fog!
    fogginess = 255 - clamp(-transformed.w + pc.hvdf_offset.w, pc.fog_min, pc.fog_max);

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
    transformed.y *= pc.scissor_adjust * pc.height_scale;
    gl_Position = transformed;

    // time of day lookup
    // start with the vertex color (only rgb, VIF filled in the 255.)
    fragment_color =  vec4(rgba_base, 1);
    // get the time of day multiplier
    vec4 tod_color = texelFetch(tex_T1, time_of_day_index, 0);
    // combine
    fragment_color *= tod_color * 4;
    fragment_color.a *= 2;

    if (pc.decal == 1) {
        fragment_color.xyz = vec3(1.0, 1.0, 1.0);
    }

    tex_coord = tex_coord_in;
    tex_coord.xy /= 4096;
}
