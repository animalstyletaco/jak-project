#version 430 core

// merc vertex definition
layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 normal_in;
layout (location = 2) in vec3 weights_in;
layout (location = 3) in vec2 st_in;
layout (location = 4) in vec3 rgba;
layout (location = 5) in uvec3 mats;

// output
layout (location = 0) out vec3 vtx_color;
layout (location = 1) out vec2 vtx_st;
layout (location = 2) out float fog;

// camera control
layout(set = 0, binding = 0) uniform EmercUniformBufferObject {
   vec4 hvdf_offset;
   vec4 fog_constants;
   vec4 fade;
   mat4 perspective_matrix;
}emerc_ubo;

layout(push_constant) uniform PushConstant
{
	float height_scale;
  float scissor_adjust;
}pc;

struct MercMatrixData {
    mat4 X;
    mat3 R;
    vec4 pad;
};

layout (std140, set=0, binding = 1) uniform ub_bones {
    MercMatrixData bones[128];
};

void main() {


    vec4 p = vec4(position_in, 1);
    vec4 vtx_pos = -bones[mats[0]].X * p * weights_in[0];
    vec3 rotated_nrm = bones[mats[0]].R * normal_in * weights_in[0];

    // game may send garbage bones if the weight is 0, don't let NaNs sneak in.
    if (weights_in[1] > 0) {
        vtx_pos += -bones[mats[1]].X * p * weights_in[1];
        rotated_nrm += bones[mats[1]].R * normal_in * weights_in[1];
    }
    if (weights_in[2] > 0) {
        vtx_pos += -bones[mats[2]].X * p * weights_in[2];
        rotated_nrm += bones[mats[2]].R * normal_in * weights_in[2];
    }

    vec4 transformed = emerc_ubo.perspective_matrix * vtx_pos;

    rotated_nrm = normalize(rotated_nrm);

    float Q = emerc_ubo.fog_constants.x / transformed[3];
    fog = 255 - clamp(-transformed.w + emerc_ubo.hvdf_offset.w, emerc_ubo.fog_constants.y, emerc_ubo.fog_constants.z);

    // emerc
    vec2 st_mod = st_in;
    {
      vec4 vf10 = vec4(rotated_nrm, 1); // ??
      // vf08 = transformed
      vec4 vf08 = transformed;
      // vf23 = unperspect
      // unperspect (1/P(0, 0), 1/P(1, 1), 0.5, 1/P(2, 3))
      vec4 vf23 = vec4(1. / emerc_ubo.perspective_matrix[0][0],
                       1. / emerc_ubo.perspective_matrix[1][1],
                       0.5,
                       1. / emerc_ubo.perspective_matrix[2][3]);
      // vf14 = rgba-fade
      vec4 vf14 = emerc_ubo.fade;
      // vf24 = normal st
      // mul.xyzw vf09, vf08, vf23 ;; do unperspect
      vec4 vf09 = vf08 * vf23;
      //subw.z vf10, vf10, vf00 ;; subtract 1 from z
      vf10.z -= 1;
      //addw.z vf09, vf00, vf09 ;; xyww the unperspected thing
      vf09.z = vf09.w;
      //mul.xyz vf15, vf09, vf10 ;;
      vec3 vf15 = vf09.xyz * vf10.xyz;
      //adday.xyzw vf15, vf15
      //maddz.x vf15, vf21, vf15
      float vf15_x = vf15.x + vf15.y + vf15.z;
      //div Q, vf15.x, vf10.z
      float qq = vf15_x / vf10.z;
      //mulaw.xyzw ACC, vf09, vf00
      vec4 ACC = vf09;
      //mul.xyzw vf09, vf08, vf23
      vf09 = vf08 * vf23;
      //madd.xyzw vf10, vf10, Q
      vf10 = ACC + vf10 * qq;
      //eleng.xyz P, vf10
      float P = length(vf10.xyz);
      //mfp.w vf10, P
      //div Q, vf23.z, vf10.w
      float qqq = vf23.z / P;
      //addaz.xyzw vf00, vf23
      ACC = vec4(vf23.z, vf23.z, vf23.z, vf23.z + 1.);
      //madd.xyzw vf10, vf10, Q
      vf10 = ACC + vf10 * qqq;
      st_mod = vf10.xy;

      // this is required to make jak 1's envmapping look right
      // otherwise it behaves like the envmap texture is mirrored.
      // TODO: see if this is right for jak 2 or not.
      // It _might_ make sense that this exists because we skip the multiply by Q
      // below, and Q is negative (no idea how that works out with clamp).
      st_mod.x = 1 - vf10.x;
      st_mod.y = 1 - vf10.y;

      //mulz.xy vf24, vf10, vf24 ;; mul tex by q
    }

    transformed.xyz *= Q;
    transformed.xyz += emerc_ubo.hvdf_offset.xyz;
    transformed.xy -= (2048.);
    transformed.z /= (8388608);
    transformed.z -= 1;
    transformed.x /= (256);
    transformed.y /= -(128);
    transformed.xyz *= transformed.w;
    transformed.y *= pc.height_scale * pc.scissor_adjust;
    gl_Position = transformed;


    vtx_color = emerc_ubo.fade.xyz;
    vtx_st = st_mod;
}
