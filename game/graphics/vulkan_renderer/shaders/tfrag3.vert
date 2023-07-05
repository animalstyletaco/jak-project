#version 430 core
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 tex_coord_in;
layout (location = 2) in ivec2 time_of_day_index;

#include "global_settings.glsl"

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
    transformed.y *= (pc.height_scale * pc.scissor_adjust);
    gl_Position = transformed;

    // time of day lookup
    int index = time_of_day_index.x; //Vulkan won't allow uint16_t as data type without extensions enabled.
    fragment_color = texelFetch(tex_T1, index, 0);
    // color adjustment
    fragment_color *= 2;
    fragment_color.a *= 2;

    int decal = (pc.settings & 1);
    if (decal == 1) {
        // tfrag/tie always use TCC=RGB, so even with decal, alpha comes from fragment.
        fragment_color.xyz = vec3(1.0, 1.0, 1.0);
    }

    // fog hack
    if (fragment_color.r < 0.0075 && fragment_color.g < 0.0075 && fragment_color.b < 0.0075) {
        fogginess = 0;
    }
    
    tex_coord = tex_coord_in;
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0; //Depth hack for OpenGL to Vulkan depth range conversion
}
