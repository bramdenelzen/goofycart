// Builds track geometry from a TrackDef: a closed Catmull-Rom spline through
// the control points, resampled every ~2 m, with a road of four lanes,
// verges, walls, ramps, boost pads, glide gaps and item box rows; then each
// side road the same way along its own open spline, joined to the main road
// by a shared stretch and a grass island narrowing to a walled nose.
#include "gk/sim/track_def.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace gk::sim {
namespace {

vec4 catmull_rom(const vec4& p0, const vec4& p1, const vec4& p2, const vec4& p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// A stretch of a road, [a, b) in metres along it; on the loop it may wrap.
struct Range {
    float a = 0.0f, b = 0.0f;
    float total = 1.0f;
    bool contains(float s) const {
        if (b - a >= total) return true;
        const float sa = std::fmod(std::fmod(a, total) + total, total);
        const float sb = std::fmod(std::fmod(b, total) + total, total);
        s = std::fmod(std::fmod(s, total) + total, total);
        return sa <= sb ? (s >= sa && s < sb) : (s >= sa || s < sb);
    }
    // 0..1 progress through the range (for ramps).
    float progress(float s) const {
        float d = std::fmod(std::fmod(s - a, total) + total, total);
        return d / std::max(b - a, 1e-3f);
    }
};

bool in_any(const std::vector<Range>& rs, float s) {
    return std::any_of(rs.begin(), rs.end(), [&](const Range& r) { return r.contains(s); });
}

constexpr int kCols = 7; // lateral vertices per sample: wall, verge, 4 lanes, verge, wall
constexpr int kStride = kCols + 2;
constexpr float kWallHeight = 1.5f;
constexpr float kOpenRoad = 1e7f; // the "loop length" of an open road: nothing wraps

// One road being built: the main loop or a branch, its samples and its
// features resolved to metres along it.
struct Strip {
    std::vector<vec4> base;   // resampled spline, w = half-width
    float step = 2.0f, total = 0.0f;
    bool closed = true;
    int branch = -1;
    uint8_t group = 0;
    float verge = 5.0f;
    Surface verge_surface = Surface::OffroadLight, surface = Surface::Road;
    std::vector<CenterSample> c;
    uint32_t first_vertex = 0;
    int first = 0; // in Track::road

    std::vector<Range> trick_ramps, glide_ramps, gaps, no_respawn, no_walls_l, no_walls_r;
    std::vector<Range> open_l, open_r; // joined to another road here: no verge, no wall
    std::vector<float> trick_heights, glide_heights;
    std::vector<std::pair<Range, int>> pads;
    std::vector<std::pair<Range, Surface>> road_surfaces;
    struct VergeSpan { Range r; bool left, right; float width; Surface surface; };
    std::vector<VergeSpan> verges;
    std::vector<float> box_rows;
    std::vector<std::pair<float, const TrackFeature*>> obstacles, movers, barriers, switches;
    std::vector<std::pair<Range, const TrackFeature*>> zones, tunnels;

    int n() const { return int(c.size()); }
    Range range(float a, float b) const { return Range{a, b, closed ? total : kOpenRoad}; }
    int sample_at(float s) const {
        const int count = int(base.size());
        if (closed) {
            s = std::fmod(std::fmod(s, total) + total, total);
            return std::min(count - 1, int(std::round(s / step))) % count;
        }
        return std::clamp(int(std::round(s / step)), 0, count - 1);
    }
    int wrap(int i) const { return closed ? ((i % n()) + n()) % n() : std::clamp(i, 0, n() - 1); }

    void add(const TrackFeature& f, float a, float b) {
        switch (f.kind) {
        case TrackFeature::Kind::TrickRamp:
            trick_ramps.push_back(range(a, a + f.length));
            trick_heights.push_back(f.height);
            break;
        case TrackFeature::Kind::GlideRamp: {
            const Range ramp = range(a, a + f.length);
            glide_ramps.push_back(ramp);
            glide_heights.push_back(f.height);
            const Range gap = range(ramp.b, ramp.b + f.gap);
            gaps.push_back(gap);
            no_respawn.push_back(range(ramp.a - 40.0f, gap.b + 4.0f));
            no_walls_l.push_back(range(ramp.a, gap.b + 4.0f));
            no_walls_r.push_back(range(ramp.a, gap.b + 4.0f));
            break;
        }
        case TrackFeature::Kind::BoostPad: pads.push_back({range(a, a + f.length), f.lanes}); break;
        case TrackFeature::Kind::Road: road_surfaces.push_back({range(a, b), f.surface}); break;
        case TrackFeature::Kind::Verge: verges.push_back({range(a, b), f.left, f.right, f.width, f.surface}); break;
        case TrackFeature::Kind::ItemBoxes: box_rows.push_back(a); break;
        case TrackFeature::Kind::Obstacle:
            obstacles.push_back({a, &f});
            if (std::abs(f.offset) < 0.5f * f.width + 1.0f) no_respawn.push_back(range(a - 8.0f, a + 8.0f)); // covers the centre
            break;
        case TrackFeature::Kind::Stairs: break; // main loop only, handled by build_track
        case TrackFeature::Kind::Mover: movers.push_back({a, &f}); break;
        case TrackFeature::Kind::NoWalls:
            if (f.left) no_walls_l.push_back(range(a, b));
            if (f.right) no_walls_r.push_back(range(a, b));
            break;
        case TrackFeature::Kind::Barrier:
            barriers.push_back({a, &f});
            no_respawn.push_back(range(a - 8.0f, a + 8.0f));
            break;
        case TrackFeature::Kind::Switch: switches.push_back({a, &f}); break;
        case TrackFeature::Kind::Zone: zones.push_back({range(a, b), &f}); break;
        case TrackFeature::Kind::Tunnel:
            tunnels.push_back({range(a, b), &f});
            no_respawn.push_back(range(a - 2.0f, b + 2.0f)); // the roof is no place to drop a kart onto
            break;
        }
    }

    float height_offset(float s) const {
        float h = 0.0f;
        for (size_t i = 0; i < trick_ramps.size(); ++i)
            if (trick_ramps[i].contains(s)) h += trick_heights[i] * smooth(trick_ramps[i].progress(s));
        for (size_t i = 0; i < glide_ramps.size(); ++i)
            if (glide_ramps[i].contains(s)) h += glide_heights[i] * smooth(glide_ramps[i].progress(s));
        return h;
    }
    Surface road_surface(float s, int lane) const {
        if (in_any(trick_ramps, s)) return Surface::TrickRamp;
        if (in_any(glide_ramps, s)) return Surface::GlideRamp;
        for (const auto& [r, lanes] : pads)
            if (r.contains(s) && (lanes >> lane & 1)) return Surface::BoostPad;
        for (const auto& [r, surf] : road_surfaces)
            if (r.contains(s)) return surf;
        return surface;
    }
    std::pair<float, Surface> verge_at(float s, bool right) const {
        if (in_any(right ? open_r : open_l, s)) return {0.0f, verge_surface};
        for (const VergeSpan& v : verges)
            if (v.r.contains(s) && (right ? v.right : v.left)) return {v.width, v.surface};
        return {verge, verge_surface};
    }
    bool in_tunnel(float s) const {
        return std::any_of(tunnels.begin(), tunnels.end(), [&](const auto& t) { return t.first.contains(s); });
    }
    bool walled(float s, bool right) const {
        if (in_any(right ? open_r : open_l, s)) return false;
        return in_tunnel(s) || !in_any(right ? no_walls_r : no_walls_l, s); // a tunnel always has its walls
    }

    // Centre samples from the resampled spline: frames, widths, floors.
    // `s_of_u` maps distance along this road to distance along the race.
    template <class F>
    void make_samples(F s_of_u) {
        const int count = int(base.size());
        c.resize(size_t(count));
        for (int i = 0; i < count; ++i) {
            CenterSample& cs = c[size_t(i)];
            cs.u = float(i) * step;
            cs.s = s_of_u(cs.u);
            cs.pos = vec3(base[size_t(i)]) + vec3(0, height_offset(cs.u), 0);
            cs.branch = branch;
        }
        for (int i = 0; i < count; ++i) {
            CenterSample& cs = c[size_t(i)];
            const int a = closed ? (i - 1 + count) % count : std::max(i - 1, 0);
            const int b = closed ? (i + 1) % count : std::min(i + 1, count - 1);
            cs.tangent = glm::normalize(vec3(base[size_t(b)]) - vec3(base[size_t(a)]));
            cs.right = glm::normalize(glm::cross(cs.tangent, kUp));
            cs.up = glm::normalize(glm::cross(cs.right, cs.tangent));
            cs.half_width = base[size_t(i)].w;
            cs.has_floor = !in_any(gaps, cs.u);
            cs.respawn_ok = !in_any(no_respawn, cs.u);
        }
    }
    uint32_t V(int sample, int col) const { return first_vertex + uint32_t(wrap(sample) * kStride + col); }
};

// Centripetal Catmull-Rom: no cusps or loops however unevenly the points
// are spaced (the shared stretches are dense, the rest of a side road isn't).
vec4 catmull_rom_centripetal(const vec4& p0, const vec4& p1, const vec4& p2, const vec4& p3, float t) {
    auto knot = [](const vec4& a, const vec4& b) { return std::max(std::sqrt(glm::length(vec3(b) - vec3(a))), 1e-3f); };
    const float t0 = 0.0f, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
    const float u = glm::mix(t1, t2, t);
    const vec4 a1 = (t1 - u) / (t1 - t0) * p0 + (u - t0) / (t1 - t0) * p1;
    const vec4 a2 = (t2 - u) / (t2 - t1) * p1 + (u - t1) / (t2 - t1) * p2;
    const vec4 a3 = (t3 - u) / (t3 - t2) * p2 + (u - t2) / (t3 - t2) * p3;
    const vec4 b1 = (t2 - u) / (t2 - t0) * a1 + (u - t0) / (t2 - t0) * a2;
    const vec4 b2 = (t3 - u) / (t3 - t1) * a2 + (u - t1) / (t3 - t1) * a3;
    return (t2 - u) / (t2 - t1) * b1 + (u - t1) / (t2 - t1) * b2;
}

// Spline through `pts` (open), resampled every ~2 m.
std::vector<vec4> resample_open(const std::vector<vec4>& pts, float& step, float& total) {
    const int nc = int(pts.size());
    std::vector<vec4> dense;
    for (int i = 0; i + 1 < nc; ++i) {
        const vec4 p0 = i > 0 ? pts[size_t(i - 1)] : 2.0f * pts[0] - pts[1];
        const vec4 p3 = i + 2 < nc ? pts[size_t(i + 2)] : 2.0f * pts[size_t(nc - 1)] - pts[size_t(nc - 2)];
        for (int k = 0; k < 64; ++k) dense.push_back(catmull_rom_centripetal(p0, pts[size_t(i)], pts[size_t(i + 1)], p3, float(k) / 64.0f));
    }
    dense.push_back(pts.back());
    std::vector<float> ds(dense.size(), 0.0f);
    for (size_t i = 1; i < dense.size(); ++i) ds[i] = ds[i - 1] + glm::length(vec3(dense[i]) - vec3(dense[i - 1]));
    total = ds.back();
    const int n = std::max(2, int(std::round(total / 2.0f)) + 1);
    step = total / float(n - 1);
    std::vector<vec4> out(static_cast<size_t>(n));
    size_t di = 0;
    for (int i = 0; i < n; ++i) {
        const float s = std::min(float(i) * step, total);
        while (di + 2 < dense.size() && ds[di + 1] < s) ++di;
        const float seg = ds[di + 1] - ds[di];
        out[size_t(i)] = glm::mix(dense[di], dense[di + 1], seg > 0.0f ? std::clamp((s - ds[di]) / seg, 0.0f, 1.0f) : 0.0f);
    }
    return out;
}

} // namespace

Track build_track(const TrackDef& def) {
    Track t;
    t.id = def.id;
    t.name = def.name;
    t.theme = def.theme;
    t.ground = def.ground;
    t.laps = def.laps;
    std::vector<vec4> ctrl = def.points;
    for (auto& c : ctrl)
        if (c.w <= 0.0f) c.w = def.half_width;
    const int nc = int(ctrl.size());
    if (nc < 4) return t;

    // ---- the main loop: dense sampling of the spline, then resampling by arc length
    std::vector<vec4> dense;
    std::vector<float> dense_ctrl; // fractional control index of each dense point
    for (int i = 0; i < nc; ++i) {
        const vec4& p0 = ctrl[size_t((i - 1 + nc) % nc)];
        const vec4& p1 = ctrl[size_t(i)];
        const vec4& p2 = ctrl[size_t((i + 1) % nc)];
        const vec4& p3 = ctrl[size_t((i + 2) % nc)];
        for (int k = 0; k < 64; ++k) {
            dense.push_back(catmull_rom(p0, p1, p2, p3, float(k) / 64.0f));
            dense_ctrl.push_back(float(i) + float(k) / 64.0f);
        }
    }
    auto xyz = [](const vec4& v) { return vec3(v); };
    std::vector<float> dense_s(dense.size() + 1, 0.0f);
    for (size_t i = 1; i <= dense.size(); ++i)
        dense_s[i] = dense_s[i - 1] + glm::length(xyz(dense[i % dense.size()]) - xyz(dense[i - 1]));
    const float total = dense_s.back();
    constexpr float kSpacing = 2.0f;
    const int n = int(std::round(total / kSpacing));
    const float step = total / float(n);

    Strip main;
    main.base.resize(size_t(n));
    main.step = step, main.total = total, main.closed = true;
    main.verge = def.verge, main.verge_surface = def.verge_surface;
    std::vector<float> ctrl_at(static_cast<size_t>(n));
    size_t di = 0;
    for (int i = 0; i < n; ++i) {
        const float s = float(i) * step;
        while (di + 1 < dense.size() && dense_s[di + 1] < s) ++di;
        const float seg = dense_s[di + 1] - dense_s[di];
        const float f = seg > 0 ? (s - dense_s[di]) / seg : 0.0f;
        main.base[size_t(i)] = glm::mix(dense[di], dense[(di + 1) % dense.size()], f);
        ctrl_at[size_t(i)] = dense_ctrl[di];
    }
    auto s_of = [&](TrackPos p) {
        float s = total;
        for (int i = 0; i < n; ++i)
            if (ctrl_at[size_t(i)] >= p.ctrl) {
                s = float(i) * step;
                break;
            }
        return s + p.metres;
    };
    for (const TrackFeature& f : def.features) {
        if (f.kind == TrackFeature::Kind::Stairs) t.stairs.push_back({s_of(f.at), s_of(f.to)});
        else main.add(f, s_of(f.at), s_of(f.to));
    }
    main.make_samples([](float u) { return u; });
    t.center = main.c;
    t.length = total;

    for (const GateDef& g : def.gates) t.gates.push_back(g.gate);
    auto inverted = [&](int g) {
        Gate inv = t.gates[size_t(g)];
        inv.invert = !inv.invert;
        t.gates.push_back(inv);
        return int(t.gates.size()) - 1;
    };

    // ---- branches: open splines from beside the main road to beside it again
    std::vector<Strip> strips;
    struct Join { // one end of a branch, where it meets the main road
        std::vector<int> main_edge, branch_edge; // samples along the island's two sides, walking away from the end
        bool nose = false;                       // ends in a walled nose (else the island runs into the other end)
        int dir = 1;                             // +1 the fork's (walking forward), -1 the merge's
    };
    std::vector<std::pair<Join, Join>> joins;
    std::vector<std::vector<Range>> opened;      // per branch: where the main road is open towards it
    for (size_t bi = 0; bi < def.branches.size(); ++bi) {
        const BranchDef& bd = def.branches[bi];
        Track::Branch tb;
        tb.name = bd.name;
        tb.side = bd.side < 0 ? -1 : 1;
        tb.gate = bd.gate;
        tb.vanish = bd.vanish && bd.gate >= 0;
        tb.ai_share = bd.ai_share;
        const int side = tb.side;
        const int fi = main.sample_at(s_of(bd.from)), mi = main.sample_at(s_of(bd.to));
        tb.fork = fi, tb.merge = mi;
        const int lm = std::max(2, int(std::round(bd.junction / step)));
        const float bhw = bd.half_width > 0.0f ? bd.half_width : def.half_width;
        auto beside = [&](int i, float w) {
            const CenterSample& c = main.c[size_t(main.wrap(i))];
            return vec4(c.pos + c.right * (float(side) * (c.half_width + w)), w);
        };
        std::vector<vec4> pts;
        const float w0 = !bd.points.empty() && bd.points.front().w > 0.0f ? bd.points.front().w : bhw;
        const float w1 = !bd.points.empty() && bd.points.back().w > 0.0f ? bd.points.back().w : bhw;
        // The shared stretches follow the main road sample by sample, so the
        // two edges meet even on a bend, and the side road opens out of the
        // main one like an exit lane: from a sliver to its full width.
        constexpr int kEvery = 3;
        constexpr float kSliver = 0.6f;
        auto lane = [&](int k, float w) { return glm::mix(kSliver, w, smooth(float(k) / float(lm))); };
        for (int k = 0; k <= lm; k += kEvery) pts.push_back(beside(fi + k, lane(k, w0)));
        if (lm % kEvery) pts.push_back(beside(fi + lm, w0));
        for (vec4 p : bd.points) pts.push_back(vec4(vec3(p), p.w > 0.0f ? p.w : bhw));
        if (lm % kEvery) pts.push_back(beside(mi - lm, w1));
        for (int k = lm - lm % kEvery; k >= 0; k -= kEvery) pts.push_back(beside(mi - k, lane(k, w1)));

        Strip st;
        st.closed = false;
        st.branch = int(bi);
        st.group = tb.vanish ? uint8_t(bi + 1) : 0;
        st.verge = bd.verge >= 0.0f ? bd.verge : def.verge;
        st.verge_surface = bd.verge_surface != Surface::Count ? bd.verge_surface : def.verge_surface;
        st.surface = bd.surface;
        st.base = resample_open(pts, st.step, st.total);
        for (const TrackFeature& f : bd.features) st.add(f, f.at.metres, f.to.metres);
        const float fork_s = main.c[size_t(fi)].s;
        float span = main.c[size_t(mi)].s - fork_s;
        if (span <= 0.0f) span += total;
        const float len = st.total;
        st.make_samples([&](float u) { return std::fmod(fork_s + u / std::max(len, 1.0f) * span, total); });
        const int nb = st.n();
        const int lb = std::max(2, int(std::round(bd.junction / st.step)));

        // The island between the roads after the shared stretch: walk along
        // both edges while they're closer than two verges and a bit.
        const float gap_max = main.verge + st.verge + 3.0f;
        auto edge_m = [&](int i) { const CenterSample& c = main.c[size_t(main.wrap(i))]; return c.pos + c.right * (float(side) * c.half_width); };
        auto edge_b = [&](int k) { const CenterSample& c = st.c[size_t(std::clamp(k, 0, nb - 1))]; return c.pos - c.right * (float(side) * c.half_width); };
        auto walk = [&](int i, int k, int di_, int k_stop, Join& j) {
            for (int guard = 0; guard < n; ++guard, i = main.wrap(i + di_)) {
                while (k + di_ >= 0 && k + di_ < nb && (di_ > 0 ? k + di_ <= k_stop : k + di_ >= k_stop) &&
                       glm::distance(edge_b(k + di_), edge_m(i)) < glm::distance(edge_b(k), edge_m(i)))
                    k += di_;
                // (The first pair always counts: where the shared stretch ends
                // the roads touch, and a nose closes the V however fast they part.)
                if (!j.main_edge.empty() && glm::distance(edge_m(i), edge_b(k)) > gap_max) {
                    j.nose = true;
                    return;
                }
                j.main_edge.push_back(i);
                if (j.branch_edge.empty() || j.branch_edge.back() != k) j.branch_edge.push_back(k);
                if (k == k_stop) return; // reached the other end's shared stretch
            }
        };
        Join fork_join, merge_join;
        merge_join.dir = -1;
        walk(main.wrap(fi + lm), lb, 1, nb - 1 - lb, fork_join);
        if (fork_join.nose) walk(main.wrap(mi - lm), nb - 1 - lb, -1, lb, merge_join);
        // Fill in the branch-side samples the walk skipped, so the island's edges are complete.
        auto complete = [](std::vector<int>& v) {
            if (v.size() < 2) return;
            std::vector<int> out;
            const int d = v.back() > v.front() ? 1 : -1;
            for (int k = v.front();; k += d) {
                out.push_back(k);
                if (k == v.back()) break;
            }
            v = std::move(out);
        };
        complete(fork_join.branch_edge);
        complete(merge_join.branch_edge);
        // Shared stretch plus island: open on the facing sides.
        std::vector<Range> mine;
        auto open_main = [&](float a, float b) {
            float len_ = std::fmod(std::fmod(b - a, total) + total, total);
            const Range r{a, a + len_ + 0.5f * step, total};
            (side > 0 ? main.open_r : main.open_l).push_back(r);
            mine.push_back(r);
        };
        auto open_branch = [&](float a, float b) { (side > 0 ? st.open_l : st.open_r).push_back(Range{a, b, kOpenRoad}); };
        const float s_fork = main.c[size_t(fi)].s, s_merge = main.c[size_t(mi)].s;
        if (!fork_join.nose) { // one island all the way: the roads never part
            open_main(s_fork, s_merge);
            open_branch(-1.0f, len + 1.0f);
        } else {
            const int fm = fork_join.main_edge.empty() ? main.wrap(fi + lm) : fork_join.main_edge.back();
            const int fb = fork_join.branch_edge.empty() ? lb : fork_join.branch_edge.back();
            open_main(s_fork, main.c[size_t(fm)].s);
            open_branch(-1.0f, st.c[size_t(fb)].u + 0.5f * st.step);
            const int mm = merge_join.main_edge.empty() ? main.wrap(mi - lm) : merge_join.main_edge.back();
            const int mb = merge_join.branch_edge.empty() ? nb - 1 - lb : merge_join.branch_edge.back();
            open_main(main.c[size_t(mm)].s, s_merge);
            open_branch(st.c[size_t(mb)].u - 0.5f * st.step, len + 1.0f);
        }
        t.branches.push_back(tb);
        strips.push_back(std::move(st));
        joins.push_back({fork_join, merge_join});
        opened.push_back(std::move(mine));
    }
    // Switches are numbered in the order they're placed below; gates find theirs by name.
    std::vector<std::string> switch_names;
    for (const auto& sw : main.switches) switch_names.push_back(sw.second->name);
    for (const Strip& st : strips)
        for (const auto& sw : st.switches) switch_names.push_back(sw.second->name);
    for (size_t g = 0; g < def.gates.size(); ++g)
        for (size_t k = 0; k < switch_names.size() && !def.gates[g].sw.empty(); ++k)
            if (switch_names[k] == def.gates[g].sw) t.gates[g].sw = int(k);

    // ---- road samples and geometry, main loop first
    t.road = main.c;
    for (size_t b = 0; b < strips.size(); ++b) {
        strips[b].first = int(t.road.size());
        t.branches[b].first = strips[b].first;
        t.branches[b].count = strips[b].n();
        t.road.insert(t.road.end(), strips[b].c.begin(), strips[b].c.end());
    }
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, Surface s, uint8_t g) {
        t.triangles.push_back({a, b, c, s, g});
        t.triangles.push_back({a, c, d, s, g});
    };
    // A triangle wound to face up (floors) or as given (walls).
    auto floor_tri = [&](uint32_t a, uint32_t b, uint32_t c, Surface s, uint8_t g) {
        const vec3 pa = t.vertices[a], pb = t.vertices[b], pc = t.vertices[c];
        const vec3 nrm = glm::cross(pb - pa, pc - pa);
        if (glm::dot(nrm, nrm) < 1e-8f) return;
        if (nrm.y < 0.0f) std::swap(b, c);
        t.triangles.push_back({a, b, c, s, g});
    };
    auto emit = [&](Strip& st) {
        st.first_vertex = uint32_t(t.vertices.size());
        const int ns = st.n();
        for (int i = 0; i < ns; ++i) {
            const CenterSample& c = st.c[size_t(i)];
            const float hw = c.half_width;
            const float sl = st.verge_at(c.u, false).first, sr = st.verge_at(c.u, true).first;
            float lat[kCols] = {-(hw + sl), -hw, -hw * 0.5f, 0.0f, hw * 0.5f, hw, hw + sr};
            // On the inside of a bend tighter than the road is wide, the
            // edges would cross over themselves: keep them inside the bend's
            // radius (a hairpin's inner verge and wall pinch in instead).
            {
                const vec3 t0 = st.c[size_t(st.wrap(i - 3))].tangent, t1 = st.c[size_t(st.wrap(i + 3))].tangent;
                const float arc = glm::distance(st.c[size_t(st.wrap(i - 3))].pos, st.c[size_t(st.wrap(i + 3))].pos);
                const float turn = -glm::cross(t0, t1).y; // > 0 turning right
                const float angle = std::acos(std::clamp(glm::dot(t0, t1), -1.0f, 1.0f));
                if (arc > 0.1f && angle > 1e-3f) {
                    const float limit = 0.92f * arc / angle; // the bend's radius, less a little
                    for (float& l : lat)
                        if (turn > 0.0f ? l > limit : l < -limit) l = turn > 0.0f ? limit : -limit;
                }
            }
            for (float l : lat) t.vertices.push_back(c.pos + c.right * l);
            t.vertices.push_back(c.pos + c.right * lat[0] + c.up * kWallHeight);
            t.vertices.push_back(c.pos + c.right * lat[kCols - 1] + c.up * kWallHeight);
        }
        const int segs = st.closed ? ns : ns - 1;
        for (int i = 0; i < segs; ++i) {
            const int j = i + 1;
            const float s = st.c[size_t(i)].u;
            const bool floor = st.c[size_t(i)].has_floor && st.c[size_t(st.wrap(j))].has_floor;
            if (!floor) continue;
            for (int col = 0; col < kCols - 1; ++col) {
                Surface surf;
                if (col == 0) {
                    if (st.verge_at(s, false).first < 0.02f && st.verge_at(st.c[size_t(st.wrap(j))].u, false).first < 0.02f) continue;
                    surf = st.verge_at(s, false).second;
                } else if (col == kCols - 2) {
                    if (st.verge_at(s, true).first < 0.02f && st.verge_at(st.c[size_t(st.wrap(j))].u, true).first < 0.02f) continue;
                    surf = st.verge_at(s, true).second;
                } else {
                    surf = st.road_surface(s, col - 1);
                }
                quad(st.V(i, col), st.V(i, col + 1), st.V(j, col + 1), st.V(j, col), surf, st.group);
            }
            // Inward-facing walls on both sides.
            if (st.walled(s, false)) quad(st.V(i, 0), st.V(j, 0), st.V(j, kCols), st.V(i, kCols), Surface::Wall, st.group);
            if (st.walled(s, true)) quad(st.V(j, kCols - 1), st.V(i, kCols - 1), st.V(i, kCols + 1), st.V(j, kCols + 1), Surface::Wall, st.group);
        }
    };
    emit(main);
    for (Strip& st : strips) {
        emit(st);
        t.branches[size_t(st.branch)].first_vertex = st.first_vertex;
    }
    // Stitch each side road to the main one: along the stretches where they
    // share an edge, the side road's inner edge is moved onto the main road's
    // (their own splines only come within a few centimetres of each other,
    // which shows as a crack or a step).
    for (Strip& st : strips) {
        const int side = t.branches[size_t(st.branch)].side;
        const int col_m = side > 0 ? kCols - 2 : 1, col_b = side > 0 ? 1 : kCols - 2, foot_b = side > 0 ? 0 : kCols - 1;
        const int top_b = side > 0 ? kCols : kCols + 1;
        const std::vector<Range>& open = side > 0 ? st.open_l : st.open_r;
        for (int k = 0; k < st.n(); ++k) {
            if (!in_any(open, st.c[size_t(k)].u)) continue;
            vec3& p = t.vertices[st.V(k, col_b)];
            vec3 best = p;
            float best_d = 0.75f; // only edges that are meant to meet
            for (int i = 0; i < main.n(); ++i) {
                const vec3 a = t.vertices[main.V(i, col_m)], b = t.vertices[main.V(i + 1, col_m)];
                const vec3 ab = b - a;
                const float f = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
                const vec3 q = a + ab * f;
                const float d = glm::distance(p, q);
                if (d < best_d) best_d = d, best = q;
            }
            const bool foot_with_edge = glm::distance(t.vertices[st.V(k, foot_b)], p) < 1e-3f;
            p = best;
            if (foot_with_edge) {
                t.vertices[st.V(k, foot_b)] = best;
                t.vertices[st.V(k, top_b)] = best + st.c[size_t(k)].up * kWallHeight;
            }
        }
    }

