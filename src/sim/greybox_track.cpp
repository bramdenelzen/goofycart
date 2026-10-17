#include "gk/sim/track.h"

#include <algorithm>
#include <cmath>

namespace gk::sim {
namespace {

vec3 catmull_rom(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

struct Range {
    float a, b;
    bool contains(float s) const { return s >= a && s < b; }
};

} // namespace

Track make_greybox_track() {
    Track t;
    t.name = "Greybox Loop";

    // Closed loop of control points (metres): a climbing north straight with a
    // trick ramp, a sweeping east turn, a glide ramp over a chasm on the way
    // south, and a climbing return through sand, ice and mud.
    const std::vector<vec3> ctrl = {
        {0, 0, 0},      {0, 0, 50},     {0, 3, 100},    {6, 6, 150},    {30, 7, 185},  {70, 7, 200},
        {115, 5, 195},  {150, 3, 170},  {165, 2, 125},  {165, 2, 75},   {160, -10, 0}, {150, -12, -45},
        {120, -12, -80}, {75, -10, -95}, {30, -7, -85},  {5, -4, -60},   {-5, -1, -30},
    };
    const int nc = int(ctrl.size());

    // Dense sampling of the spline, then resampling by arc length.
    std::vector<vec3> dense;
    std::vector<float> dense_ctrl; // fractional control index of each dense point
    for (int i = 0; i < nc; ++i) {
        const vec3& p0 = ctrl[size_t((i - 1 + nc) % nc)];
        const vec3& p1 = ctrl[size_t(i)];
        const vec3& p2 = ctrl[size_t((i + 1) % nc)];
        const vec3& p3 = ctrl[size_t((i + 2) % nc)];
        for (int k = 0; k < 64; ++k) {
            dense.push_back(catmull_rom(p0, p1, p2, p3, float(k) / 64.0f));
            dense_ctrl.push_back(float(i) + float(k) / 64.0f);
        }
    }
    std::vector<float> dense_s(dense.size() + 1, 0.0f);
    for (size_t i = 1; i <= dense.size(); ++i)
        dense_s[i] = dense_s[i - 1] + glm::length(dense[i % dense.size()] - dense[i - 1]);
    const float total = dense_s.back();
    constexpr float kSpacing = 2.0f;
    const int n = int(std::round(total / kSpacing));
    const float step = total / float(n);

    std::vector<vec3> base(static_cast<size_t>(n));
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
    auto s_of_ctrl = [&](float c) {
        for (int i = 0; i < n; ++i)
            if (ctrl_at[size_t(i)] >= c) return float(i) * step;
        return total;
    };

    // Feature placement along the loop (distances in metres).
    const float s_trick = s_of_ctrl(1.0f) + 12.0f;      // trick ramp lip on the north straight
    const Range trick_ramp{s_trick - 10.0f, s_trick};
    const Range boost_pad_1{s_of_ctrl(1.0f) - 14.0f, s_of_ctrl(1.0f) - 6.0f};
    const Range boost_pad_2{s_of_ctrl(5.0f), s_of_ctrl(5.0f) + 8.0f};
    const float s_glide = s_of_ctrl(9.0f) + 4.0f;        // glide ramp start
    const Range glide_ramp{s_glide, s_glide + 24.0f};
    const Range gap{glide_ramp.b, glide_ramp.b + 62.0f};
    const Range no_respawn{glide_ramp.a - 40.0f, gap.b + 4.0f};
    const Range sand{s_of_ctrl(6.0f), s_of_ctrl(7.0f)};
    const Range ice{s_of_ctrl(11.0f) + 6.0f, s_of_ctrl(12.0f)};
    const Range gravel_inner{s_of_ctrl(3.0f), s_of_ctrl(5.0f)};
    const Range mud_outer{s_of_ctrl(13.0f), s_of_ctrl(15.0f)};

    auto height_offset = [&](float s) {
        float h = 0.0f;
        if (trick_ramp.contains(s)) h += 1.6f * smooth((s - trick_ramp.a) / (trick_ramp.b - trick_ramp.a));
        if (glide_ramp.contains(s)) h += 3.0f * smooth((s - glide_ramp.a) / (glide_ramp.b - glide_ramp.a));
        return h;
    };

    t.center.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        CenterSample& c = t.center[size_t(i)];
        c.s = float(i) * step;
        c.pos = base[size_t(i)] + vec3(0, height_offset(c.s), 0);
    }
    for (int i = 0; i < n; ++i) {
        CenterSample& c = t.center[size_t(i)];
        const vec3 next = base[size_t((i + 1) % n)], prev = base[size_t((i - 1 + n) % n)];
        c.tangent = glm::normalize(next - prev);
        c.right = glm::normalize(glm::cross(c.tangent, kUp));
        c.up = glm::normalize(glm::cross(c.right, c.tangent));
        c.half_width = 7.0f;
        c.has_floor = !gap.contains(c.s);
        c.respawn_ok = !no_respawn.contains(c.s);
    }
    t.length = total;

