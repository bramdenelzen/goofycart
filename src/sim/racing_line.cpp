// The racing line the CPU drivers follow: a smooth path that goes wide into
// corners, clips the inside and runs wide out again, within the drivable part
// of the road. Found by relaxing each point towards where its neighbours say
// a curve of least bending would put it, clamped to the road.
#include "gk/sim/track.h"

#include <algorithm>
#include <cmath>

namespace gk::sim {
namespace {

bool drivable(Surface s) {
    switch (s) {
    case Surface::Road:
    case Surface::Sand:
    case Surface::Ice:
    case Surface::Water:
    case Surface::BoostPad:
    case Surface::GlideRamp:
    case Surface::TrickRamp: return true;
    default: return false;
    }
}

// How far the road reaches from the centre towards `dir` (+1 right, -1 left).
float reach(const Track& t, const CenterSample& c, float dir) {
    constexpr float kStep = 0.5f;
    float last = 0.0f;
    for (float o = 0.0f; o <= c.half_width + 4.0f; o += kStep) {
        const vec3 p = c.pos + c.right * (o * dir);
        const auto hit = t.raycast(p + c.up * 3.0f, -c.up, 6.0f);
        if (!hit || !drivable(hit->surface)) break;
        last = o;
    }
    return last;
}

// Lateral extent of any boost pad across the road at this sample.
bool pad_span(const Track& t, const CenterSample& c, float& lo, float& hi) {
    lo = 1e9f, hi = -1e9f;
    for (float o = -c.half_width; o <= c.half_width; o += 0.5f) {
        const auto hit = t.raycast(c.pos + c.right * o + c.up * 3.0f, -c.up, 6.0f);
        if (hit && hit->surface == Surface::BoostPad) lo = std::min(lo, o), hi = std::max(hi, o);
    }
    return hi >= lo;
}

} // namespace

void build_racing_line(Track& t) {
    const int n = int(t.center.size());
    t.line.assign(size_t(n), {});
    if (n < 8) return;
    const float spacing = t.length > 0.0f ? t.length / float(n) : glm::length(t.center[1].pos - t.center[0].pos);
    t.closed = glm::length(t.center[size_t(n - 1)].pos - t.center[0].pos) < 3.0f * spacing;

    // Drivable band, keeping a kart's width off the edge. Over gaps the line
    // stays in the middle, and it narrows towards them so karts arrive straight.
    constexpr float kMargin = 1.6f;
    std::vector<int> gap_dist(size_t(n), 1 << 20);
    for (int i = 0; i < n; ++i)
        if (!t.center[size_t(i)].has_floor) gap_dist[size_t(i)] = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 2 * n; ++k) {
            const int i = pass == 0 ? k % n : (2 * n - 1 - k) % n;
            const int j = t.wrap(pass == 0 ? i - 1 : i + 1);
            gap_dist[size_t(i)] = std::min(gap_dist[size_t(i)], gap_dist[size_t(j)] + 1);
        }
    for (int i = 0; i < n; ++i) {
        const CenterSample& c = t.center[size_t(i)];
        Track::LinePoint& lp = t.line[size_t(i)];
        const float taper = float(gap_dist[size_t(i)]) * spacing * 0.08f;
        lp.hi = std::clamp(reach(t, c, 1.0f) - kMargin, 0.0f, taper);
        lp.lo = -std::clamp(reach(t, c, -1.0f) - kMargin, 0.0f, taper);
        // Run over boost pads.
        float pad_lo, pad_hi;
        if (pad_span(t, c, pad_lo, pad_hi)) {
            const float mid = 0.5f * (pad_lo + pad_hi), half = std::max(0.0f, 0.5f * (pad_hi - pad_lo) - 0.6f);
            lp.lo = std::max(lp.lo, std::min(mid - half, lp.hi));
            lp.hi = std::min(lp.hi, std::max(mid + half, lp.lo));
        }
    }

    // Relax: wide stencil first to find the overall shape, then narrow to smooth.
    std::vector<float> next(static_cast<size_t>(n));
    for (int stencil : {12, 6, 3}) {
        for (int iter = 0; iter < 300; ++iter) {
            for (int i = 0; i < n; ++i) {
                Track::LinePoint& lp = t.line[size_t(i)];
                const int a = t.wrap(i - stencil), b = t.wrap(i + stencil);
                const int a2 = t.wrap(i - 2 * stencil), b2 = t.wrap(i + 2 * stencil);
                if (!t.closed && (a2 != i - 2 * stencil || b2 != i + 2 * stencil)) { // near an open end
                    next[size_t(i)] = lp.offset;
                    continue;
                }
                // Zero fourth difference: the point a smooth (least-bending) curve
                // through the neighbours would pass.
                const vec3 mid = (4.0f * (t.line_point(a) + t.line_point(b)) - t.line_point(a2) - t.line_point(b2)) / 6.0f;
                const CenterSample& c = t.center[size_t(i)];
                const float want = glm::dot(mid - c.pos, c.right);
                next[size_t(i)] = std::clamp(glm::mix(lp.offset, want, 0.6f), lp.lo, lp.hi);
            }
            for (int i = 0; i < n; ++i) t.line[size_t(i)].offset = next[size_t(i)];
        }
    }

    // Signed curvature from the heading change across a few samples.
    constexpr int kSpan = 3;
    for (int i = 0; i < n; ++i) {
        const CenterSample& c = t.center[size_t(i)];
        vec3 d1 = t.line_point(i) - t.line_point(i - kSpan);
        vec3 d2 = t.line_point(i + kSpan) - t.line_point(i);
        d1 -= c.up * glm::dot(d1, c.up);
        d2 -= c.up * glm::dot(d2, c.up);
        const float l1 = glm::length(d1), l2 = glm::length(d2);
        if (l1 < 1e-3f || l2 < 1e-3f) continue;
        // Right turns rotate the heading towards `right`, i.e. clockwise seen from above.
        const float turn = std::atan2(-glm::dot(glm::cross(d1, d2), c.up), glm::dot(d1, d2));
        t.line[size_t(i)].curvature = turn / (0.5f * (l1 + l2));
    }
}

} // namespace gk::sim