    // ---- islands between the roads, their noses, and the gates on branches
    for (size_t b = 0; b < strips.size(); ++b) {
        const Strip& st = strips[b];
        const Track::Branch& tb = t.branches[b];
        const int side = tb.side;
        const uint8_t g = st.group;
        const Surface grass = st.verge_surface;
        const int col_m = side > 0 ? kCols - 2 : 1, col_b = side > 0 ? 1 : kCols - 2;      // road edges facing each other
        const int foot_m = side > 0 ? kCols - 1 : 0, foot_b = side > 0 ? 0 : kCols - 1;    // wall feet
        const int top_m = side > 0 ? kCols + 1 : kCols, top_b = side > 0 ? kCols : kCols + 1;
        auto island = [&](const Join& j) {
            // Zip the two edges into a strip of triangles.
            const auto& A = j.main_edge;
            const auto& B = j.branch_edge;
            if (A.size() < 1 || B.size() < 1) return;
            size_t ia = 0, ib = 0;
            while (ia + 1 < A.size() || ib + 1 < B.size()) {
                const uint32_t a0 = main.V(A[ia], col_m), b0 = st.V(B[ib], col_b);
                const bool step_a = ib + 1 >= B.size() ||
                                    (ia + 1 < A.size() && glm::distance(t.vertices[main.V(A[ia + 1], col_m)], t.vertices[b0]) <
                                                              glm::distance(t.vertices[a0], t.vertices[st.V(B[ib + 1], col_b)]));
                if (step_a) {
                    floor_tri(a0, b0, main.V(A[ia + 1], col_m), grass, g);
                    ++ia;
                } else {
                    floor_tri(a0, b0, st.V(B[ib + 1], col_b), grass, g);
                    ++ib;
                }
            }
            if (!j.nose) return;
            // The nose: a two-sided wall across where the walls start again,
            // and grass up to it.
            const int ma = main.wrap(A.back() + j.dir);
            const int kb = std::clamp(B.back() + j.dir, 0, st.n() - 1);
            const uint32_t em = main.V(A.back(), col_m), eb = st.V(B.back(), col_b);
            const uint32_t wm = main.V(ma, foot_m), wb = st.V(kb, foot_b);
            floor_tri(em, eb, wb, grass, g);
            floor_tri(em, wb, wm, grass, g);
            const uint32_t tm = main.V(ma, top_m), tbv = st.V(kb, top_b);
            quad(wm, wb, tbv, tm, Surface::Wall, g);
            quad(wm, tm, tbv, wb, Surface::Wall, g);
        };
        island(joins[b].first);
        island(joins[b].second);
        // Where the side road starts and ends, its outer wall bends in to meet
        // the main road's, over a wedge of grass.
        auto cap = [&](int at_main, int walled_main, int k_end) {
            const uint32_t em = main.V(at_main, col_m);
            const uint32_t pw = st.V(k_end, foot_m), pt = st.V(k_end, top_m);
            const uint32_t wm = main.V(walled_main, foot_m), wt = main.V(walled_main, top_m);
            floor_tri(em, pw, wm, grass, g);
            quad(pw, wm, wt, pt, Surface::Wall, g);
            quad(pw, pt, wt, wm, Surface::Wall, g);
        };
        cap(tb.fork, main.wrap(tb.fork - 1), 0);
        cap(tb.merge, main.wrap(tb.merge + 1), st.n() - 1);

        if (tb.gate < 0) continue;
        const int shut = inverted(tb.gate);
        if (tb.vanish) {
            // While the road is gone, a fence along the main road's open side.
            for (const Range& r : opened[b]) {
                const int i0 = main.sample_at(r.a), count = int(std::round((r.b - r.a) / step));
                for (int k = 0; k < count; k += 3) {
                    const int ia = main.wrap(i0 + k), ib = main.wrap(i0 + std::min(k + 3, count));
                    const vec3 pa = t.vertices[main.V(ia, col_m)], pb = t.vertices[main.V(ib, col_m)];
                    const CenterSample& c = main.c[size_t(ia)];
                    Track::Barrier bar;
                    bar.forward = glm::normalize(pb - pa);
                    bar.up = c.up;
                    bar.right = glm::normalize(glm::cross(bar.forward, bar.up));
                    bar.half = vec3(0.25f, 0.6f, 0.5f * glm::length(pb - pa) + 0.15f);
                    bar.pos = 0.5f * (pa + pb) + c.right * (float(side) * 0.3f);
                    bar.gate = shut;
                    bar.sample = ia;
                    bar.branch = int(b);
                    t.barriers.push_back(bar);
                }
            }
        } else {
            // A barrier across the branch's mouth, just past the island.
            const Join& j = joins[b].first;
            const int kb = std::clamp((j.branch_edge.empty() ? 0 : j.branch_edge.back()) + 4, 0, st.n() - 1);
            const CenterSample& c = st.c[size_t(kb)];
            const float rl = c.half_width + st.verge_at(c.u, false).first, rr = c.half_width + st.verge_at(c.u, true).first;
            Track::Barrier bar;
            bar.forward = c.tangent, bar.right = c.right, bar.up = c.up;
            bar.half = vec3(0.5f * (rl + rr) + 0.2f, 1.1f, 0.45f);
            bar.pos = c.pos + c.right * (0.5f * (rr - rl));
            bar.gate = shut;
            bar.sample = st.first + kb;
            bar.branch = int(b);
            t.barriers.push_back(bar);
        }
    }

