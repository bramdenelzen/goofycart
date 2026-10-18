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

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_world;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_layer;

// Detail textures (render/textures.cpp): 0..1 stored, 0..2 meant.
layout(set = 0, binding = 1) uniform sampler2DArray textures;
// A loaded model's own texture, when draw.flags.x is set.
layout(set = 1, binding = 0) uniform sampler2D albedo_tex;
// The sun's shadow map, depth-compared.
layout(set = 0, binding = 2) uniform sampler2DShadow shadow_map;

// 0 in shadow .. 1 lit, softened over a 4x4 neighbourhood.
float sun_visibility(vec3 world, vec3 n) {
    if (frame.shadow.x < 0.5) return 1.0;
    vec4 p = frame.light_vp * vec4(world + n * 0.12, 1.0);
    vec3 s = p.xyz / p.w;
    vec2 uv = s.xy * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || s.z >= 1.0) return 1.0;
    float t = frame.shadow.y, sum = 0.0;
    for (int y = -1; y <= 2; ++y)
        for (int x = -1; x <= 2; ++x) sum += texture(shadow_map, vec3(uv + (vec2(x, y) - 0.5) * t * 2.2, s.z - 0.0008));
    float v = sum / 16.0;
    // Fade out towards the edge of the shadowed area.
    vec2 e = abs(uv - 0.5) * 2.0;
    return mix(v, 1.0, smoothstep(0.85, 1.0, max(e.x, e.y)));
}

layout(location = 0) out vec4 out_color;

// Filmic response: highlights roll off instead of clipping, mid-tones get
// a little contrast, colours a touch less candy.
vec3 grade(vec3 c) {
    // A soft shoulder above 0.8 so bright sun never clips flat.
    vec3 over = max(c - 0.8, 0.0);
    c = min(c, 0.8) + 0.2 * (1.0 - exp(-over * 3.0));
    // Arcade punch: an S-curve for contrast and richer colour.
    // (gentler at night, where it would crush the dark road)
    c = mix(c, c * c * (3.0 - 2.0 * c), mix(0.04, 0.14, clamp(frame.shadow.z, 0.0, 1.0)));
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, 1.15);
    // Warm highlights, slightly cool shadows.
    c *= mix(vec3(0.95, 0.98, 1.05), vec3(1.04, 1.0, 0.94), smoothstep(0.2, 0.8, luma));
    return clamp(c, 0.0, 1.0);
}

// Smooth value noise for large-scale variation across the ground.
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}

// The haze colour along a view direction: warmer towards the sun.
vec3 haze(vec3 dir, vec3 l, float sun) {
    float toward = pow(max(dot(dir, l), 0.0), 6.0);
    return mix(frame.fog_color.rgb, mix(frame.fog_color.rgb, vec3(1.0, 0.86, 0.66), 0.55), toward * clamp(sun, 0.0, 1.0));
}

