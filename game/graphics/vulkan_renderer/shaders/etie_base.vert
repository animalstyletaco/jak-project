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
} ubo;

// etie stuff
layout (set = 0, binding = 1) uniform EtieUniformBufferObject {
   vec4 persp0;
   vec4 persp1;
   mat4 cam_no_persp;
} etie_ubo;

layout (set = 0, binding = 2) uniform sampler1D tex_T1; // note, sampled in the vertex shader on purpose.

layout (push_constant) uniform PushConstant {
   float height_scale;
   float scissor_adjust;
   int decal;
} pc;

layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec3 tex_coord;
layout (location = 2) out float fogginess;


void main() {
    float fog1 = ubo.camera[3].w + ubo.camera[0].w * position_in.x + ubo.camera[1].w * position_in.y + ubo.camera[2].w * position_in.z;
    fogginess = 255 - clamp(fog1 + ubo.hvdf_offset.w, ubo.fog_min, ubo.fog_max);
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



    if (pc.decal == 1) {
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
}