    // ---- things placed on the roads
    auto place = [&](Strip& st) {
        // Obstacles: prisms on the road, faces outward, base sunk a little so a
        // banked road never shows a gap underneath.
        for (const auto& [s_at, f] : st.obstacles) {
            Track::Obstacle o;
            const int si = st.sample_at(s_at);
            o.sample = st.first + si;
            const CenterSample& c = st.c[size_t(si)];
            o.forward = c.tangent, o.right = c.right, o.up = c.up;
            o.half = vec3(0.5f * f->width, 0.5f * f->height, 0.5f * f->length);
            o.pos = c.pos + c.right * f->offset - c.up * 0.3f;
            o.sides = std::clamp(f->sides, 3, 16);
            {
                using B = Track::Breakable;
                const bool pillar = o.sides > 4;
                const std::string& th = def.theme;
                std::string kind = f->prop;
                if (kind.empty()) {
                    if (th == "desert" || th == "canyon" || th == "volcano") kind = "solid";
                    else if (th == "snow") kind = "crystal";
                    else if (th == "market") kind = pillar ? "solid" : "crate";
                    else if (th == "sky" || th == "night") kind = pillar ? "tires" : "crate";
                    else kind = pillar ? "barrel" : "crate";
                }
                o.breakable = kind == "crate" ? B::Crate : kind == "barrel" ? B::Barrel : kind == "crystal" ? B::Crystal
                            : kind == "tires" ? B::Tires : B::None;
            }
            o.first_tri = uint32_t(t.triangles.size());
            if (o.breakable != Track::Breakable::None) { // no collision triangles: the World handles it
                o.tri_count = 0;
                t.obstacles.push_back(o);
                continue;
            }
            const uint32_t v0 = uint32_t(t.vertices.size());
            const int k = o.sides;
            // Corners at half-angle offsets, so 4 sides make a box aligned with the road.
            for (int i = 0; i < k; ++i) {
                const float ang = (float(i) + 0.5f) / float(k) * 2.0f * kPi, r = 1.0f / std::cos(kPi / float(k));
                const vec3 foot = o.pos + o.right * (std::cos(ang) * r * o.half.x) + o.forward * (std::sin(ang) * r * o.half.z);
                t.vertices.push_back(foot);
                t.vertices.push_back(foot + o.up * (2.0f * o.half.y + 0.3f));
            }
            const vec3 mid = o.pos + o.up * (o.half.y + 0.15f);
            auto face = [&](uint32_t a, uint32_t b, uint32_t c3) { // wound to face away from the middle
                const vec3 pa = t.vertices[a], pb = t.vertices[b], pc = t.vertices[c3];
                if (glm::dot(glm::cross(pb - pa, pc - pa), (pa + pb + pc) / 3.0f - mid) < 0.0f) std::swap(b, c3);
                t.triangles.push_back({a, b, c3, Surface::Wall, st.group});
            };
            for (int i = 0; i < k; ++i) {
                const uint32_t a = v0 + uint32_t(2 * i), b = v0 + uint32_t(2 * ((i + 1) % k));
                face(a, b, b + 1);
                face(a, b + 1, a + 1);
            }
            for (int i = 1; i + 1 < k; ++i) face(v0 + 1, v0 + uint32_t(2 * i + 1), v0 + uint32_t(2 * i + 3));
            o.tri_count = uint32_t(t.triangles.size()) - o.first_tri;
            t.obstacles.push_back(o);
        }
        // Movers: placed in the road's frame at their sample.
        for (const auto& [s_at, f] : st.movers) {
            Track::Mover m;
            const int si = st.sample_at(s_at);
            m.sample = st.first + si;
            const CenterSample& c = st.c[size_t(si)];
            m.kind = f->block ? Track::Mover::Kind::Block : Track::Mover::Kind::Platform;
            m.forward = c.tangent, m.right = c.right, m.up = c.up;
            m.half = vec3(0.5f * f->width, 0.5f * f->height, 0.5f * f->length);
            m.base = c.pos + c.right * f->offset;
            m.travel = c.right * f->travel.x + c.up * f->travel.y + c.tangent * f->travel.z;
            m.period = std::max(30, int(std::round(f->period * 60.0f)));
            m.phase = f->phase;
            m.ramp = f->block ? 0.0f : std::min(f->ramp, m.half.z);
            m.deck = f->boost ? Surface::BoostPad : Surface::Road;
            t.movers.push_back(m);
        }
        // Barriers written in the track: up while their gate holds.
        for (const auto& [s_at, f] : st.barriers) {
            const int si = st.sample_at(s_at);
            const CenterSample& c = st.c[size_t(si)];
            Track::Barrier bar;
            bar.forward = c.tangent, bar.right = c.right, bar.up = c.up;
            const float width = f->width > 0.0f ? f->width : 2.0f * c.half_width + st.verge_at(c.u, false).first + st.verge_at(c.u, true).first;
            bar.half = vec3(0.5f * width, 0.5f * f->height, 0.5f * f->length);
            const float centre = f->width > 0.0f ? f->offset : 0.5f * (st.verge_at(c.u, true).first - st.verge_at(c.u, false).first);
            bar.pos = c.pos + c.right * centre - c.up * 0.05f;
            bar.gate = f->gate;
            bar.sample = st.first + si;
            t.barriers.push_back(bar);
        }
        for (const auto& [s_at, f] : st.switches) {
            const int si = st.sample_at(s_at);
            const CenterSample& c = st.c[size_t(si)];
            Track::Switch sw;
            sw.forward = c.tangent, sw.right = c.right, sw.up = c.up;
            sw.pos = c.pos + c.right * f->offset + c.up * 0.02f;
            sw.radius = std::max(1.0f, 0.5f * f->width);
            sw.hold = std::max(1, int(std::round(f->hold * 60.0f)));
            sw.sample = st.first + si;
            t.switches.push_back(sw);
        }
        for (const auto& [r, f] : st.zones) {
            Track::Zone z;
            const int a = st.sample_at(r.a);
            int count = int(std::round((r.b - r.a) / st.step));
            if (!st.closed) count = std::min(count, st.n() - a);
            z.surface = f->surface;
            z.gate = f->gate;
            z.deep = f->deep;
            if (z.deep) { // the water's surface: level with the road where the zone starts or ends, whichever is lower
                const int last = st.closed ? (a + std::max(1, count) - 1) % st.n() : std::min(a + std::max(1, count) - 1, st.n() - 1);
                z.level = f->level < 1e8f ? f->level : std::min(st.c[size_t(a)].pos.y, st.c[size_t(last)].pos.y);
            }
            if (f->width > 0.0f) z.lo = f->offset - 0.5f * f->width, z.hi = f->offset + 0.5f * f->width;
            else if (!f->left) z.lo = 0.4f; // half the road, leaving the line down the middle
            else if (!f->right) z.hi = -0.4f;
            // On the loop a zone can't run over the start line: split it there.
            if (st.closed && a + count > st.n()) {
                Track::Zone z2 = z;
                z2.first = st.first, z2.count = a + count - st.n();
                t.zones.push_back(z2);
                count = st.n() - a;
            }
            z.first = st.first + a, z.count = std::max(1, count);
            t.zones.push_back(z);
        }
        for (const auto& [r, f] : st.tunnels) {
            Track::Tunnel tn;
            tn.height = f->height;
            tn.style = f->name;
            const int a = st.sample_at(r.a);
            int count = int(std::round((r.b - r.a) / st.step)) + 1;
            if (!st.closed) count = std::min(count, st.n() - a);
            auto spans = [&](int from, int n) {
                std::vector<float> out;
                for (int k = 0; k < n; ++k) {
                    const int i = st.wrap(from + k);
                    const vec3 c = st.c[size_t(i)].pos;
                    out.push_back(std::max(glm::distance(t.vertices[st.V(i, 0)], c), glm::distance(t.vertices[st.V(i, kCols - 1)], c)));
                }
                return out;
            };
            // On the loop a tunnel can't run over the start line: split it there.
            if (st.closed && a + count > st.n()) {
                Track::Tunnel t2 = tn;
                t2.first = st.first, t2.count = a + count - st.n();
                t2.half_span = spans(0, t2.count);
                t.tunnels.push_back(t2);
                count = st.n() - a;
            }
            tn.first = st.first + a, tn.count = std::max(1, count);
            tn.half_span = spans(a, tn.count);
            t.tunnels.push_back(tn);
        }
        // Item box rows: four boxes across the road.
        for (float s_box : st.box_rows) {
            if (st.closed) s_box = std::fmod(std::fmod(s_box, st.total) + st.total, st.total);
            int i = 0;
            while (i + 1 < st.n() && st.c[size_t(i)].u < s_box) ++i;
            const CenterSample& c = st.c[size_t(i)];
            for (float l : {-0.6f, -0.2f, 0.2f, 0.6f}) t.item_boxes.push_back(c.pos + c.right * (l * c.half_width) + c.up * 1.0f);
        }
    };
    main.first = 0;
    place(main);
    for (Strip& st : strips) place(st);
    // Out-of-bounds floor far below the track.
    float min_y = 1e9f;
    for (const vec4& c : ctrl) min_y = std::min(min_y, c.y);
    for (const BranchDef& bd : def.branches)
        for (const vec4& c : bd.points) min_y = std::min(min_y, c.y);
    // On terrain the floor sits just under the ground, so a fall is short.
    const float floor_y = std::floor(min_y) - (def.ground ? 12.0f : 20.0f);
    vec3 lo(1e9f), hi(-1e9f);
    for (const CenterSample& c : t.road) lo = glm::min(lo, c.pos), hi = glm::max(hi, c.pos);
    const float e = std::max(hi.x - lo.x, hi.z - lo.z) + 1200.0f;
    const vec3 mid = 0.5f * (lo + hi);
    const uint32_t base_v = uint32_t(t.vertices.size());
    t.vertices.push_back({mid.x - e, floor_y, mid.z - e});
    t.vertices.push_back({mid.x - e, floor_y, mid.z + e});
    t.vertices.push_back({mid.x + e, floor_y, mid.z + e});
    t.vertices.push_back({mid.x + e, floor_y, mid.z - e});
    quad(base_v, base_v + 1, base_v + 2, base_v + 3, Surface::OutOfBounds, 0);
    t.kill_y = floor_y - 10.0f;

