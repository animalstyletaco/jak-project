#version 430 core
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 tex_coord_in;
layout (location = 2) in int time_of_day_index;

#include "vertex_global_settings.glsl"

layout (set = 0, binding = 0) uniform sampler1D tex_T1; // note, sampled in the vertex shader on purpose.

// etie stuff
layout (set = 0, binding = 1) uniform EtieUniformBufferObject {
   mat4 cam_no_persp;
   vec4 persp0;
   vec4 persp1;
} etie_ubo;


layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec3 tex_coord;
layout (location = 2) out float fogginess;
layout (location = 3) out int gfx_hack_no_tex;


void main() {
    float fog1 = pc.camera[3].w + pc.camera[0].w * position_in.x + pc.camera[1].w * position_in.y + pc.camera[2].w * position_in.z;
    fogginess = 255 - clamp(fog1 + pc.hvdf_offset.w, pc.fog_min, pc.fog_max);
    vec4 vf17 = etie_ubo.cam_no_persp[3];
    vf17 += etie_ubo.cam_no_persp[0] * position_in.x;
    vf17 += etie_ubo.cam_no_persp[1] * position_in.y;
    vf17 += etie_ubo.cam_no_persp[2] * position_in.z;
    vec4 p_proj = vec4(etie_ubo.persp1.x * vf17.x, etie_ubo.persp1.y * vf17.y, etie_ubo.persp1.z, etie_ubo.persp1.w);
    p_proj += etie_ubo.persp0 * vf17.z;

    float pQ = 1.f / p_proj.w;
    vec4 transformed = p_proj * pQ;
    transformed.w = p_proj.w;

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


    int decal = (pc.settings & 1);
    if (decal == 1) {
        fragment_color = vec4(1.0, 1.0, 1.0, 1.0);
    } else {
        // time of day lookup
        fragment_color = texelFetch(tex_T1, time_of_day_index, 0);
        // color adjustment
        fragment_color *= 2;
        fragment_color.a *= 2;
    }

    // fog hack
    if (fragment_color.r < 0.005 && fragment_color.g < 0.005 && fragment_color.b < 0.005) {
        fogginess = 0;
    }

    tex_coord = tex_coord_in;
    gfx_hack_no_tex = (pc.settings & 0x10);
}
