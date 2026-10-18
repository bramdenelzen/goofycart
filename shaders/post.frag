#version 450
// Volumetric light and fog over the finished scene: sun shafts marched
// through the sky mask towards the sun, and a low, drifting mist in the
// hollows below the camera, both from the depth buffer.

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_proj;
    vec4 camera_pos;
    vec4 light_dir;    // xyz towards the light, w = ambient
    vec4 fog_color;    // rgb, a = fog density
    vec4 sky_color;
    mat4 light_vp;
    vec4 shadow;       // z: sun strength, w: time
} frame;

layout(push_constant) uniform Post {
    mat4 inv_view_proj;
    vec4 params; // x: shafts strength, y: mist strength, z: aspect, w: unused
    vec4 range;  // x: near plane, y: far plane
} post;

layout(set = 1, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 1) uniform sampler2D depth;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}

void main() {
    vec3 col = texture(scene, v_uv).rgb;
    float d = texture(depth, v_uv).r;
    // Linear distance along the view from the depth value; the sky dome sits near the far plane.
    float near = post.range.x, far = post.range.y;
    float lin = near * far / max(far - d * (far - near), 1e-3);
    bool sky = lin > far * 0.8;
    float sun = clamp(frame.shadow.z, 0.0, 1.0);
    vec3 l = normalize(frame.light_dir.xyz);
    vec3 sun_col = mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.9, 0.7), sun);

    // ---- sun shafts: how much open sky lies between this pixel and the sun
    vec4 sp = frame.view_proj * vec4(frame.camera_pos.xyz + l * 4000.0, 1.0);
    if (sp.w > 0.0 && post.params.x > 0.0) {
        vec2 suv = (sp.xy / sp.w) * 0.5 + 0.5;
        // Fade the effect out as the sun leaves the screen.
        vec2 off = (suv - 0.5) * vec2(post.params.z, 1.0);
        float on_screen = 1.0 - smoothstep(0.7, 1.6, length(off));
        if (on_screen > 0.0) {
            const int N = 28;
            vec2 step = (suv - v_uv) / float(N) * 0.9;
            vec2 p = v_uv + step * hash2(v_uv * 913.0 + frame.shadow.w); // dithered start hides banding
            float acc = 0.0, w = 1.0;
            for (int i = 0; i < N; ++i) {
                float sd = texture(depth, clamp(p, vec2(0.0), vec2(1.0))).r;
                float sl = near * far / max(far - sd * (far - near), 1e-3);
                acc += (sl > far * 0.8 ? 1.0 : 0.0) * w;
                w *= 0.955;
                p += step;
            }
            float rays = acc / float(N);
            // Brighter close to the sun, and only where this pixel itself is not in the sky's own glare.
            float near_sun = 1.0 - smoothstep(0.0, 0.9, length((v_uv - suv) * vec2(post.params.z, 1.0)));
            col += sun_col * rays * (0.3 + 0.7 * near_sun) * post.params.x * on_screen * sun;
        }
    }

    // ---- mist: a drifting ground fog in the low ground, thicker with distance
    if (!sky && post.params.y > 0.0) {
        vec4 wp = post.inv_view_proj * vec4(v_uv * 2.0 - 1.0, d, 1.0);
        vec3 world = wp.xyz / wp.w;
        float dist = length(world - frame.camera_pos.xyz);
        float floor_y = frame.camera_pos.y - 3.5;
        float low = clamp(1.0 - (world.y - floor_y) / 8.0, 0.0, 1.0);
        float t = frame.shadow.w;
        float n = vnoise(world.xz * 0.045 + vec2(t * 0.06, t * 0.03)) * 0.6 + vnoise(world.xz * 0.11 - vec2(t * 0.04, t * 0.07)) * 0.4;
        float mist = low * (0.3 + 0.7 * n) * (1.0 - exp(-dist * 0.05)) * 1.5 * post.params.y;
        vec3 mist_col = mix(frame.fog_color.rgb, sun_col, 0.3 * sun) * 1.05;
        col = mix(col, mist_col, clamp(mist, 0.0, 0.7));
    }
    out_color = vec4(col, 1.0);
}
