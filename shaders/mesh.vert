#version 450

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_proj;
    vec4 camera_pos;   // xyz, w unused
    vec4 light_dir;    // xyz towards the light, w = ambient
    vec4 fog_color;    // rgb, a = fog density
    vec4 sky_color;
} frame;

layout(push_constant) uniform Draw {
    mat4 model;
    vec4 tint;
    ivec4 flags; // x: 1 = textured model (set 1), else the detail array
} draw;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in uint in_layer;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_world;
layout(location = 3) out vec2 v_uv;
layout(location = 4) flat out uint v_layer;

void main() {
    vec4 world = draw.model * vec4(in_pos, 1.0);
    v_world = world.xyz;
    v_normal = mat3(draw.model) * in_normal;
    v_color = in_color * draw.tint;
    v_uv = in_uv;
    v_layer = in_layer;
    gl_Position = frame.view_proj * world;
}
