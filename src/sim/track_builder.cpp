// Builds track geometry from a TrackDef: a closed Catmull-Rom spline through
// the control points, resampled every ~2 m, with a road of four lanes,
// verges, walls, ramps, boost pads, glide gaps and item box rows.
#include <filesystem>
#include <optional>
#include <cstdio>
#include <mutex>
#include <map>
#include <array>
#include "gk/sim/track_def.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
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

// A stretch of the loop, [a, b) in metres from the start line; may wrap.
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

} // namespace

namespace {
// Positions and triangles by material from a Wavefront OBJ: only what
// collision needs. Parsed once per file.
struct ObjCollision {
    std::vector<vec3> v;
    std::vector<std::pair<std::array<uint32_t, 3>, std::string>> tris;
};
const ObjCollision* load_obj_collision(const std::string& path) {
    static std::map<std::string, ObjCollision> cache;
    static std::mutex m;
    std::lock_guard lock(m);
    if (auto it = cache.find(path); it != cache.end()) return &it->second;
    std::ifstream f(path);
    if (!f) return nullptr;
    ObjCollision o;
    std::string line, mat;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            vec3 p;
            if (std::sscanf(line.c_str() + 2, "%f %f %f", &p.x, &p.y, &p.z) == 3) o.v.push_back(p);
        } else if (line.rfind("usemtl ", 0) == 0) {
            mat = line.substr(7);
            while (!mat.empty() && (mat.back() == '\r' || mat.back() == ' ')) mat.pop_back();
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::vector<uint32_t> idx;
            std::istringstream ss(line.substr(2));
            std::string tok;
            while (ss >> tok) {
                const long i = std::strtol(tok.c_str(), nullptr, 10);
                idx.push_back(uint32_t(i > 0 ? i - 1 : long(o.v.size()) + i));
            }
            for (size_t k = 1; k + 1 < idx.size(); ++k) o.tris.push_back({{idx[0], idx[k], idx[k + 1]}, mat});
        }
    }
    return &cache.emplace(path, std::move(o)).first->second;
}

void add_mesh_course(Track& t, const TrackDef::MeshCourse& mc) {
    const ObjCollision* o = load_obj_collision(mc.file);
    if (!o) throw std::runtime_error("cannot open course model " + mc.file);
    auto role = [&](const std::string& mat) -> std::optional<Surface> {
        auto has = [&](const std::vector<std::string>& l) { return std::find(l.begin(), l.end(), mat) != l.end(); };
        if (has(mc.boost)) return Surface::BoostPad;
        if (has(mc.ramps)) return Surface::TrickRamp;
        if (has(mc.road)) return Surface::Road;
        if (has(mc.walls)) return Surface::Wall;
        return std::nullopt;
    };
    std::map<uint32_t, uint32_t> remap;
    for (const auto& [tri, mat] : o->tris) {
        const auto s = role(mat);
        if (!s) continue;
        uint32_t out[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            const uint32_t i = tri[size_t(k)];
            if (i >= o->v.size()) { ok = false; break; }
            auto [it, fresh] = remap.try_emplace(i, uint32_t(t.vertices.size()));
            if (fresh) t.vertices.push_back(o->v[i] * mc.scale + mc.offset);
            out[k] = it->second;
        }
        if (!ok) continue;
        // Floors must face up for the wheel probes; the file's winding may not.
        const vec3 a = t.vertices[out[0]], b = t.vertices[out[1]], c = t.vertices[out[2]];
        if (*s != Surface::Wall && glm::cross(b - a, c - a).y < 0.0f) std::swap(out[1], out[2]);
        t.triangles.push_back({out[0], out[1], out[2], *s});
    }
}
} // namespace

