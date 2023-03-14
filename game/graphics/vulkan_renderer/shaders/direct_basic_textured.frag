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

vec4 sample_tex(vec2 coord, uint unit) {
   return texture(tex_T0, coord);
}

vec4 sample_tex_px(vec2 coordf, uint unit) {
    ivec2 coord;
    coord.x = int(coordf.x / 16);
    coord.y = int(coordf.y / 16);
    return texelFetch(tex_T0, coord, 0);
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
