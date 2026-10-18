#include "paint.h"

#include <algorithm>
#include <cmath>

namespace gk::render {
namespace {

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float rnd01(uint32_t& seed) {
    seed = hash32(seed + 0x9e3779b9u);
    return float(seed & 0xffffff) / float(0x1000000);
}
float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// Value noise on a lattice, 0..1.
float vnoise(float x, float y, uint32_t seed) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = smooth01(x - float(x0)), ty = smooth01(y - float(y0));
    auto at = [&](int cx, int cy) { return float(hash32(uint32_t(cx) * 73856093u ^ uint32_t(cy) * 19349663u ^ seed) & 0xffff) / 65535.0f; };
    const float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}
float fbm(float x, float y, uint32_t seed) {
    return 0.5f * vnoise(x, y, seed) + 0.3f * vnoise(x * 2.0f, y * 2.0f, seed + 1) + 0.2f * vnoise(x * 4.0f, y * 4.0f, seed + 2);
}

} // namespace

Canvas::Canvas(int size, vec4 clear) : size_(size), rgba_(size_t(size) * size_t(size) * 4) {
    for (size_t i = 0; i < rgba_.size(); i += 4) {
        rgba_[i] = uint8_t(std::clamp(clear.r, 0.0f, 1.0f) * 255.0f);
        rgba_[i + 1] = uint8_t(std::clamp(clear.g, 0.0f, 1.0f) * 255.0f);
        rgba_[i + 2] = uint8_t(std::clamp(clear.b, 0.0f, 1.0f) * 255.0f);
        rgba_[i + 3] = uint8_t(std::clamp(clear.a, 0.0f, 1.0f) * 255.0f);
    }
}