    // Lateral columns: wall, offroad, road (4 lanes), offroad, wall.
    struct Row {
        float lat[7];
        float strip_l, strip_r;
    };
    auto road_surface = [&](float s, int lane) {
        if (trick_ramp.contains(s)) return Surface::TrickRamp;
        if (glide_ramp.contains(s)) return Surface::GlideRamp;
        if ((boost_pad_1.contains(s) || boost_pad_2.contains(s)) && (lane == 1 || lane == 2)) return Surface::BoostPad;
        if (sand.contains(s)) return Surface::Sand;
        if (ice.contains(s)) return Surface::Ice;
        return Surface::Road;
    };
    auto strip_surface = [&](float s, bool right) {
        if (right && gravel_inner.contains(s)) return Surface::OffroadMedium;
        if (!right && mud_outer.contains(s)) return Surface::OffroadHeavy;
        return Surface::OffroadLight;
    };

    constexpr float kWallHeight = 1.5f;
    constexpr int kCols = 7; // lateral vertices per sample
    for (int i = 0; i < n; ++i) {
        const CenterSample& c = t.center[size_t(i)];
        const float hw = c.half_width;
        const float sl = mud_outer.contains(c.s) ? 9.0f : 5.0f;
        const float sr = gravel_inner.contains(c.s) ? 10.0f : 5.0f;
        const float lat[kCols] = {-(hw + sl), -hw, -hw * 0.5f, 0.0f, hw * 0.5f, hw, hw + sr};
        for (float l : lat) t.vertices.push_back(c.pos + c.right * l);
        // wall tops for the two outer columns
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
            if (col == 0) surf = strip_surface(s, false);
            else if (col == kCols - 2) surf = strip_surface(s, true);
            else surf = road_surface(s, col - 1);
            quad(V(i, col), V(i, col + 1), V(j, col + 1), V(j, col), surf);
        }
        // Inward-facing walls on both sides, except around the glide ramp.
        if (!no_respawn.contains(s) || s < glide_ramp.a) {
            quad(V(i, 0), V(j, 0), V(j, kCols), V(i, kCols), Surface::Wall);
            quad(V(j, kCols - 1), V(i, kCols - 1), V(i, kCols + 1), V(j, kCols + 1), Surface::Wall);
        }
    }

    // Out-of-bounds floor far below the track.
    const float floor_y = -32.0f;
    const uint32_t base_v = uint32_t(t.vertices.size());
    const float e = 1500.0f;
    t.vertices.push_back({-e, floor_y, -e});
    t.vertices.push_back({-e, floor_y, e});
    t.vertices.push_back({e, floor_y, e});
    t.vertices.push_back({e, floor_y, -e});
    quad(base_v, base_v + 1, base_v + 2, base_v + 3, Surface::OutOfBounds);
    t.kill_y = floor_y - 10.0f;

    // Item box rows: four boxes across the road at a few points per lap.
    for (float s_box : {s_of_ctrl(2.0f), s_of_ctrl(6.5f), s_of_ctrl(10.5f) + 20.0f, s_of_ctrl(14.0f)}) {
        int i = 0;
        while (i + 1 < n && t.center[size_t(i)].s < s_box) ++i;
        const CenterSample& c = t.center[size_t(i)];
        for (float lat : {-0.6f, -0.2f, 0.2f, 0.6f}) t.item_boxes.push_back(c.pos + c.right * (lat * c.half_width) + c.up * 1.0f);
    }

    // Checkpoint regions roughly every 40 m; region 0 starts at the line.
    const int per = std::max(1, int(40.0f / step));
    for (int i = 0; i < n; i += per) t.checkpoints.push_back(i);
    if (t.checkpoints.size() > 1 && n - t.checkpoints.back() < per / 2) t.checkpoints.pop_back();

    t.finalize();
    return t;
}

} // namespace gk::sim