    // Checkpoint regions roughly every 40 m; region 0 starts at the line.
    const int per = std::max(1, int(40.0f / step));
    for (int i = 0; i < n; i += per) t.checkpoints.push_back(i);
    if (t.checkpoints.size() > 1 && n - t.checkpoints.back() < per / 2) t.checkpoints.pop_back();

    t.finalize();
    return t;
}

TrackDef greybox_track_def() {
    TrackDef d;
    d.id = "greybox";
    d.name = "Greybox Loop";
    d.theme = "meadow";
    // Closed loop: a climbing north straight with a trick ramp, a sweeping
    // east turn, a glide ramp over a chasm on the way south, and a climbing
    // return through sand, ice and mud.
    for (vec3 p : {vec3{0, 0, 0}, {0, 0, 50}, {0, 3, 100}, {6, 6, 150}, {30, 7, 185}, {70, 7, 200}, {115, 5, 195},
                   {150, 3, 170}, {165, 2, 125}, {165, 2, 75}, {160, -10, 0}, {150, -12, -45}, {120, -12, -80},
                   {75, -10, -95}, {30, -7, -85}, {5, -4, -60}, {-5, -1, -30}})
        d.points.push_back(vec4(p, 0.0f));
    using K = TrackFeature::Kind;
    auto F = [&](K kind, TrackPos at) {
        TrackFeature f;
        f.kind = kind;
        f.at = at;
        d.features.push_back(f);
        return &d.features.back();
    };
    auto* trick = F(K::TrickRamp, {1, 2});
    trick->length = 10.0f, trick->height = 1.6f;
    auto* pad1 = F(K::BoostPad, {1, -14});
    pad1->length = 8.0f, pad1->lanes = 0b0110;
    auto* pad2 = F(K::BoostPad, {5, 0});
    pad2->length = 8.0f, pad2->lanes = 0b0110;
    auto* glide = F(K::GlideRamp, {9, 4});
    glide->length = 24.0f, glide->height = 3.0f, glide->gap = 62.0f;
    auto* sand = F(K::Road, {6, 0});
    sand->to = {7, 0}, sand->surface = Surface::Sand;
    auto* ice = F(K::Road, {11, 6});
    ice->to = {12, 0}, ice->surface = Surface::Ice;
    auto* gravel = F(K::Verge, {3, 0});
    gravel->to = {5, 0}, gravel->left = false, gravel->width = 10.0f, gravel->surface = Surface::OffroadMedium;
    auto* mud = F(K::Verge, {13, 0});
    mud->to = {15, 0}, mud->right = false, mud->width = 9.0f, mud->surface = Surface::OffroadHeavy;
    F(K::ItemBoxes, {2, 0});
    F(K::ItemBoxes, {6.5f, 0});
    F(K::ItemBoxes, {10.5f, 20});
    F(K::ItemBoxes, {14, 0});
    return d;
}