void Canvas::blend_px(int x, int y, vec4 c) {
    if (x < 0 || y < 0 || x >= size_ || y >= size_) return;
    const float a = std::clamp(c.a, 0.0f, 1.0f);
    if (a <= 0.0f) return;
    uint8_t* p = &rgba_[(size_t(y) * size_t(size_) + size_t(x)) * 4];
    for (int k = 0; k < 3; ++k) {
        const float src = std::clamp(c[k], 0.0f, 1.0f);
        p[k] = uint8_t(std::clamp((float(p[k]) / 255.0f) * (1.0f - a) + src * a, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    // Alpha accumulates (painting opaque over transparent makes it opaque).
    const float dst_a = float(p[3]) / 255.0f;
    p[3] = uint8_t(std::clamp(dst_a + a * (1.0f - dst_a), 0.0f, 1.0f) * 255.0f + 0.5f);
}

vec4 Canvas::get(const UvRect& r, float u, float v) const {
    const vec2 uv = r.at(u, v);
    const int x = std::clamp(int(uv.x * float(size_)), 0, size_ - 1), y = std::clamp(int(uv.y * float(size_)), 0, size_ - 1);
    const uint8_t* p = &rgba_[(size_t(y) * size_t(size_) + size_t(x)) * 4];
    return vec4(p[0], p[1], p[2], p[3]) / 255.0f;
}

template <class F> void Canvas::paint(const UvRect& r, F&& f) {
    const int px0 = std::clamp(int(std::floor(r.x0 * float(size_))), 0, size_), px1 = std::clamp(int(std::ceil(r.x1 * float(size_))), 0, size_);
    const int py0 = std::clamp(int(std::floor(r.y0 * float(size_))), 0, size_), py1 = std::clamp(int(std::ceil(r.y1 * float(size_))), 0, size_);
    const float inv = 1.0f / float(size_);
    for (int y = py0; y < py1; ++y)
        for (int x = px0; x < px1; ++x) {
            const float u = ((float(x) + 0.5f) * inv - r.x0) / std::max(r.w(), 1e-6f);
            const float v = ((float(y) + 0.5f) * inv - r.y0) / std::max(r.h(), 1e-6f);
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
            blend_px(x, y, f(u, v));
        }
}

void Canvas::custom(const UvRect& r, const std::function<vec4(float, float)>& f) {
    paint(r, [&](float u, float v) { return f(u, v); });
}

void Canvas::fill(const UvRect& r, vec4 color) {
    paint(r, [&](float, float) { return color; });
}

void Canvas::gradient_v(const UvRect& r, vec4 top, vec4 bottom) {
    paint(r, [&](float, float v) { return glm::mix(top, bottom, v); });
}

void Canvas::gradient_u(const UvRect& r, vec4 left, vec4 right) {
    paint(r, [&](float u, float) { return glm::mix(left, right, u); });
}

void Canvas::glow(const UvRect& r, vec2 c, float radius, vec4 color, float softness) {
    const float aspect = r.w() / std::max(r.h(), 1e-6f);
    paint(r, [&](float u, float v) {
        // Measured in rect-u units so the glow is round on a square rect.
        const float dx = (u - c.x), dy = (v - c.y) / aspect;
        const float d = std::sqrt(dx * dx + dy * dy) / std::max(radius, 1e-6f);
        const float a = 1.0f - smooth01(softness > 0.0f ? (d - (1.0f - softness)) / softness : d);
        return vec4(color.r, color.g, color.b, color.a * (d < 1.0f ? a : 0.0f));
    });
}

void Canvas::vignette(const UvRect& r, float strength, float width) {
    paint(r, [&](float u, float v) {
        const float e = std::min({u, 1.0f - u, v, 1.0f - v});
        const float a = (1.0f - smooth01(e / std::max(width, 1e-6f))) * strength;
        return vec4(0.0f, 0.0f, 0.0f, a);
    });
}

void Canvas::band_v(const UvRect& r, float v0, float v1, vec4 color, float soft) {
    paint(r, [&](float, float v) {
        float a = 1.0f;
        if (soft > 0.0f) a = smooth01((v - v0) / soft) * smooth01((v1 - v) / soft);
        else a = (v >= v0 && v < v1) ? 1.0f : 0.0f;
        return vec4(color.r, color.g, color.b, color.a * a);
    });
}

void Canvas::band_u(const UvRect& r, float u0, float u1, vec4 color, float soft) {
    paint(r, [&](float u, float) {
        float a = 1.0f;
        if (soft > 0.0f) a = smooth01((u - u0) / soft) * smooth01((u1 - u) / soft);
        else a = (u >= u0 && u < u1) ? 1.0f : 0.0f;
        return vec4(color.r, color.g, color.b, color.a * a);
    });
}

void Canvas::spots(const UvRect& r, int n, float radius, vec4 color, uint32_t seed, float soft) {
    uint32_t s = seed;
    for (int i = 0; i < n; ++i) {
        const vec2 c(rnd01(s), rnd01(s));
        const float rr = radius * (0.7f + 0.6f * rnd01(s));
        glow(r, c, rr, color, soft);
        // Wrap around u so spots cross the seam of a lathe cleanly.
        if (c.x < rr) glow(r, c + vec2(1.0f, 0.0f), rr, color, soft);
        if (c.x > 1.0f - rr) glow(r, c - vec2(1.0f, 0.0f), rr, color, soft);
    }
}

void Canvas::stripes_u(const UvRect& r, int count, vec4 a, vec4 b, float slant) {
    paint(r, [&](float u, float v) {
        const int i = int(std::floor((u + slant * v) * float(count) + 100.0f));
        return (i & 1) ? b : a;
    });
}

void Canvas::checker(const UvRect& r, int cells, vec4 a, vec4 b) {
    paint(r, [&](float u, float v) {
        const int i = int(u * float(cells)) + int(v * float(cells));
        return (i & 1) ? b : a;
    });
}

void Canvas::ellipse(const UvRect& r, vec2 c, vec2 radius, vec4 color, float soft) {
    paint(r, [&](float u, float v) {
        const float dx = (u - c.x) / std::max(radius.x, 1e-6f), dy = (v - c.y) / std::max(radius.y, 1e-6f);
        const float d = std::sqrt(dx * dx + dy * dy);
        const float a = 1.0f - smooth01((d - 1.0f + soft) / std::max(soft, 1e-6f));
        return vec4(color.r, color.g, color.b, color.a * a);
    });
}

void Canvas::rounded(const UvRect& r, UvRect inner, float corner, vec4 color) {
    paint(r, [&](float u, float v) {
        const float cx = std::clamp(u, inner.x0 + corner, inner.x1 - corner), cy = std::clamp(v, inner.y0 + corner, inner.y1 - corner);
        const float d = std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy));
        const float a = 1.0f - smooth01((d - corner) / 0.006f + 0.5f);
        return vec4(color.r, color.g, color.b, color.a * a);
    });
}

void Canvas::eye(const UvRect& r, vec2 c, vec2 radius, vec4 iris, float lid) {
    ellipse(r, c + vec2(0.0f, radius.y * 0.08f), radius * 1.06f, vec4(0.05f, 0.05f, 0.08f, 1.0f), 0.05f); // outline
    ellipse(r, c, radius, vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.04f);
    ellipse(r, c + vec2(0.0f, radius.y * 0.15f), radius * vec2(0.6f, 0.62f), iris, 0.05f);
    ellipse(r, c + vec2(0.0f, radius.y * 0.2f), radius * vec2(0.34f, 0.38f), vec4(0.05f, 0.05f, 0.08f, 1.0f), 0.05f);
    ellipse(r, c + vec2(-radius.x * 0.22f, -radius.y * 0.12f), radius * vec2(0.16f, 0.16f), vec4(1.0f, 1.0f, 1.0f, 0.95f), 0.1f);
    if (lid > 0.0f) {
        // An upper lid cutting the eye flat: a dark slab over the top part.
        paint(r, [&](float u, float v) {
            const float dx = (u - c.x) / std::max(radius.x, 1e-6f), dy = (v - c.y) / std::max(radius.y, 1e-6f);
            const bool inside = dx * dx + dy * dy < 1.2f;
            const float a = inside && dy < -1.0f + 2.0f * lid ? 1.0f : 0.0f;
            return vec4(0.05f, 0.05f, 0.08f, a);
        });
    }
}

void Canvas::arc(const UvRect& r, vec2 c, float radius, float a0, float a1, float thickness, vec4 color) {
    paint(r, [&](float u, float v) {
        const float dx = u - c.x, dy = v - c.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        float ang = std::atan2(dy, dx);
        if (ang < a0) ang += 2.0f * kPi;
        const bool in_arc = ang >= a0 && ang <= a1;
        const float a = in_arc ? 1.0f - smooth01((std::abs(d - radius) - thickness * 0.5f) / 0.008f) : 0.0f;
        return vec4(color.r, color.g, color.b, color.a * a);
    });
}

void Canvas::mouth(const UvRect& r, vec2 c, float width, float curve, float thickness, vec4 color) {
    paint(r, [&](float u, float v) {
        const float x = (u - c.x) / std::max(width * 0.5f, 1e-6f);
        if (std::abs(x) > 1.0f) return vec4(0.0f);
        const float y = c.y + curve * (x * x - 0.5f);
        const float a = 1.0f - smooth01((std::abs(v - y) - thickness * 0.5f) / 0.008f);
        const float taper = 1.0f - smooth01((std::abs(x) - 0.7f) / 0.3f);
        return vec4(color.r, color.g, color.b, color.a * a * (0.3f + 0.7f * taper));
    });
}

void Canvas::noise(const UvRect& r, float amount, float scale, uint32_t seed, vec4 tint) {
    paint(r, [&](float u, float v) {
        const float n = fbm(u * scale, v * scale, seed) - 0.5f;
        return vec4(tint.r * (0.5f + n), tint.g * (0.5f + n), tint.b * (0.5f + n), amount * tint.a);
    });
}

void Canvas::jagged(const UvRect& r, vec2 a, vec2 b, float thickness, vec4 color, uint32_t seed, float wobble) {
    const int segs = 7;
    std::vector<vec2> pts;
    uint32_t s = seed;
    for (int i = 0; i <= segs; ++i) {
        const float t = float(i) / float(segs);
        vec2 p = glm::mix(a, b, t);
        if (i > 0 && i < segs) {
            const vec2 d = glm::normalize(b - a), n(-d.y, d.x);
            p += n * (rnd01(s) - 0.5f) * 2.0f * wobble;
        }
        pts.push_back(p);
    }
    paint(r, [&](float u, float v) {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const vec2 p = pts[i], q = pts[i + 1], x(u, v);
            const vec2 pq = q - p;
            const float t = std::clamp(glm::dot(x - p, pq) / std::max(glm::dot(pq, pq), 1e-9f), 0.0f, 1.0f);
            best = std::min(best, glm::length(x - (p + pq * t)));
        }
        const float al = 1.0f - smooth01((best - thickness * 0.5f) / 0.01f);
        return vec4(color.r, color.g, color.b, color.a * al);
    });
}

