#pragma once
// Shared helpers for the painted Gobby Kart models: texture regions,
// colours, baked shading and a few compound shapes.
#include "gobby_assets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gk::render::gobby {

constexpr uint32_t kWhite = rgba(255, 255, 255);

// Regions of the texture: an n x n grid of cells, with a little inset so
// bilinear filtering never bleeds a neighbour in.
inline UvRect cell(int col, int row, int n = 4, float inset = 0.004f) {
    const float s = 1.0f / float(n);
    return {col * s + inset, row * s + inset, (col + 1) * s - inset, (row + 1) * s - inset};
}
inline UvRect span(int col0, int row0, int col1, int row1, int n = 4, float inset = 0.004f) {
    const float s = 1.0f / float(n);
    return {col0 * s + inset, row0 * s + inset, col1 * s - inset, row1 * s - inset};
}
inline void region(MeshBuilder& b, const UvRect& r) { b.uv_region(r.x0, r.y0, r.x1, r.y1); }

inline vec4 col(int r, int g, int bl, float a = 1.0f) { return vec4(r / 255.0f, g / 255.0f, bl / 255.0f, a); }
inline vec4 shade(vec4 c, float k) { return vec4(c.r * k, c.g * k, c.b * k, c.a); }

// Soft studio shading for a round part: light from the top, a rim at the
// bottom, a highlight up and to the left.
inline void shade_round(Canvas& t, const UvRect& r, vec4 base, float strength = 1.0f) {
    t.gradient_v(r, shade(base, 1.0f + 0.12f * strength), shade(base, 0.72f));
    t.glow(r, {0.22f, 0.28f}, 0.22f, vec4(1.0f, 1.0f, 1.0f, 0.35f * strength), 1.0f);
    t.band_v(r, 0.86f, 1.0f, vec4(0.0f, 0.0f, 0.05f, 0.18f * strength), 0.1f);
}

// Elliptical rings along a curve, for leaves, flaps and tentacles.
inline void ribbon(MeshBuilder& b, const std::vector<vec3>& path, const std::vector<vec2>& half_sizes, uint32_t color, int segments = 10) {
    std::vector<std::vector<vec3>> rings;
    vec3 prev_t = glm::normalize(path[1] - path[0]);
    vec3 u = glm::normalize(glm::cross(std::abs(prev_t.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0), prev_t));
    vec3 v = glm::cross(prev_t, u);
    for (size_t i = 0; i < path.size(); ++i) {
        vec3 t = i + 1 < path.size() ? path[i + 1] - path[i] : path[i] - path[i - 1];
        if (i > 0 && i + 1 < path.size()) t = glm::normalize(path[i + 1] - path[i]) + glm::normalize(path[i] - path[i - 1]);
        t = glm::normalize(t);
        const vec3 axis = glm::cross(prev_t, t);
        if (glm::length(axis) > 1e-6f) {
            const quat q = glm::angleAxis(std::acos(std::clamp(glm::dot(prev_t, t), -1.0f, 1.0f)), glm::normalize(axis));
            u = q * u;
            v = q * v;
        }
        prev_t = t;
        std::vector<vec3> ring;
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            ring.push_back(path[i] + u * (std::cos(th) * half_sizes[i].x) + v * (std::sin(th) * half_sizes[i].y));
        }
        rings.push_back(ring);
    }
    b.loft(rings, color, true);
}

// A two-sided flat quad with its own UVs, for leaves and fins.
inline void leaf_quad(MeshBuilder& b, vec3 a, vec3 c, vec3 d, vec3 e, uint32_t color) {
    b.quad_uv(a, c, d, e, {0, 1}, {1, 1}, {1, 0}, {0, 0}, color);
    b.quad_uv(e, d, c, a, {0, 0}, {1, 0}, {1, 1}, {0, 1}, color);
}

// A sphere pushed in and out by noise: rocks and foliage.
inline void lumpy_sphere(MeshBuilder& b, vec3 c, vec3 radii, float lump, uint32_t seed, uint32_t color, int segments = 24, int rings = 16) {
    auto h = [&](float x, float y, float z) {
        const uint32_t k = uint32_t(int(x * 3.1f + 10.0f)) * 73856093u ^ uint32_t(int(y * 3.1f + 10.0f)) * 19349663u ^ uint32_t(int(z * 3.1f + 10.0f)) * 83492791u ^ seed;
        uint32_t v = k;
        v ^= v >> 16;
        v *= 0x7feb352du;
        v ^= v >> 15;
        return float(v & 0xffff) / 65535.0f;
    };
    segments = MeshBuilder::scaled(segments, 8);
    rings = MeshBuilder::scaled(rings, 5);
    std::vector<vec3> pts;
    for (int r = 0; r <= rings; ++r) {
        const float phi = kPi * float(r) / float(rings);
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            const vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
            // Smooth-ish bumps: blend a few hashed samples around the direction.
            float bump = 0.0f;
            for (int k = 0; k < 4; ++k) bump += h(n.x * (1.3f + k), n.y * (1.3f + k), n.z * (1.3f + k));
            bump = bump / 4.0f - 0.5f;
            const float pole = std::sin(phi); // no lumps at the poles, which share one point
            pts.push_back(c + radii * n * (1.0f + lump * bump * pole));
        }
    }
    b.grid(pts, rings + 1, segments, true, color, 1.0f, 1.0f);
}

} // namespace gk::render::gobby
