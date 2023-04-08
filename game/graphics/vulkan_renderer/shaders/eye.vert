#version 430 core
layout (location = 0) in vec4 xyst_in;

layout (location = 0) out vec2 st;

void main() {
    //Inverted y-axis since vulkan and opengl y_axis are inverted
    gl_Position = vec4((xyst_in.x - 768.f) / 256.f, -1 * ((xyst_in.y - 768.f) / 256.f), 0, 1);
    st = xyst_in.zw;
}
