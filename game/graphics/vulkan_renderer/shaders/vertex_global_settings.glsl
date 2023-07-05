
//Settings is for any debug flags we may want to use
//Bit 1 is used for gfx_hack_no_tex
//Bit 0 is used for decal mode

layout(push_constant) uniform PushConstant {
  layout(offset = 0) mat4 camera;
  layout(offset = 64)vec4 hvdf_offset;
  layout(offset = 80)float fog_constant;
  layout(offset = 84)float fog_min;
  layout(offset = 88)float fog_max;
  layout(offset = 92)float height_scale;
  layout(offset = 96)float scissor_adjust;
  layout(offset = 100) int settings;
} pc;
