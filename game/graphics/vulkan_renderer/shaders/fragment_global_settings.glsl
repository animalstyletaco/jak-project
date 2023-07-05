//Background push constant

layout (push_constant) uniform PushConstant {
  layout(offset = 104) float alpha_min;
  layout(offset = 108) float alpha_max;
  layout(offset = 112) vec4 fog_color;
} pc;
