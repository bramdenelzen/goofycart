#version 450

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_proj;
    vec4 camera_pos;
    vec4 light_dir;
    vec4 fog_color;
    vec4 sky_color;
} frame;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_world;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_layer;

// Low-res detail textures (render/textures.cpp): 0..1 stored, 0..2 meant.
layout(set = 0, binding = 1) uniform sampler2DArray textures;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(v_normal);
    float ndl = max(dot(n, normalize(frame.light_dir.xyz)), 0.0);
    // Three flat bands: a toon look that suits faceted low-poly models.
    float band = ndl > 0.6 ? 1.0 : (ndl > 0.25 ? 0.72 : 0.5);
    float ambient = frame.light_dir.w;
    vec3 albedo = v_color.rgb * texture(textures, vec3(v_uv, float(v_layer))).rgb * 2.0;
    vec3 lit = albedo * mix(ambient, 1.0, band);
    float dist = length(v_world - frame.camera_pos.xyz);
    float fog = 1.0 - exp(-dist * frame.fog_color.a);
    out_color = vec4(mix(lit, frame.fog_color.rgb, clamp(fog, 0.0, 1.0)), v_color.a);
}