Track make_greybox_track() { return build_track(greybox_track_def()); }

// ---------------------------------------------------------------- JSON

namespace {

bool parse_surface(const std::string& s, Surface& out) {
    static const std::pair<const char*, Surface> kNames[] = {
        {"road", Surface::Road},          {"offroad_light", Surface::OffroadLight},
        {"offroad_medium", Surface::OffroadMedium}, {"offroad_heavy", Surface::OffroadHeavy},
        {"sand", Surface::Sand},          {"ice", Surface::Ice},
        {"water", Surface::Water},          {"oil", Surface::Oil},
        {"boost", Surface::BoostPad},      {"lava", Surface::OutOfBounds}, // zones
    };
    for (const auto& [name, surf] : kNames)
        if (s == name) {
            out = surf;
            return true;
        }
    return false;
}

TrackPos parse_pos(const nlohmann::json& j) {
    if (j.is_array()) return {j.at(0).get<float>(), j.size() > 1 ? j.at(1).get<float>() : 0.0f};
    return {j.get<float>(), 0.0f};
}

int ticks(float seconds) { return int(std::lround(seconds * 60.0f)); }

// A gate written in a track file:
//   {"laps": [2, 3]}            while the leader is on lap 2 or 3 ("from_lap": 2 = 2 and on)
//   {"cycle": [12, 5, 3]}       5 s of every 12 s of race time, starting 3 s in
//   {"switch": "lever"}         while the switch named lever is pressed
// any of them with "invert": true; or a name from the track's "gates", "!name" for its opposite.
GateDef parse_gate(const nlohmann::json& w) {
    GateDef g;
    if (w.contains("laps")) {
        g.gate.when = Gate::When::Laps;
        g.gate.lap_lo = w["laps"].at(0).get<int>();
        g.gate.lap_hi = w["laps"].size() > 1 ? w["laps"].at(1).get<int>() : g.gate.lap_lo;
    } else if (w.contains("lap")) {
        g.gate.when = Gate::When::Laps;
        g.gate.lap_lo = g.gate.lap_hi = w["lap"].get<int>();
    } else if (w.contains("from_lap")) {
        g.gate.when = Gate::When::Laps;
        g.gate.lap_lo = w["from_lap"].get<int>();
    } else if (w.contains("cycle")) {
        const auto& c = w["cycle"];
        g.gate.when = Gate::When::Cycle;
        g.gate.period = std::max(1, ticks(c.at(0).get<float>()));
        g.gate.open = ticks(c.size() > 1 ? c.at(1).get<float>() : c.at(0).get<float>() * 0.5f);
        g.gate.offset = ticks(c.size() > 2 ? c.at(2).get<float>() : 0.0f);
    } else if (w.contains("switch")) {
        g.gate.when = Gate::When::Switch;
        g.sw = w["switch"].get<std::string>();
    } else {
        throw std::runtime_error("a gate needs laps, lap, from_lap, cycle or switch");
    }
    g.gate.invert = w.value("invert", false);
    return g;
}

struct GateNames {
    std::map<std::string, int> index;
    int resolve(const nlohmann::json& w, TrackDef& d) {
        if (w.is_string()) {
            std::string name = w.get<std::string>();
            const bool inv = !name.empty() && name[0] == '!';
            if (inv) name = name.substr(1);
            const auto it = index.find(name);
            if (it == index.end()) throw std::runtime_error("unknown gate " + name);
            if (!inv) return it->second;
            GateDef g = d.gates[size_t(it->second)];
            g.gate.invert = !g.gate.invert;
            d.gates.push_back(g);
            return int(d.gates.size()) - 1;
        }
        d.gates.push_back(parse_gate(w));
        return int(d.gates.size()) - 1;
    }
};

// One feature; on a branch, `at` and `to` are metres from where it leaves.
TrackFeature parse_feature(const nlohmann::json& f, TrackDef& d, GateNames& gates, bool on_branch, Surface verge_surface) {
    TrackFeature tf;
    const std::string type = f.at("type").get<std::string>();
    using K = TrackFeature::Kind;
    if (type == "trick_ramp") tf.kind = K::TrickRamp;
    else if (type == "glide_ramp") tf.kind = K::GlideRamp;
    else if (type == "boost_pad") tf.kind = K::BoostPad;
    else if (type == "road") tf.kind = K::Road;
    else if (type == "verge") tf.kind = K::Verge;
    else if (type == "item_boxes") tf.kind = K::ItemBoxes;
    else if (type == "no_walls") tf.kind = K::NoWalls;
    else if (type == "obstacle") tf.kind = K::Obstacle;
    else if (type == "stairs") tf.kind = K::Stairs; // the road between from and to drawn as steps
    else if (type == "mover") tf.kind = K::Mover;
    else if (type == "barrier") tf.kind = K::Barrier;
    else if (type == "switch") tf.kind = K::Switch;
    else if (type == "zone") tf.kind = K::Zone;
    else if (type == "tunnel") tf.kind = K::Tunnel;
    else throw std::runtime_error("unknown feature type " + type);
    if (on_branch && tf.kind == K::Stairs) throw std::runtime_error("stairs only on the main loop");
    auto pos = [&](const nlohmann::json& j) { return on_branch ? TrackPos{0.0f, j.get<float>()} : parse_pos(j); };
    tf.at = pos(f.contains("at") ? f["at"] : f.at("from"));
    if (f.contains("to")) tf.to = pos(f["to"]);
    tf.length = f.value("length", tf.length);
    tf.height = f.value("height", tf.height);
    tf.gap = f.value("gap", tf.gap);
    tf.width = f.value("width", tf.width);
    tf.offset = f.value("offset", tf.offset);
    if (f.contains("when")) tf.gate = gates.resolve(f["when"], d);
    if (tf.kind == K::Obstacle) {
        tf.length = f.value("length", 2.5f);
        tf.width = f.value("width", 2.5f);
        tf.height = f.value("height", 2.5f);
        const std::string shape = f.value("shape", std::string("block"));
        if (shape == "block") tf.sides = 4;
        else if (shape == "pillar") tf.sides = 8;
        else throw std::runtime_error("unknown obstacle shape " + shape);
        tf.prop = f.value("prop", std::string());
        if (tf.prop == "tires") { // a stack of three: a pillar unless told otherwise
            if (!f.contains("shape")) tf.sides = 8;
            if (!f.contains("width")) tf.width = 1.7f;
            if (!f.contains("length")) tf.length = 1.7f;
            if (!f.contains("height")) tf.height = 1.1f;
        }
        // One that comes and goes is a barrier: the fixed track can't change.
        if (tf.gate >= 0) tf.kind = K::Barrier;
    }
    if (tf.kind == K::Barrier && type == "barrier") {
        tf.width = f.value("width", 0.0f); // 0: across the whole road
        tf.length = f.value("thickness", f.value("length", 1.0f));
        tf.height = f.value("height", 2.2f);
    }
    if (tf.kind == K::Switch) {
        tf.name = f.at("name").get<std::string>();
        tf.width = f.value("width", 5.0f);
        tf.hold = f.value("hold", 10.0f);
    }
    if (tf.kind == K::Tunnel) {
        tf.height = f.value("height", 7.0f);
        tf.name = f.value("style", std::string());
        if (tf.height < 3.5f) throw std::runtime_error("a tunnel needs a roof at least 3.5 m up");
    }
    if (tf.kind == K::Zone) {
        tf.width = f.value("width", 0.0f); // 0: the whole road (or one side of it)
        tf.surface = Surface::Water;
        const std::string depth = f.value("depth", std::string("shallow"));
        if (depth == "deep") tf.deep = true;
        else if (depth != "shallow") throw std::runtime_error("unknown zone depth " + depth);
        tf.level = f.value("level", tf.level);
    }
    if (tf.kind == K::Mover) {
        tf.block = f.value("kind", std::string("platform")) == "block";
        tf.width = f.value("width", tf.block ? 3.0f : 6.0f);
        tf.length = f.value("length", tf.block ? 3.0f : 12.0f);
        tf.height = f.value("height", tf.block ? 2.4f : 0.8f);
        if (f.contains("travel")) tf.travel = {f["travel"][0].get<float>(), f["travel"][1].get<float>(), f["travel"][2].get<float>()};
        tf.period = f.value("period", 4.0f);
        tf.phase = f.value("phase", 0.0f);
        tf.ramp = f.value("ramp", 2.5f);
        tf.boost = f.value("boost", false);
    }
    if (f.contains("lanes")) {
        tf.lanes = 0;
        for (const auto& l : f["lanes"]) tf.lanes |= 1 << std::clamp(l.get<int>(), 0, 3);
    }
    if (f.contains("side")) {
        const std::string side = f["side"].get<std::string>();
        tf.left = side != "right";
        tf.right = side != "left";
    }
    if (f.contains("surface") && !parse_surface(f["surface"].get<std::string>(), tf.surface))
        throw std::runtime_error("unknown surface in " + type);
    if (tf.kind == K::Verge && !f.contains("surface")) tf.surface = verge_surface;
    if (tf.deep && tf.surface != Surface::Water) throw std::runtime_error("only water zones can be deep");
    return tf;
}

vec4 parse_point(const nlohmann::json& p) {
    if (p.size() < 3) throw std::runtime_error("a point needs x, y, z");
    return vec4(p[0].get<float>(), p[1].get<float>(), p[2].get<float>(), p.size() > 3 ? p[3].get<float>() : 0.0f);
}

} // namespace

