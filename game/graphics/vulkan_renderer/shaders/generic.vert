#version 430 core

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec4 rgba_in;
layout (location = 2) in vec2 tex_coord_in;
layout (location = 3) in uvec4 byte_info;

layout (set = 0, binding = 0) uniform UniformBufferObject {
   float mat_32;
   vec3 fog_constants;
   vec4 scale;
   float mat_23;
   float mat_33;
   vec4 hvdf_offset;
} ubo;

layout (location = 0) out vec2 tex_coord;

layout (location = 1) out vec4 fragment_color;
layout (location = 2) out float fog;
 
layout (location = 3) out flat uvec2 tex_info;

layout(push_constant) uniform HeightScale
{
	layout(offset = 0) float height_scale;
}height_scale;

const float SCISSOR_ADJUST = 512.0/448.0;

void main() {
    // lq.xy vf22, 0(vi10)          texture load?
    // lq_buffer(Mask::xy, vu.vf22, vu.vi10);

    // lq.xyz vf16, 2(vi10)         vertex load
    // lq_buffer(Mask::xyz, gen.vtx_load0, vu.vi10 + 2);

    // mtir vi02, vf22.x            grab s coordinate of texture
    // vu.vi02 = vu.vf22.x_as_u16();

    // mulaw.xyzw ACC, vf11, vf00   matrix multiply W
    // vu.acc.mula(Mask::xyzw, gen.mat3, vu.vf00.w());
    vec4 transformed;

    // maddax.xyzw ACC, vf08, vf16  matrix multiply X
    // vu.acc.madda(Mask::xyzw, gen.mat0, gen.vtx_load0.x());
    transformed.xyz = position_in * ubo.scale.xyz;
    transformed.z += ubo.mat_32;
    transformed.w = ubo.mat_23 * position_in.z + ubo.mat_33;

    transformed *= -1; // todo?


    // div Q, vf01.x, vf12.w        perspective divide
    // vu.Q = gen.fog.x() / gen.vtx_p0.w();
    float Q = ubo.fog_constants.x / transformed.w;

    fog = 255 - clamp(-transformed.w + ubo.hvdf_offset.w, ubo.fog_constants.y, ubo.fog_constants.z);

    // itof12.xyz vf18, vf22        texture int to float
    // vu.vf18.itof12(Mask::xyz, vu.vf22);
    tex_coord = tex_coord_in / 4096.f; // TODO, more and wrong.

    // mul.xyz vf12, vf12, Q        persepective divide
    // gen.vtx_p0.mul(Mask::xyz, gen.vtx_p0, vu.Q);
    transformed.xyz *= Q;

    // mul.xyz vf18, vf18, Q        texture perspective divide
    // vu.vf18.mul(Mask::xyz, vu.vf18, vu.Q);

    // add.xyzw vf12, vf12, vf04    apply hvdf
    // gen.vtx_p0.add(Mask::xyzw, gen.vtx_p0, gen.hvdf_off);
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

    gl_Position = transformed;
    // scissoring area adjust
    gl_Position.y *= (SCISSOR_ADJUST * height_scale.height_scale);

    fragment_color = vec4(rgba_in.rgb, rgba_in.a * 2);
    tex_info = byte_info.xy;
}
