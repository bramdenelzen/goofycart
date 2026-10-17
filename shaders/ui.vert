#version 450

layout(push_constant) uniform Ui {
    vec2 inv_half_size; // 2 / screen size in pixels
} ui;

layout(location = 0) in vec2 in_pos;   // pixels, origin top-left
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 v_color;

void main() {
    v_color = in_color;
    gl_Position = vec4(in_pos * ui.inv_half_size - 1.0, 0.0, 1.0);
}
