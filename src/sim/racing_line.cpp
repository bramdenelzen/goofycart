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
    case Surface::Oil:
    case Surface::Water:
    case Surface::BoostPad:
    case Surface::GlideRamp:
    case Surface::TrickRamp: return true;
    default: return false;
    }
}

bool on_obstacle(const Track& t, vec3 p) {
    for (const Track::Obstacle& o : t.obstacles) {
        const vec3 d = p - o.pos;
        if (std::abs(glm::dot(d, o.right)) < o.half.x + 0.3f && std::abs(glm::dot(d, o.forward)) < o.half.z + 0.3f &&
            std::abs(glm::dot(d, o.up)) < 2.0f * o.half.y + 4.0f)
            return true;
    }
    return false;
}

// How far the road reaches from the centre towards `dir` (+1 right, -1 left),
// looking past obstacles standing on it.
float reach(const Track& t, const CenterSample& c, float dir) {
    constexpr float kStep = 0.5f;
    float last = 0.0f;
    for (float o = 0.0f; o <= c.half_width + 4.0f; o += kStep) {
        const vec3 p = c.pos + c.right * (o * dir);
        if (on_obstacle(t, p)) {
            last = o;
            continue;
        }
        const auto hit = t.raycast(p + c.up * 3.0f, -c.up, 6.0f);
        if (!hit || !drivable(hit->surface)) break;
        last = o;
    }
    return last;
}

// Whether a wall stands beyond the drivable road on this side; without one
// the line keeps further from the edge.
bool walled(const Track& t, const CenterSample& c, float dir, float road_reach) {
    const vec3 from = c.pos + c.right * (road_reach * dir) + c.up * 0.8f;
    const auto hit = t.raycast(from, c.right * dir, 14.0f);
    return hit && hit->surface == Surface::Wall;
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
        const float rr = reach(t, c, 1.0f), rl = reach(t, c, -1.0f);
        constexpr float kOpenMargin = 2.0f; // extra room from an edge you can fall off
        const float road_hi = rr - kMargin - (walled(t, c, 1.0f, rr) ? 0.0f : kOpenMargin);
        const float road_lo = -(rl - kMargin - (walled(t, c, -1.0f, rl) ? 0.0f : kOpenMargin));
        lp.hi = std::clamp(road_hi, 0.0f, taper);
        lp.lo = -std::clamp(-road_lo, 0.0f, taper);
        // Obstacles split the road; keep to the widest part that's left. They
        // block from a little before (karts arrive at an angle) to just after.
        std::vector<std::pair<float, float>> parts{{road_lo, road_hi}};
        for (const Track::Obstacle& o : t.obstacles) {
            const float along = glm::dot(c.pos - o.pos, o.forward);
            if (along < -o.half.z - 6.0f || along > o.half.z + 2.0f || std::abs(glm::dot(c.pos - o.pos, o.up)) > 6.0f) continue;
            const float across = glm::dot(o.pos - c.pos, c.right);
            const float b0 = across - o.half.x - kMargin - 0.4f, b1 = across + o.half.x + kMargin + 0.4f;
            std::vector<std::pair<float, float>> cut;
            for (const auto& [a, b] : parts) {
                if (b0 > a) cut.push_back({a, std::min(b, b0)});
                if (b1 < b) cut.push_back({std::max(a, b1), b});
            }
            parts = std::move(cut);
        }
        if (parts.size() != 1 || parts[0] != std::pair(road_lo, road_hi)) {
            // The widest part, or along a run of blocked samples the part
            // that carries on from the last one, so the line can't swap sides.
            const Track::LinePoint* prev = i > 0 && t.line[size_t(i - 1)].blocked ? &t.line[size_t(i - 1)] : nullptr;
            auto score = [&](const std::pair<float, float>& part) {
                if (!prev) return part.second - part.first;
                return std::min(part.second, prev->hi) - std::max(part.first, prev->lo);
            };
            std::pair<float, float> best{0.0f, 0.0f};
            float best_score = -1e9f;
            for (const auto& part : parts)
                if (score(part) > best_score) best = part, best_score = score(part);
            lp.lo = std::max(best.first, -taper);
            lp.hi = std::min(best.second, taper);
            if (lp.lo > lp.hi) lp.lo = lp.hi = 0.5f * (lp.lo + lp.hi);
            lp.blocked = true;
        }
        // Run over boost pads.
        float pad_lo, pad_hi;
        if (pad_span(t, c, pad_lo, pad_hi) && pad_hi - pad_lo > 1.5f) { // skip slivers at a pad's ends
            const float mid = 0.5f * (pad_lo + pad_hi), half = std::max(0.0f, 0.5f * (pad_hi - pad_lo) - 0.6f);
            lp.lo = std::max(lp.lo, std::min(mid - half, lp.hi));
            lp.hi = std::min(lp.hi, std::max(mid + half, lp.lo));
        }
    }

    // Relax: wide stencil first to find the overall shape, then narrow to smooth.
    // The final 1-sample pass matters: wider stencils alone leave every third
    // sample on its own chain, free to zig-zag against its neighbours.
    std::vector<float> next(static_cast<size_t>(n));
    for (int stencil : {12, 6, 3, 1}) {
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
