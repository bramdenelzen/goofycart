#version 450

// The 2D overlay. Mode 0: flat colour. Mode 1: text from a signed distance
// field (params.x = the edge, lower is bolder; params.y = extra softness).
// Mode 2: a rounded rectangle (uv = offset from its centre in pixels,
// params = half width, half height, corner radius, feather).
layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in uint v_mode;
layout(location = 3) in vec4 v_params;
layout(location = 0) out vec4 out_color;

void main() {
    if (v_mode == 1u) {
        float d = texture(atlas, v_uv).a;
        float w = max(fwidth(d) * 0.7, 1e-4) + v_params.y;
        float a = smoothstep(v_params.x - w, v_params.x + w, d);
        out_color = vec4(v_color.rgb, v_color.a * a);
    } else if (v_mode == 2u) {
        vec2 q = abs(v_uv) - v_params.xy + v_params.z;
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_params.z;
        float f = max(v_params.w, fwidth(dist));
        float a = 1.0 - smoothstep(-f * 0.5, f * 0.5, dist);
        out_color = vec4(v_color.rgb, v_color.a * a);
    } else if (v_mode == 4u) { // an icon: its own colours
        vec4 t = texture(atlas, v_uv);
        out_color = t * v_color;
    } else if (v_mode == 3u) { // vignette: darker towards the corners
        float r = length(v_uv / v_params.xy);
        out_color = vec4(v_color.rgb, v_color.a * smoothstep(0.55, 1.35, r) * v_params.z);
    } else {
        out_color = v_color;
    }
}
