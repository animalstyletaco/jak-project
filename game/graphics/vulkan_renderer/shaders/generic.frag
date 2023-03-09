#version 430 core


layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec4 fragment_color;
layout (location = 2) in float fog;
layout (location = 3) in flat uvec2 tex_info;

layout (set = 1, binding = 0) uniform UniformBufferObject {
  float alpha_reject;
  float color_mult;
  vec4 fog_color;
} ubo;

layout(push_constant) uniform PER_OBJECT
{
	layout(offset = 8) int imgIdx;
}pc;

//FIXME: This should be texture2Darray
const int MAX_BUCKET_COUNT = 800;
layout (set = 1, binding = 1) uniform sampler2D textures[MAX_BUCKET_COUNT];

layout (location = 0) out vec4 color;

vec4 sample_tex(vec2 coord, uint unit) {
    return texture(textures[unit], coord);

//    switch (unit) {
//        case 0: return texture(tex_T0, coord);
//        case 1: return texture(tex_T1, coord);
//        case 2: return texture(tex_T2, coord);
//        case 3: return texture(tex_T3, coord);
//        case 4: return texture(tex_T4, coord);
//        case 5: return texture(tex_T5, coord);
//        case 6: return texture(tex_T6, coord);
//        case 7: return texture(tex_T7, coord);
//        case 8: return texture(tex_T8, coord);
//        case 9: return texture(tex_T9, coord);
//        default : return vec4(1.0, 0, 1.0, 1.0);
//    }
}

void main() {
    //vec4 T0 = sample_tex(tex_coord.xy, tex_info.x);
    vec4 T0 = texture(textures[pc.imgIdx], tex_coord.xy);
    // y is tcc
    // z is decal

    if ((tex_info.y & 1u) == 0) {
        if ((tex_info.y & 2u) == 0) {
            // modulate + no tcc
            color.xyz = fragment_color.xyz * T0.xyz;
            color.w = fragment_color.w;
        } else {
            // decal + no tcc
            color.xyz = T0.xyz * 0.5;
            color.w = fragment_color.w;
        }
    } else {
        if ((tex_info.y & 2u) == 0) {
            // modulate + tcc
            color = fragment_color * T0;
        } else {
            // decal + tcc
            color.xyz = T0.xyz * 0.5;
            color.w = T0.w;
        }
    }
    color *= 2;
    color.xyz *= ubo.color_mult;

    if (color.a < ubo.alpha_reject) {
        discard;
    }
    if ((tex_info.y & 4u) != 0) {
        color.xyz = mix(color.xyz, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
    }
}
