#version 430 core

layout (location = 0) out vec4 color;

layout (location = 0) in vec4 fragment_color;
layout (location = 1) in vec3 tex_coord;
layout (location = 2) in float fog;
layout (location = 3) in flat uvec4 tex_info;
layout (location = 4) in flat uint use_uv;

layout (set = 0, binding = 0) uniform UniformBufferObject {
  float alpha_reject;
  float color_mult;
  float alpha_mult;
  float alpha_sub;
  vec4 fog_color;
  float ta0;
} ubo;

//TODO: See if we can set this up into some sort of array
layout (set = 0, binding = 1) uniform sampler2D tex_T0;
layout (set = 0, binding = 2) uniform sampler2D tex_T1;
layout (set = 0, binding = 3) uniform sampler2D tex_T2;
layout (set = 0, binding = 4) uniform sampler2D tex_T3;
layout (set = 0, binding = 5) uniform sampler2D tex_T4;
layout (set = 0, binding = 6) uniform sampler2D tex_T5;
layout (set = 0, binding = 7) uniform sampler2D tex_T6;
layout (set = 0, binding = 8) uniform sampler2D tex_T7;
layout (set = 0, binding = 9) uniform sampler2D tex_T8;
layout (set = 0, binding = 10) uniform sampler2D tex_T9;

vec4 sample_tex(vec2 coord, uint unit) {
    switch (unit) {
        case 0: return texture(tex_T0, coord);
        case 1: return texture(tex_T1, coord);
        case 2: return texture(tex_T2, coord);
        case 3: return texture(tex_T3, coord);
        case 4: return texture(tex_T4, coord);
        case 5: return texture(tex_T5, coord);
        case 6: return texture(tex_T6, coord);
        case 7: return texture(tex_T7, coord);
        case 8: return texture(tex_T8, coord);
        case 9: return texture(tex_T9, coord);
        default : return vec4(1.0, 0, 1.0, 1.0);
    }
}

vec4 sample_tex_px(vec2 coordf, uint unit) {
    ivec2 coord;
    coord.x = int(coordf.x / 16);
    coord.y = int(coordf.y / 16);
    switch (unit) {
        case 0: return texelFetch(tex_T0, coord, 0);
        case 1: return texelFetch(tex_T1, coord, 0);
        case 2: return texelFetch(tex_T2, coord, 0);
        case 3: return texelFetch(tex_T3, coord, 0);
        case 4: return texelFetch(tex_T4, coord, 0);
        case 5: return texelFetch(tex_T5, coord, 0);
        case 6: return texelFetch(tex_T6, coord, 0);
        case 7: return texelFetch(tex_T7, coord, 0);
        case 8: return texelFetch(tex_T8, coord, 0);
        case 9: return texelFetch(tex_T9, coord, 0);
        default : return vec4(1.0, 0, 1.0, 1.0);
    }
}

void main() {
    vec4 T0;
    if (use_uv == 1) {
        T0 = sample_tex_px(tex_coord.xy, tex_info.x);
    } else {
        T0 = sample_tex(tex_coord.xy / tex_coord.z, tex_info.x);
    }
    // y is tcc
    // z is decal
    if (T0.w == 0) {
        T0.w = ubo.ta0;
    }

    if (tex_info.y == 0) {
        if (tex_info.z == 0) {
            // modulate + no tcc
            color.xyz = fragment_color.xyz * T0.xyz;
            color.w = fragment_color.w;
        } else {
            // decal + no tcc
            color.xyz = T0.xyz * 0.5;
            color.w = fragment_color.w;
        }
    } else {
        if (tex_info.z == 0) {
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
    color.w *= ubo.alpha_mult;
    if (color.a < ubo.alpha_reject) {
        discard;
    }
    if (tex_info.w == 1) {
        color.xyz = mix(color.xyz, ubo.fog_color.rgb, clamp(ubo.fog_color.a * fog, 0, 1));
    }

}
