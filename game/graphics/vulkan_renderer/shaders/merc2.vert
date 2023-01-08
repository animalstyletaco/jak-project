#version 430 core

// merc vertex definition
layout (location = 0) in vec3 position_in;
layout (location = 1) in vec3 normal_in;
layout (location = 2) in vec3 weights_in;
layout (location = 3) in vec2 st_in;
layout (location = 4) in vec3 rgba;
layout (location = 5) in uvec3 mats;

// light control
struct LightControlUniformBufferObject {
   vec3 light_dir0;
   vec3 light_dir1;
   vec3 light_dir2;
   vec3 light_col0;
   vec3 light_col1;
   vec3 light_col2;
   vec3 light_ambient;
};

// camera control
struct CameraControlUniformBufferObject {
   vec4 hvdf_offset;
   vec4 perspective0;
   vec4 perspective1;
   vec4 perspective2;
   vec4 perspective3;
   vec4 fog_constants;
};

layout (set = 0, binding = 0) uniform MercUniformBufferObject {
   LightControlUniformBufferObject light_control;
   CameraControlUniformBufferObject camera_control;
   mat4 perspective_matrix;
} ubo;

layout(push_constant) uniform HeightScale
{
	float height_scale;
}height_scale;

const float SCISSOR_ADJUST = 512.0/448.0;

// output
layout (location = 0) out vec3 vtx_color;
layout (location = 1) out vec2 vtx_st;

layout (location = 2) out float fog;

struct MercMatrixData {
    mat4 X;
    mat3 R;
    vec4 pad;
};

layout (std140, binding = 1) uniform ub_bones {
    MercMatrixData bones[128];
};


/*
The inputs are in registers 8, 10, 12, 25, and the outputs are 9, 11, 13, 26. The output is written over the input.

```
mula.xyzw ACC, vf15, vf08
maddz.xyzw vf09, vf16, vf08
mula.xyzw ACC, vf15, vf10
maddz.xyzw vf11, vf16, vf10
mula.xyzw ACC, vf15, vf12
maddz.xyzw vf13, vf16, vf12
addax.xyzw vf20, vf00
madda.xyzw ACC, vf27, vf25
maddz.xyzw vf26, vf28, vf25
```
*/
void main() {
    //    vec4 transformed = -ubo.camera_control.perspective3.xyzw;
    //    transformed += -ubo.camera_control.perspective0 * position_in.x;
    //    transformed += -ubo.camera_control.perspective1 * position_in.y;
    //    transformed += -ubo.camera_control.perspective2 * position_in.z;


//    vec4 transformed = -hmat3.xyzw;
//    transformed += -hmat0 * position_in.x;
//    transformed += -hmat1 * position_in.y;
//    transformed += -hmat2 * position_in.z;

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

    vec4 transformed = ubo.perspective_matrix * vtx_pos;

    rotated_nrm = normalize(rotated_nrm);
    vec3 light_intensity = ubo.light_control.light_dir0 * rotated_nrm.x + ubo.light_control.light_dir1 * rotated_nrm.y + ubo.light_control.light_dir2 * rotated_nrm.z;
    light_intensity = max(light_intensity, vec3(0, 0, 0));

    vec3 light_color_no_ambient = light_intensity.x * ubo.light_control.light_col0
                                  + light_intensity.y * ubo.light_control.light_col1
                                  + light_intensity.z * ubo.light_control.light_col2;

    vec3 light_color = ubo.light_control.light_ambient + light_color_no_ambient;

    float Q = ubo.camera_control.fog_constants.x / transformed[3];
    fog = 255 - clamp(-transformed.w + ubo.camera_control.hvdf_offset.w, ubo.camera_control.fog_constants.y, ubo.camera_control.fog_constants.z);

    transformed.xyz *= Q;
    transformed.xyz += ubo.camera_control.hvdf_offset.xyz;
    transformed.xy -= (2048.);
    transformed.z /= (8388608);
    transformed.z -= 1;
    transformed.x /= (256);
    transformed.y /= -(128);
    transformed.xyz *= transformed.w;
    transformed.y *= (SCISSOR_ADJUST * height_scale.height_scale);
    gl_Position = transformed;


    vtx_color = rgba * light_color;
    vtx_st = st_in;
}
