#version 450
// Depth from the sun, for the shadow map.

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_proj;
    vec4 camera_pos;
    vec4 light_dir;
    vec4 fog_color;
    vec4 sky_color;
    mat4 light_vp;
    vec4 shadow;
} frame;

layout(push_constant) uniform Draw {
    mat4 model;
    vec4 tint;
    ivec4 flags;
} draw;

layout(location = 0) in vec3 in_pos;

void main() {
    gl_Position = frame.light_vp * draw.model * vec4(in_pos, 1.0);
}
