#version 450

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_proj;
    vec4 camera_pos;
    vec4 light_dir;    // xyz towards the light, w = ambient
    vec4 fog_color;    // rgb, a = fog density
    vec4 sky_color;
    mat4 light_vp;     // world to shadow map
    vec4 shadow;       // x: 1 = shadows on, y: one shadow-map texel in uv
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
    // Glow (fx 3) carries its colour in the tint; the surface keeps its own.
    v_color = draw.flags.w == 3 || draw.flags.w == 7 ? in_color : in_color * draw.tint;
    v_uv = in_uv;
    if (draw.flags.w == 7) {
        // Wavy road: lifted by amplitude (tint.x) * sin(k' * v + phase) with
        // k' (tint.y) and the phase (tint.z) set by the game from the tick,
        // the same wave the simulation drives on; the vertex alpha eases the
        // wave in and out at the ends of the stretch.
        world.y += in_color.a * draw.tint.x * sin(in_uv.y * draw.tint.y + draw.tint.z);
        v_world = world.xyz;
        v_color.a = 1.0;
    }
    if (draw.flags.w == 4) { // flames lick about, more towards the tip
        float t = frame.shadow.w * 22.0 + draw.tint.a * 40.0;
        world.xyz += mat3(draw.model) * vec3(sin(t + in_pos.z * 6.0) * 0.07, cos(t * 0.7 + in_pos.z * 5.0) * 0.05, 0.0) * in_uv.y;
    }
    v_layer = in_layer;
    if (in_layer == 7u && draw.flags.x == 0 && draw.flags.w == 0 && in_normal.y > 0.5) {
        // Water: a low swell from two sine waves, 12 m and 7 m long at 60
        // degrees to each other, 0.15 m in all. Time snaps to 12 steps a
        // second for a stop-motion feel. The vertex alpha is the distance to
        // the shore (0 at the bank, 1 from 6 m out); the swell dies out at the
        // bank so the water meets the road and walls without a crack.
        float t = floor(frame.shadow.w * 12.0) / 12.0;
        float shore = in_color.a;
        float amp = 0.15 * smoothstep(0.0, 0.35, shore);
        const vec2 d1 = vec2(0.8, 0.6);
        const vec2 d2 = vec2(0.8 * 0.5 - 0.6 * 0.8660254, 0.8 * 0.8660254 + 0.6 * 0.5); // d1 turned 60 degrees
        const float k1 = 6.2831853 / 12.0, k2 = 6.2831853 / 7.0;
        float a1 = amp * 0.6, a2 = amp * 0.4;
        float p1 = dot(d1, world.xz) * k1 - t * 1.4, p2 = dot(d2, world.xz) * k2 - t * 1.9;
        float h = a1 * sin(p1) + a2 * sin(p2);
        vec2 slope = d1 * (a1 * k1 * cos(p1)) + d2 * (a2 * k2 * cos(p2));
        world.y += h;
        v_world = world.xyz;
        v_normal = normalize(vec3(-slope.x, 1.0, -slope.y));
        // The fragment shader gets the crest height (0 trough .. 1 crest) and the shore distance.
        v_uv = vec2(amp > 0.0 ? h / 0.15 * 0.5 + 0.5 : 0.5, shore);
    }
    gl_Position = frame.view_proj * world;
}