Track build_track(const TrackDef& def) {
    Track t;
    t.id = def.id;
    t.name = def.name;
    t.theme = def.theme;
    t.laps = def.laps;
    std::vector<vec4> ctrl = def.points;
    for (auto& c : ctrl)
        if (c.w <= 0.0f) c.w = def.half_width;
    const int nc = int(ctrl.size());
    if (nc < 4) return t;

    // Dense sampling of the spline, then resampling by arc length.
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

    std::vector<vec4> base(static_cast<size_t>(n));
    std::vector<float> ctrl_at(static_cast<size_t>(n));
    size_t di = 0;
    for (int i = 0; i < n; ++i) {
        const float s = float(i) * step;
        while (di + 1 < dense.size() && dense_s[di + 1] < s) ++di;
        const float seg = dense_s[di + 1] - dense_s[di];
        const float f = seg > 0 ? (s - dense_s[di]) / seg : 0.0f;
        base[size_t(i)] = glm::mix(dense[di], dense[(di + 1) % dense.size()], f);
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
    auto range = [&](float a, float b) { return Range{a, b, total}; };

    // Features along the loop.
    std::vector<Range> trick_ramps, glide_ramps, gaps, no_respawn, no_walls_l, no_walls_r;
    std::vector<std::pair<Range, int>> pads;
    std::vector<std::pair<Range, Surface>> road_surfaces;
    struct VergeSpan { Range r; bool left, right; float width; Surface surface; };
    std::vector<VergeSpan> verges;
    std::vector<float> box_rows;
    std::vector<float> trick_heights, glide_heights;
    std::vector<std::pair<float, const TrackFeature*>> obstacles, movers;
    for (const TrackFeature& f : def.features) {
        const float a = s_of(f.at);
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
        case TrackFeature::Kind::Road: road_surfaces.push_back({range(a, s_of(f.to)), f.surface}); break;
        case TrackFeature::Kind::Verge: verges.push_back({range(a, s_of(f.to)), f.left, f.right, f.width, f.surface}); break;
        case TrackFeature::Kind::ItemBoxes: box_rows.push_back(a); break;
        case TrackFeature::Kind::Obstacle:
            obstacles.push_back({a, &f});
            if (std::abs(f.offset) < 0.5f * f.width + 1.0f) no_respawn.push_back(range(a - 8.0f, a + 8.0f)); // covers the centre
            break;
        case TrackFeature::Kind::Stairs: t.stairs.push_back({a, s_of(f.to)}); break;
        case TrackFeature::Kind::Mover: movers.push_back({a, &f}); break;
        case TrackFeature::Kind::NoWalls:
            if (f.left) no_walls_l.push_back(range(a, s_of(f.to)));
            if (f.right) no_walls_r.push_back(range(a, s_of(f.to)));
            break;
        }
    }
    auto in_any = [](const std::vector<Range>& rs, float s) {
        return std::any_of(rs.begin(), rs.end(), [&](const Range& r) { return r.contains(s); });
    };

    auto height_offset = [&](float s) {
        float h = 0.0f;
        for (size_t i = 0; i < trick_ramps.size(); ++i)
            if (trick_ramps[i].contains(s)) h += trick_heights[i] * smooth(trick_ramps[i].progress(s));
        for (size_t i = 0; i < glide_ramps.size(); ++i)
            if (glide_ramps[i].contains(s)) h += glide_heights[i] * smooth(glide_ramps[i].progress(s));
        return h;
    };

    t.center.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        CenterSample& c = t.center[size_t(i)];
        c.s = float(i) * step;
        c.pos = xyz(base[size_t(i)]) + vec3(0, height_offset(c.s), 0);
    }
    for (int i = 0; i < n; ++i) {
        CenterSample& c = t.center[size_t(i)];
        const vec3 next = xyz(base[size_t((i + 1) % n)]), prev = xyz(base[size_t((i - 1 + n) % n)]);
        c.tangent = glm::normalize(next - prev);
        c.right = glm::normalize(glm::cross(c.tangent, kUp));
        c.up = glm::normalize(glm::cross(c.right, c.tangent));
        c.half_width = base[size_t(i)].w;
        c.has_floor = !in_any(gaps, c.s);
        c.respawn_ok = !in_any(no_respawn, c.s);
    }
    t.length = total;

    auto road_surface = [&](float s, int lane) {
        if (in_any(trick_ramps, s)) return Surface::TrickRamp;
        if (in_any(glide_ramps, s)) return Surface::GlideRamp;
        for (const auto& [r, lanes] : pads)
            if (r.contains(s) && (lanes >> lane & 1)) return Surface::BoostPad;
        for (const auto& [r, surf] : road_surfaces)
            if (r.contains(s)) return surf;
        return Surface::Road;
    };
    auto verge_at = [&](float s, bool right) {
        for (const VergeSpan& v : verges)
            if (v.r.contains(s) && (right ? v.right : v.left)) return std::pair(v.width, v.surface);
        return std::pair(def.verge, def.verge_surface);
    };

    constexpr float kWallHeight = 1.5f;
    constexpr int kCols = 7; // lateral vertices per sample: wall, verge, 4 lanes, verge, wall
    for (int i = 0; i < n; ++i) {
        const CenterSample& c = t.center[size_t(i)];
        const float hw = c.half_width;
        const float sl = verge_at(c.s, false).first, sr = verge_at(c.s, true).first;
        const float lat[kCols] = {-(hw + sl), -hw, -hw * 0.5f, 0.0f, hw * 0.5f, hw, hw + sr};
        for (float l : lat) t.vertices.push_back(c.pos + c.right * l);
        t.vertices.push_back(c.pos + c.right * lat[0] + c.up * kWallHeight);
        t.vertices.push_back(c.pos + c.right * lat[kCols - 1] + c.up * kWallHeight);
    }
    constexpr int kStride = kCols + 2;
    auto V = [&](int sample, int col) { return uint32_t(((sample % n) * kStride) + col); };
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, Surface s) {
        t.triangles.push_back({a, b, c, s});
        t.triangles.push_back({a, c, d, s});
    };
    for (int i = 0; i < n; ++i) {
        const int j = i + 1;
        const float s = t.center[size_t(i)].s;
        const bool floor = t.center[size_t(i)].has_floor && t.center[size_t(j % n)].has_floor;
        if (!floor) continue;
        for (int col = 0; col < kCols - 1; ++col) {
            Surface surf;
            if (col == 0) surf = verge_at(s, false).second;
            else if (col == kCols - 2) surf = verge_at(s, true).second;
            else surf = road_surface(s, col - 1);
            quad(V(i, col), V(i, col + 1), V(j, col + 1), V(j, col), surf);
        }
        // Inward-facing walls on both sides.
        if (!in_any(no_walls_l, s)) quad(V(i, 0), V(j, 0), V(j, kCols), V(i, kCols), Surface::Wall);
        if (!in_any(no_walls_r, s)) quad(V(j, kCols - 1), V(i, kCols - 1), V(i, kCols + 1), V(j, kCols + 1), Surface::Wall);
    }

    // Obstacles: prisms on the road, faces outward, base sunk a little so a
    // banked road never shows a gap underneath.
    auto sample_at = [&](float s_at) {
        s_at = std::fmod(std::fmod(s_at, total) + total, total);
        return std::min(n - 1, int(std::round(s_at / step))) % n;
    };
    for (const auto& [s_at, f] : obstacles) {
        Track::Obstacle o;
        o.sample = sample_at(s_at);
        const CenterSample& c = t.center[size_t(o.sample)];
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
        auto face = [&](uint32_t a, uint32_t b, uint32_t c) { // wound to face away from the middle
            const vec3 pa = t.vertices[a], pb = t.vertices[b], pc = t.vertices[c];
            if (glm::dot(glm::cross(pb - pa, pc - pa), (pa + pb + pc) / 3.0f - mid) < 0.0f) std::swap(b, c);
            t.triangles.push_back({a, b, c, Surface::Wall});
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
    for (const auto& [s_at, f] : movers) {
        Track::Mover m;
        m.sample = sample_at(s_at);
        const CenterSample& c = t.center[size_t(m.sample)];
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

    // Out-of-bounds floor far below the track.
    float min_y = 1e9f;
    for (const vec4& c : ctrl) min_y = std::min(min_y, c.y);
    const float floor_y = std::floor(min_y) - 20.0f;
    vec3 lo(1e9f), hi(-1e9f);
    for (const CenterSample& c : t.center) lo = glm::min(lo, c.pos), hi = glm::max(hi, c.pos);
    const float e = std::max(hi.x - lo.x, hi.z - lo.z) + 1200.0f;
    const vec3 mid = 0.5f * (lo + hi);
    const uint32_t base_v = uint32_t(t.vertices.size());
    t.vertices.push_back({mid.x - e, floor_y, mid.z - e});
    t.vertices.push_back({mid.x - e, floor_y, mid.z + e});
    t.vertices.push_back({mid.x + e, floor_y, mid.z + e});
    t.vertices.push_back({mid.x + e, floor_y, mid.z - e});
    quad(base_v, base_v + 1, base_v + 2, base_v + 3, Surface::OutOfBounds);
    t.kill_y = floor_y - 10.0f;

    if (def.mesh) {
        // A model course: keep only the rails and the floor far below, and
        // drive on the model's own road.
        std::erase_if(t.triangles, [](const TrackTriangle& tri) { return tri.surface != Surface::Wall && tri.surface != Surface::OutOfBounds; });
        t.obstacles.clear();
        add_mesh_course(t, *def.mesh);
    }

    // Item box rows: four boxes across the road.
    for (float s_box : box_rows) {
        s_box = std::fmod(std::fmod(s_box, total) + total, total);
        int i = 0;
        while (i + 1 < n && t.center[size_t(i)].s < s_box) ++i;
        const CenterSample& c = t.center[size_t(i)];
        for (float l : {-0.6f, -0.2f, 0.2f, 0.6f}) t.item_boxes.push_back(c.pos + c.right * (l * c.half_width) + c.up * 1.0f);
    }

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
        {"water", Surface::Water},
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

} // namespace

bool parse_track_def(const std::string& text, TrackDef& d, std::string* error) {
    try {
        const auto j = nlohmann::json::parse(text, nullptr, true, true);
        d = TrackDef{};
        d.id = j.at("id").get<std::string>();
        d.name = j.at("name").get<std::string>();
        d.theme = j.value("theme", d.theme);
        d.laps = j.value("laps", d.laps);
        d.half_width = j.value("half_width", d.half_width);
        d.verge = j.value("verge", d.verge);
        if (j.contains("verge_surface") && !parse_surface(j["verge_surface"].get<std::string>(), d.verge_surface))
            throw std::runtime_error("unknown verge_surface");
        for (const auto& p : j.at("points")) {
            if (p.size() < 3) throw std::runtime_error("a point needs x, y, z");
            d.points.push_back(vec4(p[0].get<float>(), p[1].get<float>(), p[2].get<float>(),
                                    p.size() > 3 ? p[3].get<float>() : 0.0f));
        }
        if (d.points.size() < 4) throw std::runtime_error("a track needs at least 4 points");
        if (j.contains("mesh")) {
            TrackDef::MeshCourse m;
            const auto& mj = j["mesh"];
            m.file = mj.at("file").get<std::string>();
            m.scale = mj.value("scale", 1.0f);
            if (mj.contains("offset")) m.offset = {mj["offset"][0].get<float>(), mj["offset"][1].get<float>(), mj["offset"][2].get<float>()};
            auto list = [&](const char* key, std::vector<std::string>& out) {
                if (mj.contains(key))
                    for (const auto& s : mj[key]) out.push_back(s.get<std::string>());
            };
            list("road", m.road);
            list("walls", m.walls);
            list("boost", m.boost);
            list("ramps", m.ramps);
            list("hidden", m.hidden);
            if (mj.contains("glow"))
                for (const auto& [k, v] : mj["glow"].items()) m.glow.push_back({k, v.get<std::string>()});
            d.mesh = m;
        }
        for (const auto& f : j.value("features", nlohmann::json::array())) {
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
            else throw std::runtime_error("unknown feature type " + type);
            tf.at = parse_pos(f.contains("at") ? f["at"] : f.at("from"));
            if (f.contains("to")) tf.to = parse_pos(f["to"]);
            tf.length = f.value("length", tf.length);
            tf.height = f.value("height", tf.height);
            tf.gap = f.value("gap", tf.gap);
            tf.width = f.value("width", tf.width);
            tf.offset = f.value("offset", tf.offset);
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
            if (tf.kind == K::Verge && !f.contains("surface")) tf.surface = d.verge_surface;
            d.features.push_back(tf);
        }
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

int TrackCatalog::load_local(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    Cup cup;
    cup.name = "Local Cup";
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const fs::path course = entry.path() / "course.json";
        if (!fs::exists(course, ec)) continue;
        TrackDef d;
        std::string err;
        if (!load_track_def(course.string(), d, &err) || find(d.id) >= 0) continue;
        if (d.mesh) {
            d.mesh->dir = entry.path().string() + "/";
            if (!fs::path(d.mesh->file).is_absolute()) d.mesh->file = d.mesh->dir + d.mesh->file;
        }
        cup.tracks.push_back(d.id);
        tracks.push_back(std::move(d));
    }
    const int n = int(cup.tracks.size());
    if (n > 0) cups.push_back(std::move(cup));
    return n;
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