void Canvas::bricks(const UvRect& r, int cols, int rows, vec4 mortar, vec4 brick, uint32_t seed) {
    paint(r, [&](float u, float v) {
        const int row = int(v * float(rows));
        const float shifted = u * float(cols) + ((row & 1) ? 0.5f : 0.0f);
        const float fu = shifted - std::floor(shifted), fv = v * float(rows) - float(row);
        const float m = 0.08f;
        if (fu < m || fv < m) return mortar;
        uint32_t h = hash32(uint32_t(int(std::floor(shifted))) * 31u + uint32_t(row) * 17u + seed);
        const float tone = 0.85f + 0.3f * float(h & 0xff) / 255.0f;
        return vec4(brick.r * tone, brick.g * tone, brick.b * tone, brick.a);
    });
}

void Canvas::windows(const UvRect& r, int cols, int rows, vec4 frame, vec4 lit, vec4 dark, uint32_t seed, float lit_chance) {
    paint(r, [&](float u, float v) {
        const int col = int(u * float(cols)), row = int(v * float(rows));
        const float fu = u * float(cols) - float(col), fv = v * float(rows) - float(row);
        if (fu < 0.25f || fu > 0.8f || fv < 0.2f || fv > 0.75f) return frame;
        const uint32_t h = hash32(uint32_t(col) * 7919u + uint32_t(row) * 104729u + seed);
        const bool on = float(h & 0xffff) / 65535.0f < lit_chance;
        const float glint = 1.0f - 0.25f * fv;
        return on ? vec4(lit.r * glint, lit.g * glint, lit.b * glint, 1.0f) : dark;
    });
}