bool parse_track_def(const std::string& text, TrackDef& d, std::string* error) {
    try {
        const auto j = nlohmann::json::parse(text, nullptr, true, true);
        d = TrackDef{};
        d.id = j.at("id").get<std::string>();
        d.name = j.at("name").get<std::string>();
        d.theme = j.value("theme", d.theme);
        d.laps = j.value("laps", d.laps);
        d.ground = j.value("ground", d.ground);
        d.half_width = j.value("half_width", d.half_width);
        d.verge = j.value("verge", d.verge);
        if (j.contains("verge_surface") && !parse_surface(j["verge_surface"].get<std::string>(), d.verge_surface))
            throw std::runtime_error("unknown verge_surface");
        for (const auto& p : j.at("points")) d.points.push_back(parse_point(p));
        if (d.points.size() < 4) throw std::runtime_error("a track needs at least 4 points");
        GateNames gates;
        const nlohmann::json named = j.value("gates", nlohmann::json::object());
        for (auto it = named.begin(); it != named.end(); ++it) {
            d.gates.push_back(parse_gate(it.value()));
            gates.index[it.key()] = int(d.gates.size()) - 1;
        }
        for (const auto& f : j.value("features", nlohmann::json::array()))
            d.features.push_back(parse_feature(f, d, gates, false, d.verge_surface));
        for (const auto& b : j.value("branches", nlohmann::json::array())) {
            BranchDef bd;
            bd.name = b.value("name", std::string());
            bd.from = parse_pos(b.at("from"));
            bd.to = parse_pos(b.at("to"));
            const std::string side = b.value("side", std::string("right"));
            if (side != "left" && side != "right") throw std::runtime_error("a branch leaves on the left or the right");
            bd.side = side == "left" ? -1 : 1;
            bd.half_width = b.value("half_width", 0.0f);
            bd.junction = b.value("junction", bd.junction);
            bd.verge = b.value("verge", bd.verge);
            if (b.contains("verge_surface") && !parse_surface(b["verge_surface"].get<std::string>(), bd.verge_surface))
                throw std::runtime_error("unknown verge_surface in branch " + bd.name);
            if (b.contains("surface") && !parse_surface(b["surface"].get<std::string>(), bd.surface))
                throw std::runtime_error("unknown surface in branch " + bd.name);
            for (const auto& p : b.value("points", nlohmann::json::array())) bd.points.push_back(parse_point(p));
            if (b.contains("when")) bd.gate = gates.resolve(b["when"], d);
            bd.vanish = b.value("vanish", false);
            bd.ai_share = b.value("cpu_share", bd.ai_share);
            const Surface vs = bd.verge_surface != Surface::Count ? bd.verge_surface : d.verge_surface;
            for (const auto& f : b.value("features", nlohmann::json::array())) bd.features.push_back(parse_feature(f, d, gates, true, vs));
            d.branches.push_back(std::move(bd));
        }
        if (d.branches.size() > 32) throw std::runtime_error("at most 32 branches");
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool load_track_def(const std::string& path, TrackDef& d, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parse_track_def(ss.str(), d, error)) {
        if (error) *error = path + ": " + *error;
        return false;
    }
    return true;
}

bool TrackCatalog::load(const std::string& index_path, std::string* error) {
    tracks.clear();
    cups.clear();
    try {
        std::ifstream f(index_path);
        if (!f) throw std::runtime_error("cannot open " + index_path);
        const auto j = nlohmann::json::parse(f, nullptr, true, true);
        const std::string dir = index_path.substr(0, index_path.find_last_of("/\\") + 1);
        for (const auto& c : j.at("cups")) {
            Cup cup;
            cup.name = c.at("name").get<std::string>();
            for (const auto& id : c.at("tracks")) {
                const std::string tid = id.get<std::string>();
                cup.tracks.push_back(tid);
                if (find(tid) >= 0) continue;
                TrackDef d;
                std::string err;
                if (!load_track_def(dir + tid + ".json", d, &err)) throw std::runtime_error(err);
                tracks.push_back(std::move(d));
            }
            cups.push_back(std::move(cup));
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

Track make_track(const TrackCatalog& catalog, const std::string& id) {
    const int i = catalog.find(id);
    return i >= 0 ? build_track(catalog.tracks[size_t(i)]) : make_greybox_track();
}

int TrackCatalog::find(const std::string& id) const {
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id) return int(i);
    return -1;
}

} // namespace gk::sim
