#version 430 core

layout (location = 0) in vec3 vtx_color;
layout (location = 1) in vec2 vtx_st;
layout (location = 2) in float fog;

layout (location = 0) out vec4 color;

layout (set = 1, binding = 0) uniform UniformBufferObject {
   vec4 fog_color;
   int ignore_alpha;
   int decal_enable;
   int gfx_hack_no_tex;
}ubo;

layout (set = 1, binding = 1) uniform sampler2D tex_T0;

void main() {
    if (ubo.gfx_hack_no_tex == 0) {
      vec4 T0 = texture(tex_T0, vtx_st);

      color.a = T0.a;
      color.rgb = T0.rgb * vtx_color;
      color *= 2;
    } else {
      color.rgb = vtx_color;
      color.a = 1;
    }
}