void Canvas::question_mark(const UvRect& r, vec2 c, float height, float thickness, vec4 color) {
    // A hook from the left, over the top and round to the bottom of its
    // circle; a stem straight down from there; a dot below.
    const float rad = height * 0.25f;
    const vec2 hook = c + vec2(0.0f, -height * 0.2f);
    arc(r, hook, rad, -kPi, kPi * 0.5f, thickness, color);
    const float stem_top = hook.y + rad, stem_bottom = c.y + height * 0.2f;
    rounded(r, {c.x - thickness * 0.5f, stem_top - thickness * 0.1f, c.x + thickness * 0.5f, stem_bottom}, thickness * 0.15f, color);
    rounded(r, {c.x - thickness * 0.55f, c.y + height * 0.3f, c.x + thickness * 0.55f, c.y + height * 0.3f + thickness * 1.1f}, thickness * 0.3f, color);
}

void Canvas::star(const UvRect& r, vec2 c, float radius, vec4 color) {
    paint(r, [&](float u, float v) {
        const float dx = u - c.x, dy = v - c.y;
        const float ang = std::atan2(dy, dx), d = std::sqrt(dx * dx + dy * dy);
        // Five-point star: radius varies with angle.
        const float k = std::cos(5.0f * (ang + kPi * 0.5f));
        const float rr = radius * (0.55f + 0.45f * (k > 0 ? std::pow(k, 0.5f) : 0.0f));
        const float a = 1.0f - smooth01((d - rr) / 0.01f);
        return vec4(color.r, color.g, color.b, color.a * a);
    });
}

} // namespace gk::render
