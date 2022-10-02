#version 430 core

layout (location = 0) in vec3 position_in;
layout (location = 1) in vec4 rgba_in;
layout (location = 2) in vec3 tex_coord_in;
layout (location = 3) in uint fog_in;

layout (location = 0) out vec4 fragment_color;
layout (location = 1) out vec3 tex_coord;
layout (location = 2) out float fog;

const float SCISSOR_ADJUST = 512.0/448.0;

layout (set = 0, binding = 0) uniform UniformBufferObject {
   int bucket;
} ubo;

void main() {
    gl_Position = vec4((position_in.x - 0.5) * 16., -(position_in.y - 0.5) * 32, position_in.z * 2 - 1., 1.0);
    // scissoring area adjust
    gl_Position.y *= SCISSOR_ADJUST;
    fragment_color = vec4(rgba_in.rgb, rgba_in.a * 2);
    tex_coord = tex_coord_in;
    fog = 255 - fog_in;
    
    if (ubo.bucket == 0) {
        fragment_color.rgb *= 2;
    } else if (ubo.bucket == 1 || ubo.bucket == 3) {
        fragment_color *= 2;
    } else if (ubo.bucket == 4) {
        fragment_color.a = 0;
    }
}