void main() {
    vec3 l = normalize(frame.light_dir.xyz);
    float sun = frame.shadow.z; // 1 by day, low under the moon
    if (draw.flags.w == 1) { // the sky dome: the painted sky by direction, plus the sun
        vec3 dir = normalize(v_world - frame.camera_pos.xyz);
        vec3 col;
        if (draw.flags.x == 1) {
            float u = atan(dir.z, dir.x) / 6.2831853 + 0.5;
            float el = asin(clamp(dir.y, -1.0, 1.0));
            float vv = clamp(0.88 - el / 1.5707963 * 0.88, 0.0, 0.999);
            col = textureLod(albedo_tex, vec2(u, vv), 0.0).rgb;
        } else {
            float up = clamp(dir.y, 0.0, 1.0);
            col = mix(frame.fog_color.rgb, frame.sky_color.rgb, pow(up, 0.5));
        }
        float d = max(dot(dir, l), 0.0);
        vec3 sun_col = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.7), clamp(sun, 0.0, 1.0));
        col += sun_col * (pow(d, 900.0) * 2.5 + pow(d, 60.0) * 0.3 + pow(d, 8.0) * 0.12) * clamp(sun, 0.2, 1.0);
        out_color = vec4(grade(col), 1.0);
        return;
    }
    if (draw.flags.y == 1) { // unlit: the texture as is, fogged
        vec4 t = texture(albedo_tex, v_uv) * v_color;
        if (t.a < 0.01) discard;
        float d = length(v_world - frame.camera_pos.xyz);
        float f = 1.0 - exp(-d * frame.fog_color.a);
        vec3 dir = (v_world - frame.camera_pos.xyz) / max(d, 1e-3);
        out_color = vec4(grade(mix(t.rgb, haze(dir, l, sun), clamp(f, 0.0, 1.0))), t.a);
        return;
    }
    vec3 n = normalize(v_normal);
    vec3 v = normalize(frame.camera_pos.xyz - v_world);
    float ambient = frame.light_dir.w;
    vec3 albedo;
    float alpha = v_color.a;
    float emission = 0.0;
    if (draw.flags.z == 1) { // glow texture: alpha 1 = plain surface, 0 = lit from within
        vec4 t = texture(albedo_tex, v_uv);
        albedo = v_color.rgb * t.rgb * 1.25;
        emission = 1.0 - t.a;
    } else if (draw.flags.x == 1) {
        vec4 t = texture(albedo_tex, v_uv);
        if (t.a < 0.08) discard; // cut-outs: leaves, hair, grilles
        albedo = v_color.rgb * t.rgb * 1.25; // game albedos expect a brighter pipeline
        alpha *= t.a;
    } else {
        albedo = v_color.rgb * texture(textures, vec3(v_uv, float(v_layer))).rgb * 2.0;
        // Break up big flat areas: patches of wear and colour over tens of metres,
        // and mowing stripes on grass.
        if (v_layer != 0u) {
            float macro = vnoise(v_world.xz * 0.035) * 0.6 + vnoise(v_world.xz * 0.12 + 17.0) * 0.4;
            albedo *= 0.86 + 0.28 * macro;
        }
        if (v_layer == 2u) albedo *= 0.92 + 0.13 * smoothstep(0.42, 0.58, fract((v_world.x + v_world.z) * 0.055));
    }

    // Diffuse with a soft terminator; shadowed faces keep a little sun.
    float ndl = dot(n, l);
    float vis = sun_visibility(v_world, n);
    // Soft light: a wide terminator and shadows filled by the sky, not black.
    float diffuse = smoothstep(-0.45, 0.95, ndl) * mix(0.45, 1.0, vis);
    // Hemisphere ambient: sky from above, warm bounce from the ground below.
    float up = n.y * 0.5 + 0.5;
    vec3 sky_amb = mix(frame.sky_color.rgb, vec3(0.8, 0.86, 0.95), 0.55) * 1.1;
    vec3 ground_amb = mix(vec3(0.55, 0.47, 0.36), frame.fog_color.rgb, 0.25);
    vec3 amb = ambient * mix(ground_amb, sky_amb, up);
    // Occlusion near the ground: the bottoms of things sit in their own shade.
    float ao = mix(0.72, 1.0, smoothstep(-0.6, 0.6, n.y)) ;
    vec3 sun_col = mix(vec3(0.5, 0.6, 0.95), vec3(1.0, 0.9, 0.74), clamp(sun, 0.0, 1.0));
    vec3 lit = albedo * (amb * ao * 0.9 + (1.0 - ambient * 0.5) * diffuse * sun * sun_col);

    // A soft sheen, not plastic: broad and faint, and only in the sun.
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 18.0) * 0.07 * step(0.0, ndl) * vis;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 4.0) * 0.05 * (0.5 + 0.5 * ndl);
    lit += (spec * sun_col + rim * sky_amb) * max(sun, 0.3);
    lit = mix(lit, albedo * 1.7, emission); // windows, neon and lamps light themselves

    // Aerial perspective: distance and low ground thicken the haze.
    vec3 to = v_world - frame.camera_pos.xyz;
    float dist = length(to);
    float height_k = mix(1.0, 0.55, clamp((v_world.y + 10.0) / 60.0, 0.0, 1.0));
    float fog = (1.0 - exp(-dist * frame.fog_color.a * height_k)) * (1.0 - 0.85 * emission); // lights shine through the haze
    out_color = vec4(grade(mix(lit, haze(to / max(dist, 1e-3), l, sun), clamp(fog, 0.0, 1.0))), alpha);
}
