#version 450

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_proj;
    vec4 camera_pos;
    vec4 light_dir;    // xyz towards the light, w = ambient
    vec4 fog_color;    // rgb, a = fog density
    vec4 sky_color;
} frame;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_world;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_layer;

// Detail textures (render/textures.cpp): 0..1 stored, 0..2 meant.
layout(set = 0, binding = 1) uniform sampler2DArray textures;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(frame.light_dir.xyz);
    vec3 v = normalize(frame.camera_pos.xyz - v_world);
    float ambient = frame.light_dir.w;
    vec3 albedo = v_color.rgb * texture(textures, vec3(v_uv, float(v_layer))).rgb * 2.0;

    // Soft wrapped diffuse: the shadow side stays readable, the terminator is
    // smooth over rounded shapes.
    float ndl = dot(n, l);
    float diffuse = smoothstep(-0.35, 0.9, ndl);
    // Hemisphere ambient: faces towards the sky take a touch of its colour.
    float up = n.y * 0.5 + 0.5;
    vec3 amb = ambient * mix(vec3(0.85, 0.82, 0.8), mix(vec3(1.0), frame.sky_color.rgb * 1.4, 0.35), up);
    vec3 lit = albedo * (amb + (1.0 - ambient) * diffuse * vec3(1.0, 0.98, 0.94));

    // A broad glossy highlight and a thin rim: plastic and paint, not matte.
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 36.0) * 0.28 * step(0.0, ndl);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.12 * (0.5 + 0.5 * ndl);
    lit += (spec + rim) * mix(vec3(1.0), frame.sky_color.rgb, 0.3);

    float dist = length(v_world - frame.camera_pos.xyz);
    float fog = 1.0 - exp(-dist * frame.fog_color.a);
    out_color = vec4(mix(lit, frame.fog_color.rgb, clamp(fog, 0.0, 1.0)), v_color.a);
}
