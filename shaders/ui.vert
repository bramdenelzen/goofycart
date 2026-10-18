#version 450

layout(push_constant) uniform Ui {
    vec2 inv_half_size; // 2 / screen size in pixels
} ui;

layout(location = 0) in vec2 in_pos;   // pixels, origin top-left
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_mode;
layout(location = 4) in vec4 in_params;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out uint v_mode;
layout(location = 3) out vec4 v_params;

void main() {
    v_color = in_color;
    v_uv = in_uv;
    v_mode = in_mode;
    v_params = in_params;
    gl_Position = vec4(in_pos * ui.inv_half_size - 1.0, 0.0, 1.0);
}
