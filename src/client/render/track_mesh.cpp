#include "track_mesh.h"

#include "textures.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gk::render {
namespace {

uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int shift) { return uint32_t(std::min(255.0f, float((c >> shift) & 255) * f)) << shift; };
    return ch(0) | ch(8) | ch(16) | (c & 0xff000000u);
}

int checker(vec3 p, float cell) {
    return (int(std::floor(p.x / cell)) + int(std::floor(p.z / cell)) + int(std::floor(p.y / cell))) & 1;
}

uint32_t surface_color(const ThemeStyle& th, sim::Surface s) {
    using sim::Surface;
    switch (s) {
    case Surface::Road: return th.road;
    case Surface::OffroadLight: return th.offroad_light;
    case Surface::OffroadMedium: return th.offroad_medium;
    case Surface::OffroadHeavy: return th.offroad_heavy;
    case Surface::Sand: return th.sand;
    case Surface::Ice: return th.ice;
    case Surface::Water: return th.water;
    case Surface::BoostPad: return rgba(255, 170, 30);
    case Surface::GlideRamp: return rgba(60, 130, 255);
    case Surface::TrickRamp: return rgba(240, 220, 60);
    case Surface::Wall: return th.wall_a;
    case Surface::OutOfBounds: return th.below;
    case Surface::Count: break;
    }
    return rgba(255, 0, 255);
}

struct Rng {
    uint32_t seed;
    float operator()() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    }
};

uint32_t hash_name(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}

} // namespace

const ThemeStyle& theme_style(const std::string& theme) {
    using S = ThemeStyle::Scenery;
    static const std::pair<const char*, ThemeStyle> kThemes[] = {
        {"meadow", {rgba(112, 112, 120), rgba(98, 170, 70), rgba(150, 140, 110), rgba(110, 76, 44), rgba(222, 196, 130),
                    rgba(186, 228, 245), rgba(60, 120, 200), rgba(230, 230, 230), rgba(210, 40, 40), rgba(92, 70, 52),
                    rgba(50, 110, 170), {0.53f, 0.74f, 0.95f}, 0.0035f, 0.45f, S::Trees}},
        {"desert", {rgba(150, 120, 96), rgba(214, 180, 120), rgba(200, 160, 100), rgba(170, 120, 70), rgba(236, 206, 140),
                    rgba(186, 228, 245), rgba(60, 140, 190), rgba(240, 220, 170), rgba(200, 110, 60), rgba(150, 100, 60),
                    rgba(226, 190, 128), {0.95f, 0.80f, 0.55f}, 0.0030f, 0.5f, S::Cactus}},
        {"harbor", {rgba(120, 120, 128), rgba(110, 176, 90), rgba(196, 180, 130), rgba(120, 90, 60), rgba(232, 214, 160),
                    rgba(186, 228, 245), rgba(40, 140, 210), rgba(250, 250, 250), rgba(40, 90, 200), rgba(90, 80, 70),
                    rgba(30, 110, 190), {0.60f, 0.82f, 0.98f}, 0.0030f, 0.48f, S::Harbor}},
        {"snow", {rgba(120, 124, 140), rgba(236, 242, 250), rgba(220, 228, 240), rgba(200, 210, 228), rgba(222, 196, 130),
                  rgba(170, 220, 250), rgba(60, 120, 200), rgba(240, 240, 250), rgba(90, 120, 200), rgba(120, 130, 150),
                  rgba(220, 230, 245), {0.78f, 0.85f, 0.95f}, 0.0060f, 0.55f, S::Pines}},
        {"canyon", {rgba(126, 108, 100), rgba(196, 130, 80), rgba(176, 108, 66), rgba(140, 84, 50), rgba(226, 180, 120),
                    rgba(186, 228, 245), rgba(60, 120, 200), rgba(220, 150, 90), rgba(160, 80, 50), rgba(120, 70, 44),
                    rgba(150, 84, 52), {0.95f, 0.70f, 0.50f}, 0.0030f, 0.48f, S::Mesas}},
        {"volcano", {rgba(80, 74, 78), rgba(90, 70, 66), rgba(70, 54, 50), rgba(50, 40, 38), rgba(130, 100, 80),
                     rgba(186, 228, 245), rgba(60, 120, 200), rgba(60, 56, 60), rgba(230, 90, 30), rgba(40, 34, 34),
                     rgba(255, 90, 20), {0.35f, 0.18f, 0.16f}, 0.0070f, 0.40f, S::Lava}},
        {"sky", {rgba(170, 170, 200), rgba(240, 240, 255), rgba(220, 220, 245), rgba(200, 200, 230), rgba(222, 196, 130),
                 rgba(186, 228, 245), rgba(60, 120, 200), rgba(255, 220, 120), rgba(250, 250, 255), rgba(200, 200, 230),
                 rgba(120, 170, 240), {0.55f, 0.75f, 1.0f}, 0.0025f, 0.55f, S::Clouds}},
        {"night", {rgba(52, 52, 70), rgba(40, 90, 70), rgba(60, 60, 80), rgba(50, 40, 60), rgba(120, 110, 90),
                   rgba(120, 180, 230), rgba(30, 60, 140), rgba(255, 60, 200), rgba(60, 220, 255), rgba(30, 30, 40),
                   rgba(20, 20, 40), {0.06f, 0.06f, 0.16f}, 0.0040f, 0.55f, S::City}},
    };
    for (const auto& [name, style] : kThemes)
        if (theme == name) return style;
    return kThemes[0].second;
}

// Texture for a surface in a theme: offroad and the ground below take the
// theme's terrain.
uint32_t surface_texture(const std::string& theme, sim::Surface s) {
    using sim::Surface;
    auto terrain = [&](bool heavy) -> uint32_t {
        if (theme == "snow") return TexSnow;
        if (theme == "desert") return heavy ? TexDirt : TexSand;
        if (theme == "canyon") return heavy ? TexRock : TexDirt;
        if (theme == "volcano") return TexRock;
        if (theme == "sky") return TexCloud;
        return heavy ? TexDirt : TexGrass;
    };
    switch (s) {
    case Surface::Road: return TexAsphalt;
    case Surface::OffroadLight: return terrain(false);
    case Surface::OffroadMedium: return terrain(true);
    case Surface::OffroadHeavy: return terrain(true);
    case Surface::Sand: return TexSand;
    case Surface::Ice: return TexIce;
    case Surface::Water: return TexWater;
    case Surface::BoostPad: return TexArrows;
    case Surface::GlideRamp: return TexArrows;
    case Surface::TrickRamp: return TexStripes;
    case Surface::Wall: return TexPanel;
    case Surface::OutOfBounds: return terrain(false);
    case Surface::Count: break;
    }
    return TexWhite;
}

// What obstacles are made of, by theme: texture and colour.
std::pair<uint32_t, uint32_t> obstacle_look(const std::string& theme, bool pillar) {
    if (theme == "desert" || theme == "canyon") return {TexRock, rgba(176, 120, 82)};
    if (theme == "snow") return {TexIce, rgba(180, 220, 245)};
    if (theme == "volcano") return {TexPanel, rgba(90, 84, 90)};
    if (theme == "sky") return {TexPanel, rgba(236, 236, 248)};
    if (theme == "night") return {TexPanel, pillar ? rgba(80, 60, 140) : rgba(60, 200, 230)};
    if (pillar) return {TexRock, rgba(150, 150, 140)};
    return {TexWood, rgba(196, 150, 96)}; // crates
}

MeshData make_track_mesh(const sim::Track& t, bool skip_obstacles) {
    const ThemeStyle& th = theme_style(t.theme);
    MeshBuilder b;
    const sim::CenterSample& start = t.center.front();
    const int n = int(t.center.size());
    constexpr int kStride = 9; // vertices per centre sample (track_builder.cpp)
    const uint32_t grid_end = uint32_t(n * kStride);
    std::vector<int> obstacle_of(t.triangles.size(), -1);
    for (size_t o = 0; o < t.obstacles.size(); ++o)
        for (uint32_t i = 0; i < t.obstacles[o].tri_count; ++i) obstacle_of[t.obstacles[o].first_tri + i] = int(o);

    // Texture coordinates that follow the road: along it by distance and
    // across it by metres from the centre (or up a wall).
    auto road_uv = [&](uint32_t vi, bool wall, float tile, bool seam) {
        const int sample = int(vi / kStride), col = int(vi % kStride);
        const sim::CenterSample& c = t.center[size_t(sample)];
        const float s = (seam && sample == 0) ? t.length : c.s;
        const vec3 d = t.vertices[vi] - c.pos;
        if (wall) return vec2(s / tile, (col >= 7 ? -glm::dot(d, c.up) : 0.0f) / tile);
        return vec2(glm::dot(d, c.right) / tile, s / tile);
    };

    for (size_t ti = 0; ti < t.triangles.size(); ++ti) {
        const auto& tri = t.triangles[ti];
        const vec3 a = t.vertices[tri.a], c1 = t.vertices[tri.b], c2 = t.vertices[tri.c];
        const vec3 centroid = (a + c1 + c2) / 3.0f;
        if (obstacle_of[ti] >= 0) {
            if (skip_obstacles) continue; // drawn as props
            const auto& o = t.obstacles[size_t(obstacle_of[ti])];
            const auto [tex, color] = obstacle_look(t.theme, o.sides > 4);
            b.texture(tex, o.sides > 4 ? 2.0f : 2.0f * std::max(o.half.x, o.half.y));
            b.tri(a, c1, c2, color);
            continue;
        }
        uint32_t color = surface_color(th, tri.surface);
        if (tri.surface == sim::Surface::Wall) color = checker(centroid, 2.0f) ? th.wall_a : th.wall_b;
        const float along = glm::dot(centroid - start.pos, start.tangent);
        const float across = glm::dot(centroid - start.pos, start.right);
        if (tri.surface == sim::Surface::Road && std::abs(along) < 1.0f && std::abs(across) < start.half_width)
            color = (int(std::floor(across)) & 1) ? rgba(20, 20, 20) : rgba(240, 240, 240);
        b.texture(surface_texture(t.theme, tri.surface), tri.surface == sim::Surface::OutOfBounds ? 8.0f : 2.0f);

        const bool on_grid = tri.a < grid_end && tri.b < grid_end && tri.c < grid_end;
        if (!on_grid) {
            b.tri(a, c1, c2, color);
            continue;
        }
        const bool wall = tri.surface == sim::Surface::Wall;
        if (wall && skip_obstacles) continue; // painted walls (trackside.cpp)
        const bool seam = std::max({tri.a, tri.b, tri.c}) / kStride == uint32_t(n - 1) &&
                          std::min({tri.a, tri.b, tri.c}) / kStride == 0;
        const bool pad = tri.surface == sim::Surface::BoostPad || tri.surface == sim::Surface::GlideRamp;
        const float tile = pad ? 4.0f : tri.surface == sim::Surface::TrickRamp ? 3.0f : 2.0f;
        const vec2 ua = road_uv(tri.a, wall, tile, seam), ub = road_uv(tri.b, wall, tile, seam),
                   uc = road_uv(tri.c, wall, tile, seam);
        b.tri_uv(a, c1, c2, ua, ub, uc, color);
        if (wall) {
            b.tri_uv(a, c2, c1, ua, uc, ub, shade(color, 0.7f)); // outside face
        } else {
            const vec3 down(0.0f, -1.2f, 0.0f);
            b.texture(TexRock, 3.0f);
            b.tri(a + down, c2 + down, c1 + down, th.underside);
        }
    }
    return b.take();
}

// ---- trackside detail: kerbs, wall rails and posts, a start gantry,
// grandstands, flags and rolling hills.

// A quad wound so its normal points along `want`.
void facing_quad(MeshBuilder& b, vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 want, uint32_t color) {
    if (glm::dot(glm::cross(p1 - p0, p2 - p0), want) < 0.0f) b.quad(p0, p3, p2, p1, color);
    else b.quad(p0, p1, p2, p3, color);
}

// Which samples have a wall on each side, read back from the wall triangles.
std::pair<std::vector<bool>, std::vector<bool>> walls_of(const sim::Track& t) {
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    std::vector<bool> left(size_t(n), false), right(size_t(n), false);
    for (const auto& tri : t.triangles) {
        if (tri.surface != sim::Surface::Wall) continue;
        for (uint32_t vi : {tri.a, tri.b, tri.c}) {
            if (vi >= uint32_t(n * kStride)) break;
            const int col = int(vi % kStride), sample = int(vi / kStride);
            if (col == 7) left[size_t(sample)] = true;
            if (col == 8) right[size_t(sample)] = true;
        }
    }
    return {left, right};
}

void kerbs(MeshBuilder& b, const sim::Track& t) {
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    const uint32_t red = rgba(220, 50, 50), white = rgba(240, 240, 240);
    b.texture(TexWhite);
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const sim::CenterSample& c = t.center[size_t(i)];
        const sim::CenterSample& d = t.center[size_t(j)];
        if (!c.has_floor || !d.has_floor) continue;
        for (int side = 0; side < 2; ++side) {
            // Road edge and wall base on this side, at both samples.
            const int edge = side ? 5 : 1, base = side ? 6 : 0;
            const vec3 e0 = t.vertices[size_t(i * kStride + edge)], e1 = t.vertices[size_t(j * kStride + edge)];
            const vec3 b0 = t.vertices[size_t(i * kStride + base)], b1 = t.vertices[size_t(j * kStride + base)];
            const float verge = glm::length(b0 - e0);
            if (verge < 0.5f) continue;
            const float w = std::min(0.7f, verge - 0.2f);
            const vec3 out0 = glm::normalize(b0 - e0), out1 = glm::normalize(b1 - e1);
            const uint32_t col = ((i / 2) & 1) ? red : white;
            const vec3 lip = c.up * 0.07f;
            facing_quad(b, e0 + lip, e1 + d.up * 0.07f, e1 + out1 * w, e0 + out0 * w, c.up, col);
            facing_quad(b, e0, e1, e1 + d.up * 0.07f, e0 + lip, -out0, shade(col, 0.8f));
        }
    }
}

void wall_rails(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th) {
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    const auto [left, right] = walls_of(t);
    const uint32_t rail = th.wall_a, post = shade(th.wall_b, 0.85f);
    b.texture(TexWhite);
    for (int side = 0; side < 2; ++side) {
        const std::vector<bool>& has = side ? right : left;
        const int top = side ? 8 : 7, base = side ? 6 : 0;
        // Runs of consecutive walled samples, each swept as one tube.
        int start = -1;
        auto flush = [&](int from, int to) { // inclusive sample range, may wrap
            std::vector<vec3> path;
            for (int k = from;; k = (k + 1) % n) {
                path.push_back(t.vertices[size_t(k * kStride + top)] + t.center[size_t(k)].up * 0.1f);
                if (k == to) break;
            }
            if (path.size() >= 2) b.sweep(path, 0.17f, rail, 8, true);
            int k = from, count = 0;
            for (;; k = (k + 1) % n, ++count) {
                if (count % 5 == 0) {
                    const vec3 foot = t.vertices[size_t(k * kStride + base)], head = t.vertices[size_t(k * kStride + top)];
                    const vec3 out = side ? t.center[size_t(k)].right : -t.center[size_t(k)].right;
                    b.tube(foot + out * 0.1f, head + out * 0.1f + t.center[size_t(k)].up * 0.25f, 0.12f, 0.1f, post, 8, true);
                }
                if (k == to) break;
            }
        };
        // Find a sample without a wall to start scanning from, so runs don't split at 0.
        int origin = 0;
        while (origin < n && has[size_t(origin)]) ++origin;
        if (origin == n) { // walled all the way round
            flush(0, n - 1);
            continue;
        }
        for (int step = 0; step < n; ++step) {
            const int k = (origin + step) % n;
            if (has[size_t(k)] && start < 0) start = k;
            if (!has[size_t(k)] && start >= 0) {
                flush(start, (k - 1 + n) % n);
                start = -1;
            }
        }
        if (start >= 0) flush(start, (origin - 1 + n) % n);
    }
}

mat4 frame_at(const sim::CenterSample& c, vec3 pos) {
    return mat4(vec4(c.right, 0.0f), vec4(c.up, 0.0f), vec4(c.tangent, 0.0f), vec4(pos, 1.0f));
}

void start_gantry(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th) {
    constexpr int kStride = 9;
    const sim::CenterSample& c = t.center.front();
    const vec3 l = t.vertices[0], r = t.vertices[kStride - 3];
    const float half = 0.5f * glm::length(r - l) + 0.9f;
    const float h = 6.5f;
    MeshBuilder g;
    g.texture(TexWhite);
    const uint32_t steel = rgba(200, 204, 212);
    for (float x : {-half, half}) {
        g.tube({x, -0.5f, 0.0f}, {x, h, 0.0f}, 0.28f, 0.22f, steel, 12, true);
        g.rounded_box({x, 0.2f, 0.0f}, {0.6f, 0.2f, 0.6f}, 0.08f, shade(steel, 0.8f), 12, 8);
    }
    g.rounded_box({0.0f, h, 0.0f}, {half + 0.2f, 0.32f, 0.5f}, 0.12f, steel, 12, 8);
    // Banner under the beam: a checker band, seen from both directions.
    const float by = h - 0.35f, bh = 1.4f;
    const int cells = std::max(2, int(half * 2.0f / 1.2f));
    const float cw = half * 2.0f / float(cells);
    for (int i = 0; i < cells; ++i) {
        const float x0 = -half + float(i) * cw, x1 = x0 + cw;
        for (int row = 0; row < 2; ++row) {
            const float y0 = by - bh + float(row) * bh * 0.5f, y1 = y0 + bh * 0.5f;
            const uint32_t col = ((i + row) & 1) ? rgba(30, 30, 34) : rgba(245, 245, 245);
            g.quad({x0, y0, -0.02f}, {x0, y1, -0.02f}, {x1, y1, -0.02f}, {x1, y0, -0.02f}, col);
            g.quad({x1, y0, 0.02f}, {x1, y1, 0.02f}, {x0, y1, 0.02f}, {x0, y0, 0.02f}, col);
        }
    }
    // Lights along the beam.
    for (int i = 0; i <= 4; ++i) {
        const float x = -half + (2.0f * half) * float(i) / 4.0f;
        g.sphere({x, h + 0.45f, 0.0f}, 0.22f, i == 2 ? rgba(255, 90, 60) : th.wall_b, 10, 7);
    }
    b.append(g.take(), frame_at(c, c.pos));
}

void grandstands(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th, Rng& rnd) {
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    // A stand on each side of the straight after the start, where there is room.
    for (int side = -1; side <= 1; side += 2) {
        const int at = 6 % n;
        const sim::CenterSample& c = t.center[size_t(at)];
        const vec3 base_v = t.vertices[size_t(at * kStride + (side > 0 ? 6 : 0))];
        const float off = glm::length(base_v - c.pos) + 3.0f;
        vec3 base = c.pos + c.right * float(side) * off;
        bool clear = true;
        for (int j = 0; j < n && clear; j += 3) {
            if (std::abs(j - at) < 12 || std::abs(j - at) > n - 12) continue;
            clear = glm::length(vec2(t.center[size_t(j)].pos.x - base.x, t.center[size_t(j)].pos.z - base.z)) > t.center[size_t(j)].half_width + 10.0f;
        }
        if (!clear) continue;
        MeshBuilder g;
        g.texture(TexPanel, 2.0f);
        const float len = 14.0f;
        const uint32_t frame = rgba(150, 150, 160);
        // Four tiers stepping up and back, each a bench row in a crowd colour.
        for (int tier = 0; tier < 4; ++tier) {
            const float d = 1.0f + 1.6f * float(tier), y = 0.8f * float(tier);
            g.rounded_box({float(side) * (d + 0.8f), y + 0.5f, 0.0f}, {0.8f, 0.5f, len * 0.5f}, 0.08f, tier % 2 ? shade(th.wall_a, 0.9f) : th.wall_b, 10, 6);
            // Spectators: a row of little heads.
            for (float z = -len * 0.5f + 0.6f; z < len * 0.5f; z += 0.9f) {
                if (rnd() < 0.2f) continue;
                const uint32_t shirt = rnd() < 0.5f ? th.wall_b : rgba(uint8_t(80 + rnd() * 170), uint8_t(80 + rnd() * 170), uint8_t(80 + rnd() * 170));
                g.capsule({float(side) * (d + 0.8f), y + 1.0f, z}, {float(side) * (d + 0.8f), y + 1.45f, z}, 0.25f, shirt, 8, 3);
                g.sphere({float(side) * (d + 0.8f), y + 1.75f, z}, 0.22f, rgba(uint8_t(170 + rnd() * 70), uint8_t(120 + rnd() * 60), uint8_t(80 + rnd() * 50)), 10, 7);
            }
        }
        // Back wall and a roof on posts.
        g.rounded_box({float(side) * 8.2f, 2.0f, 0.0f}, {0.2f, 2.0f, len * 0.5f + 0.5f}, 0.05f, frame, 8, 6);
        for (float z : {-len * 0.5f, len * 0.5f}) g.tube({float(side) * 1.4f, 0.0f, z}, {float(side) * 1.4f, 5.2f, z}, 0.12f, 0.12f, frame, 8, true);
        g.rounded_box({float(side) * 4.8f, 5.4f, 0.0f}, {3.8f, 0.15f, len * 0.5f + 0.8f}, 0.05f, th.wall_a, 8, 6);
        b.append(g.take(), frame_at(c, base));
    }
}

void flags(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th, const std::vector<bool>& left, const std::vector<bool>& right) {
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    b.texture(TexWhite);
    for (int i = 3; i < n; i += 14) {
        const sim::CenterSample& c = t.center[size_t(i)];
        if (!c.has_floor) continue;
        const int side = (i / 14) % 2 ? 1 : -1;
        const bool walled = side > 0 ? right[size_t(i)] : left[size_t(i)];
        const vec3 base_v = t.vertices[size_t(i * kStride + (side > 0 ? 6 : 0))];
        const vec3 foot = base_v + c.right * float(side) * (walled ? 0.9f : 0.3f);
        const float h = 5.0f;
        b.tube(foot - c.up * 0.5f, foot + c.up * h, 0.09f, 0.07f, rgba(220, 220, 228), 8, true);
        b.sphere(foot + c.up * (h + 0.1f), 0.14f, rgba(250, 210, 80), 8, 6);
        const vec3 top = foot + c.up * (h - 0.1f);
        const uint32_t col = (i / 14) % 3 == 0 ? th.wall_b : (i / 14) % 3 == 1 ? th.wall_a : rgba(250, 210, 80);
        const vec3 tip = top + c.tangent * 1.6f - c.up * 0.6f;
        b.tri(top, top - c.up * 1.2f, tip, col);
        b.tri(top, tip, top - c.up * 1.2f, col);
    }
}

void hills(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th, Rng& rnd, vec3 mid, float span, float floor_y) {
    using S = ThemeStyle::Scenery;
    if (th.scenery == S::City || th.scenery == S::Clouds) return;
    const uint32_t tex = th.scenery == S::Pines ? TexSnow : th.scenery == S::Cactus ? TexSand : th.scenery == S::Mesas ? TexRock : th.scenery == S::Lava ? TexRock : TexGrass;
    const uint32_t col = th.scenery == S::Pines ? rgba(236, 242, 250) : th.scenery == S::Cactus ? rgba(222, 190, 128) : th.scenery == S::Mesas ? rgba(196, 130, 80) : th.scenery == S::Lava ? rgba(60, 50, 52) : th.scenery == S::Harbor ? rgba(110, 176, 90) : rgba(96, 168, 70);
    b.texture(tex, 6.0f);
    for (int i = 0; i < 70; ++i) {
        const float a = rnd() * 2.0f * kPi;
        const float d = span * 0.55f + 30.0f + rnd() * 160.0f;
        const vec3 p(mid.x + std::cos(a) * d, floor_y, mid.z + std::sin(a) * d);
        const float r = 18.0f + rnd() * 45.0f, h = 6.0f + rnd() * 22.0f;
        bool clear = true;
        for (size_t j = 0; j < t.center.size() && clear; j += 3) {
            const sim::CenterSample& c = t.center[j];
            clear = glm::length(vec2(c.pos.x - p.x, c.pos.z - p.z)) > c.half_width + r * 0.9f || c.pos.y > floor_y + h + 4.0f;
        }
        if (!clear) continue;
        std::vector<vec2> prof{{r, -2.0f}, {r * 0.92f, h * 0.2f}, {r * 0.7f, h * 0.55f}, {r * 0.38f, h * 0.85f}, {0.0f, h}};
        b.lathe(p, kUp, prof, 18, col, 8.0f, 2.0f);
    }
}

// Lamp posts on the walls, leaning over the road, and billboards on the
// outside of the walls along straights, facing the road.
void wall_props(const sim::Track& t, const ThemeStyle& th, const std::vector<bool>& left, const std::vector<bool>& right, Rng& rnd,
                std::vector<PropPlacement>& out) {
    using S = ThemeStyle::Scenery;
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    const int lamp_every = th.scenery == S::City ? 12 : 20;
    auto oriented = [&](const sim::CenterSample& c, vec3 pos, vec3 x_axis, float scale) {
        const vec3 x = glm::normalize(x_axis - c.up * glm::dot(x_axis, c.up));
        const vec3 y = c.up, z = glm::cross(x, y);
        return mat4(vec4(x * scale, 0.0f), vec4(y * scale, 0.0f), vec4(z * scale, 0.0f), vec4(pos, 1.0f));
    };
    for (int i = 9; i < n - 6; i += lamp_every) {
        const int side = (i / lamp_every) % 2 ? 1 : -1;
        if (!(side > 0 ? right[size_t(i)] : left[size_t(i)])) continue;
        const sim::CenterSample& c = t.center[size_t(i)];
        const vec3 top = t.vertices[size_t(i * kStride + (side > 0 ? 8 : 7))];
        const vec3 outward = c.right * float(side);
        out.push_back({"lamp_post", oriented(c, top + outward * 0.35f - c.up * 0.05f, -outward, 0.9f)});
    }
    // Billboards where the road runs straight for a while.
    for (int i = 30; i < n - 30; i += 70) {
        const sim::CenterSample& c = t.center[size_t(i)];
        const float bend = glm::dot(t.center[size_t(i - 8)].tangent, t.center[size_t(i + 8)].tangent);
        if (bend < 0.97f) continue;
        const int side = rnd() < 0.5f ? 1 : -1;
        if (!(side > 0 ? right[size_t(i)] : left[size_t(i)])) continue;
        const vec3 top = t.vertices[size_t(i * kStride + (side > 0 ? 8 : 7))];
        const vec3 outward = c.right * float(side);
        const vec3 pos = top + outward * 1.6f - c.up * 0.8f;
        bool clear = true; // not over another part of the track
        for (int j = 0; j < n && clear; j += 3) {
            const int along = std::abs(j - i);
            if (std::min(along, n - along) < 12) continue;
            clear = glm::distance(t.center[size_t(j)].pos, pos) > t.center[size_t(j)].half_width + 8.0f;
        }
        if (!clear) continue;
        // The board's front is +Z: turn it to face the road.
        const vec3 z = -outward;
        const vec3 x = glm::normalize(glm::cross(c.up, z));
        const char* kind = int(rnd() * 3.0f) == 0 ? "billboard" : rnd() < 0.5f ? "billboard_2" : "billboard_3";
        out.push_back({kind, mat4(vec4(x, 0.0f), vec4(c.up, 0.0f), vec4(glm::cross(x, c.up), 0.0f), vec4(pos, 1.0f))});
    }
}

// Small islands just outside the walls, at road height, each with a few of
// the theme's props, so the roadside is never empty.
void roadside_islands(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th, Rng& rnd, std::vector<PropPlacement>& out) {
    using S = ThemeStyle::Scenery;
    if (th.scenery == S::City) return;
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    auto place = [&](const char* name, vec3 pos, float yaw, float scale) {
        out.push_back({name, glm::scale(glm::rotate(glm::translate(mat4(1.0f), pos), yaw, kUp), vec3(scale))});
    };
    for (int i = 12; i < n - 6; i += 9) {
        if (rnd() < 0.25f) continue;
        const sim::CenterSample& c = t.center[size_t(i)];
        if (!c.has_floor) continue;
        const float side = rnd() < 0.5f ? -1.0f : 1.0f;
        const vec3 outward = c.right * side;
        const vec3 wall = t.vertices[size_t(i * kStride + (side > 0 ? 6 : 0))];
        const float r = 3.0f + rnd() * 2.5f;
        vec3 centre = wall + outward * (r + 1.2f);
        centre.y = c.pos.y - 0.7f;
        // Not over or under any other part of the track.
        bool clear = true;
        for (int j = 0; j < n && clear; j += 2) {
            const int along = std::abs(j - i);
            if (std::min(along, n - along) < 10) continue;
            const sim::CenterSample& o = t.center[size_t(j)];
            clear = glm::length(vec2(o.pos.x - centre.x, o.pos.z - centre.z)) > o.half_width + r + 5.0f || std::abs(o.pos.y - centre.y) > 14.0f;
        }
        if (!clear) continue;
        const float yaw = rnd() * 6.28f;
        if (th.scenery == S::Clouds) {
            place("cloud", centre - vec3(0, 2.5f, 0), yaw, 0.35f + rnd() * 0.2f);
            continue;
        }
        // The island: a flat top in the theme's ground and a rocky underside.
        b.texture(surface_texture(t.theme, sim::Surface::OffroadLight), 3.0f);
        b.lathe(centre, kUp, {{r, -0.25f}, {r * 0.98f, 0.1f}, {r * 0.7f, 0.28f}, {0.0f, 0.32f}}, 18, th.offroad_light, 2.0f, 1.0f);
        b.texture(TexRock, 2.5f);
        b.lathe(centre, -kUp, {{0.0f, 4.5f + r}, {r * 0.45f, 2.5f + r * 0.4f}, {r * 0.85f, 0.9f}, {r, 0.25f}}, 18, th.underside, 2.0f, 1.0f);
        const vec3 top = centre + vec3(0, 0.25f, 0);
        auto spot = [&](float spread) {
            const float a = rnd() * 6.28f, d = r * spread * std::sqrt(rnd());
            return top + vec3(std::cos(a) * d, 0.0f, std::sin(a) * d);
        };
        switch (th.scenery) {
        case S::Trees:
            place(rnd() < 0.15f ? "tree_autumn" : rnd() < 0.5f ? "tree_round" : "tree_round_2", spot(0.3f), yaw, 0.65f + rnd() * 0.35f);
            if (rnd() < 0.5f) place("tree_round_2", spot(0.6f), yaw, 0.5f + rnd() * 0.3f);
            for (int k = 0; k < 3; ++k) {
                const float roll = rnd();
                place(roll < 0.4f ? "bush" : roll < 0.7f ? "flower_pink" : roll < 0.9f ? "flower_yellow" : "rock", spot(0.85f), rnd() * 6.28f,
                      roll < 0.4f ? 0.8f + rnd() * 0.4f : roll < 0.9f ? 1.8f + rnd() : 0.6f);
            }
            if (rnd() < 0.2f) place(rnd() < 0.5f ? "tire_stack" : "cone", spot(0.7f), rnd() * 6.28f, 1.2f);
            break;
        case S::Harbor:
            place("tree_palm", spot(0.3f), yaw, 0.9f + rnd() * 0.3f);
            if (rnd() < 0.6f) place(rnd() < 0.5f ? "barrel" : "crate", spot(0.8f), rnd() * 6.28f, 1.0f);
            if (rnd() < 0.4f) place("barrel_red", spot(0.8f), rnd() * 6.28f, 1.0f);
            break;
        case S::Cactus:
            place("cactus", spot(0.4f), yaw, 0.9f + rnd() * 0.5f);
            if (rnd() < 0.6f) place(rnd() < 0.5f ? "rock_red" : "barrel_red", spot(0.8f), rnd() * 6.28f, 0.8f + rnd() * 0.4f);
            if (rnd() < 0.25f) place("dead_tree", spot(0.6f), rnd() * 6.28f, 0.9f);
            break;
        case S::Pines:
            place("tree_pine_snow", spot(0.35f), yaw, 0.7f + rnd() * 0.3f);
            if (rnd() < 0.5f) place("tree_pine_snow", spot(0.7f), yaw, 0.5f + rnd() * 0.2f);
            if (rnd() < 0.35f) place(rnd() < 0.5f ? "snowman" : "crystal", spot(0.75f), rnd() * 6.28f, rnd() < 0.5f ? 0.8f : 1.0f);
            break;
        case S::Mesas:
            place(rnd() < 0.6f ? "rock_red" : "boulder", spot(0.4f), yaw, 1.0f + rnd() * 0.8f);
            if (rnd() < 0.6f) place("cactus", spot(0.7f), rnd() * 6.28f, 0.9f + rnd() * 0.4f);
            break;
        case S::Lava:
            place("lava_rock", spot(0.3f), yaw, 0.5f + rnd() * 0.4f);
            if (rnd() < 0.4f) place("dead_tree", spot(0.7f), rnd() * 6.28f, 0.9f);
            break;
        default: break;
        }
    }
}

MeshData make_scenery_mesh(const sim::Track& t, std::vector<PropPlacement>* props) {
    using S = ThemeStyle::Scenery;
    const ThemeStyle& th = theme_style(t.theme);
    MeshBuilder b;
    MeshData tree = make_tree_mesh();
    Rng rnd{hash_name(t.id) | 1u};
    // A painted prop, turned about the vertical and scaled.
    auto place = [&](const char* name, vec3 pos, float yaw, vec3 scale) {
        props->push_back({name, glm::scale(glm::rotate(glm::translate(mat4(1.0f), pos), yaw, kUp), scale)});
    };
    auto pick = [&](std::initializer_list<const char*> names) { return names.begin()[std::min(int(rnd() * float(names.size())), int(names.size()) - 1)]; };
    vec3 lo(1e9f), hi(-1e9f);
    for (const auto& c : t.center) lo = glm::min(lo, c.pos), hi = glm::max(hi, c.pos);
    const vec3 mid = 0.5f * (lo + hi);
    const float span = std::max(hi.x - lo.x, hi.z - lo.z);
    float floor_y = lo.y - 20.0f;
    for (const auto& v : t.vertices) floor_y = std::min(floor_y, v.y);

    // Columns under bridges: where the road passes over another part of the
    // track, posts down to the ground on both sides, kept off the road below.
    const int n = int(t.center.size());
    for (int i = 0; i < n; i += 5) {
        const sim::CenterSample& c = t.center[size_t(i)];
        bool over_road = false;
        for (int j = 0; j < n && !over_road; ++j) {
            const int along = std::abs(i - j);
            if (std::min(along, n - along) < 20) continue;
            const sim::CenterSample& o = t.center[size_t(j)];
            over_road = o.pos.y < c.pos.y - 6.0f &&
                        glm::length(vec2(o.pos.x - c.pos.x, o.pos.z - c.pos.z)) < c.half_width + o.half_width + 12.0f;
        }
        if (!over_road || !c.has_floor) continue;
        for (float side : {-1.0f, 1.0f}) {
            const vec3 top = c.pos + c.right * side * (c.half_width + 2.0f) - vec3(0, 1.2f, 0);
            bool clear = true;
            for (int j = 0; j < n && clear; ++j) {
                const sim::CenterSample& o = t.center[size_t(j)];
                clear = o.pos.y > top.y - 1.0f ||
                        glm::length(vec2(o.pos.x - top.x, o.pos.z - top.z)) > o.half_width + 7.0f;
            }
            if (!clear) continue;
            const float h = top.y - floor_y;
            b.texture(t.theme == "night" || t.theme == "volcano" || t.theme == "sky" ? TexPanel : TexRock, 2.0f);
            b.box(vec3(top.x, floor_y + h * 0.5f, top.z), vec3(0.8f, h * 0.5f, 0.8f), th.wall_b);
        }
    }

    // Props beside the track, away from the road.
    for (size_t i = 0; i < t.center.size(); i += 6) {
        const sim::CenterSample& c = t.center[i];
        for (float side : {-1.0f, 1.0f}) {
            if (rnd() < 0.3f) continue;
            const float dist = c.half_width + 14.0f + rnd() * 30.0f;
            vec3 base = c.pos + c.right * side * dist;
            // Keep clear of other parts of the track.
            bool clear = true;
            for (size_t j = 0; j < t.center.size() && clear; j += 4)
                clear = glm::length(vec2(t.center[j].pos.x - base.x, t.center[j].pos.z - base.z)) > t.center[j].half_width + 12.0f;
            if (!clear) continue;
            switch (th.scenery) {
            case S::Trees: {
                base.y -= 6.0f + rnd() * 14.0f;
                const float r = 3.0f + rnd() * 4.0f;
                b.texture(TexDirt, 2.0f);
                b.cone(base, r, -3.5f - rnd() * 3.0f, 10, rgba(110, 82, 58));
                b.texture(TexGrass, 2.0f);
                b.cone(base, r, 0.4f, 10, rgba(96, 168, 70));
                const int trees = 1 + int(rnd() * 3.0f);
                for (int k = 0; k < trees; ++k) {
                    const vec3 off(rnd() * r - r * 0.5f, 0.2f, rnd() * r - r * 0.5f);
                    if (props) {
                        const char* kind = rnd() < 0.15f ? "tree_autumn" : pick({"tree_round", "tree_round_2"});
                        place(kind, base + off * 0.8f + vec3(0, 0.15f, 0), rnd() * 6.28f, vec3(0.55f + rnd() * 0.4f));
                    } else {
                        b.append(tree, glm::scale(glm::translate(mat4(1.0f), base + off), vec3(0.8f + rnd() * 0.6f)));
                    }
                }
                if (props) { // undergrowth round the edge of the island
                    for (int k = 0; k < 4; ++k) {
                        const float a = rnd() * 6.28f, d = r * (0.55f + 0.3f * rnd());
                        const vec3 p = base + vec3(std::cos(a) * d, 0.25f, std::sin(a) * d);
                        const float roll = rnd();
                        place(roll < 0.4f ? "bush" : roll < 0.7f ? "flower_pink" : roll < 0.9f ? "flower_yellow" : "rock", p, rnd() * 6.28f,
                              vec3(roll < 0.4f ? 0.7f + rnd() * 0.5f : roll < 0.9f ? 1.6f + rnd() : 0.5f + rnd() * 0.4f));
                    }
                }
                break;
            }
            case S::Cactus: {
                base.y = floor_y;
                const float h = 4.0f + rnd() * 5.0f;
                const uint32_t g = rgba(70, 150, 80);
                if (props) {
                    place("cactus", base + vec3(0, 0.8f, 0), rnd() * 6.28f, vec3(h / 3.6f));
                    if (rnd() < 0.4f) place(pick({"rock_red", "boulder"}), base + vec3(2.5f, 0.6f, 1.0f), rnd() * 6.28f, vec3(0.8f + rnd()));
                    if (rnd() < 0.2f) place("dead_tree", base + vec3(-2.0f, 0.8f, -1.5f), rnd() * 6.28f, vec3(1.2f));
                } else {
                    b.texture(TexBark, 1.0f);
                    b.capsule(base, base + vec3(0, h, 0), 0.5f, g, 12, 4);
                    b.sweep({base + vec3(0.3f, h * 0.45f, 0), base + vec3(1.0f, h * 0.5f, 0), base + vec3(1.2f, h * 0.9f, 0)}, 0.3f, g, 10);
                    b.sweep({base + vec3(-0.3f, h * 0.6f, 0), base + vec3(-0.9f, h * 0.65f, 0), base + vec3(-1.0f, h * 0.95f, 0)}, 0.26f, g, 10);
                }
                b.texture(TexSand, 2.0f);
                b.cone(base, 3.0f + rnd() * 4.0f, 1.2f, 10, rgba(230, 196, 132));
                break;
            }
            case S::Harbor: {
                base.y = floor_y + 0.2f;
                if (props && rnd() < 0.55f) {
                    const float roll = rnd();
                    if (roll < 0.55f) place("boat", base, rnd() * 6.28f, vec3(1.0f + rnd() * 0.4f));
                    else if (roll < 0.8f) { // a sandy islet with a palm
                        b.texture(TexSand, 2.0f);
                        b.lathe(base - vec3(0, 0.6f, 0), kUp, {{0.0f, 0.0f}, {4.0f, 0.0f}, {3.2f, 0.9f}, {0.0f, 1.3f}}, 16, rgba(236, 210, 150), 3.0f, 1.0f);
                        place("tree_palm", base + vec3(0, 0.6f, 0), rnd() * 6.28f, vec3(1.0f + rnd() * 0.3f));
                    } else {
                        b.texture(TexRock, 2.0f);
                        b.lathe(base - vec3(0, 0.6f, 0), kUp, {{0.0f, 0.0f}, {4.5f, 0.0f}, {3.5f, 1.5f}, {0.0f, 2.0f}}, 16, rgba(120, 118, 112), 3.0f, 1.0f);
                        place("tower", base + vec3(0, 1.4f, 0), rnd() * 6.28f, vec3(0.9f));
                    }
                } else if (rnd() < 0.5f) { // a little boat
                    const float yaw = rnd() * 6.28f;
                    const mat4 m = glm::rotate(glm::translate(mat4(1.0f), base), yaw, kUp);
                    MeshBuilder boat;
                    boat.texture(TexWood, 1.5f);
                    boat.box({0, 0.5f, 0}, {1.2f, 0.5f, 3.0f}, rgba(240, 240, 240));
                    boat.box({0, 1.4f, -0.5f}, {0.8f, 0.5f, 1.0f}, rgba(220, 60, 50));
                    boat.box({0, 3.0f, 0.8f}, {0.08f, 2.0f, 0.08f}, rgba(90, 70, 50));
                    b.append(boat.take(), m);
                } else { // buoy
                    b.texture(TexStripes, 1.0f);
                    b.lathe(base, kUp, {{0.0f, 0.0f}, {0.8f, 0.0f}, {0.7f, 0.9f}, {0.3f, 1.6f}, {0.12f, 2.2f}, {0.0f, 2.3f}}, 14, rgba(250, 120, 40), 2.0f, 1.0f);
                }
                break;
            }
            case S::Pines: {
                base.y -= 4.0f + rnd() * 10.0f;
                b.texture(TexRock, 2.0f);
                b.cone(base, 5.0f + rnd() * 3.0f, -4.0f, 10, rgba(120, 120, 130));
                b.texture(TexSnow, 2.0f);
                b.cone(base, 5.0f, 0.5f, 10, rgba(240, 245, 252));
                for (int k = 0; k < 2 && props; ++k) {
                    const vec3 p = base + vec3(rnd() * 4.0f - 2.0f, 0.3f, rnd() * 4.0f - 2.0f);
                    place("tree_pine_snow", p, rnd() * 6.28f, vec3(0.7f + rnd() * 0.35f));
                }
                if (props) {
                    const float roll = rnd();
                    const vec3 p = base + vec3(rnd() * 5.0f - 2.5f, 0.35f, rnd() * 5.0f - 2.5f);
                    if (roll < 0.25f) place("snowman", p, rnd() * 6.28f, vec3(0.9f));
                    else if (roll < 0.5f) place("crystal", p, rnd() * 6.28f, vec3(1.0f + rnd() * 0.6f));
                    else if (roll < 0.7f) place("rock", p, rnd() * 6.28f, vec3(0.8f + rnd() * 0.5f));
                }
                for (int k = 0; k < 2 && !props; ++k) {
                    const vec3 p = base + vec3(rnd() * 4.0f - 2.0f, 0.4f, rnd() * 4.0f - 2.0f);
                    b.texture(TexBark, 1.0f);
                    b.tube(p - vec3(0, 0.4f, 0), p + vec3(0, 1.0f, 0), 0.22f, 0.16f, rgba(90, 60, 40), 10, true);
                    b.texture(TexLeaves, 1.0f);
                    b.lathe(p, kUp, {{0.0f, 0.0f}, {1.6f, 0.1f}, {1.1f, 1.6f}, {0.0f, 3.0f}}, 14, rgba(40, 100, 70), 3.0f, 1.0f);
                    b.lathe(p + vec3(0, 2.0f, 0), kUp, {{0.0f, 0.0f}, {1.2f, 0.1f}, {0.8f, 1.3f}, {0.0f, 2.6f}}, 14, rgba(46, 112, 78), 3.0f, 1.0f);
                    b.texture(TexSnow, 1.0f);
                    b.lathe(p + vec3(0, 3.8f, 0), kUp, {{0.0f, 0.0f}, {0.7f, 0.1f}, {0.4f, 0.8f}, {0.0f, 1.4f}}, 12, rgba(240, 245, 252), 2.0f, 1.0f);
                }
                break;
            }
            case S::Mesas: {
                if (rnd() < 0.5f) break; // mesas are big: fewer of them
                base.y = floor_y;
                const float h = (c.pos.y - floor_y) + 6.0f + rnd() * 25.0f;
                const float r = 6.0f + rnd() * 9.0f;
                if (props) {
                    place("mesa", base, rnd() * 6.28f, vec3(r, h, r));
                    for (int k = 0; k < 3; ++k) {
                        const float a = rnd() * 6.28f;
                        place(pick({"rock_red", "boulder", "rock_red"}), base + vec3(std::cos(a), 0.0f, std::sin(a)) * (r * 1.4f + 2.0f), rnd() * 6.28f, vec3(1.0f + rnd() * 1.5f));
                    }
                    if (rnd() < 0.4f) place("cactus", base + vec3(r * 1.6f + 3.0f, 0.0f, 0.0f), rnd() * 6.28f, vec3(1.3f));
                    break;
                }
                b.texture(TexRock, 4.0f);
                // A flat-topped butte: a wide skirt, two ledges and a cap.
                std::vector<vec2> prof{{r * 1.3f, 0.0f}, {r * 1.05f, h * 0.12f}, {r * 0.95f, h * 0.4f}, {r * 0.8f, h * 0.44f},
                                       {r * 0.76f, h * 0.72f}, {r * 0.62f, h * 0.76f}, {r * 0.58f, h * 0.98f}, {r * 0.5f, h}, {0.0f, h}};
                b.lathe(base, kUp, prof, 16, rgba(200, 120, 74), 4.0f, 2.0f);
                b.texture(TexSand, 3.0f);
                b.lathe(base + vec3(0, h + 0.05f, 0), kUp, {{r * 0.5f, 0.0f}, {0.0f, 0.1f}}, 16, rgba(222, 160, 104));
                break;
            }
            case S::Lava: {
                base.y = floor_y;
                const float h = (c.pos.y - floor_y) + 2.0f + rnd() * 14.0f;
                if (props) {
                    const float r = 2.5f + rnd() * 3.0f;
                    place("lava_rock", base - vec3(0, 0.2f * h, 0), rnd() * 6.28f, vec3(r / 2.4f, h / 2.6f, r / 2.2f));
                    if (rnd() < 0.3f) place("dead_tree", base + vec3(0, h * 0.95f, 0), rnd() * 6.28f, vec3(1.0f));
                    break;
                }
                b.texture(TexRock, 2.0f);
                b.cone(base, 2.5f + rnd() * 3.0f, h, 8, rgba(46, 38, 40));
                break;
            }
            case S::Clouds: {
                base.y = c.pos.y - 8.0f - rnd() * 16.0f;
                if (props) {
                    place("cloud", base, rnd() * 6.28f, vec3(0.7f + rnd() * 0.5f));
                    break;
                }
                b.texture(TexCloud, 3.0f);
                for (int k = 0; k < 4; ++k)
                    b.sphere(base + vec3(rnd() * 10.0f - 5.0f, rnd() * 2.0f, rnd() * 10.0f - 5.0f), 3.0f + rnd() * 4.0f,
                             rgba(250, 250, 255), 14, 10);
                break;
            }
            case S::City: {
                base.y = floor_y;
                const float h = (c.pos.y - floor_y) + 8.0f + rnd() * 40.0f;
                const float w = 4.0f + rnd() * 5.0f;
                if (props) { // stacked blocks so the windows keep their size
                    const char* kind = pick({"building", "building_2", "building_3"});
                    const float yaw = std::atan2(c.pos.x - base.x, c.pos.z - base.z);
                    const int floors = std::max(1, int(std::ceil(h / (2.0f * w))));
                    for (int k = 0; k < floors; ++k) place(kind, base + vec3(0, float(k) * 2.0f * w, 0), yaw, vec3(2.0f * w));
                    break;
                }
                b.texture(TexPanel, 4.0f);
                b.box(base + vec3(0, h * 0.5f, 0), vec3(w, h * 0.5f, w), rgba(30, 30, 46));
                // Lit windows on the side facing the track.
                const vec3 face = glm::normalize(vec3(c.pos.x - base.x, 0, c.pos.z - base.z));
                const vec3 across = glm::cross(kUp, face);
                for (float wy = 3.0f; wy < h - 2.0f; wy += 3.0f)
                    for (float wx = -w + 1.0f; wx < w - 1.0f; wx += 2.0f)
                        if (rnd() < 0.55f) {
                            const vec3 p = base + face * (w + 0.05f) + across * wx + vec3(0, wy, 0);
                            const uint32_t lit = rnd() < 0.5f ? rgba(255, 220, 120) : rgba(120, 220, 255);
                            b.texture(TexWhite);
                            b.quad(p - across * 0.5f, p + across * 0.5f, p + across * 0.5f + kUp * 1.2f,
                                   p - across * 0.5f + kUp * 1.2f, lit);
                        }
                break;
            }
            }
        }
    }
    // Sky decoration: clouds by day, stars at night.
    b.texture(TexCloud, 4.0f);
    const int clouds = th.scenery == S::City ? 0 : th.scenery == S::Clouds ? 70 : th.scenery == S::Lava ? 0 : 40;
    for (int i = 0; i < clouds; ++i) {
        const float a = rnd() * 2.0f * kPi;
        const float d = span * 0.8f + 120.0f + rnd() * 380.0f;
        const vec3 p(mid.x + std::cos(a) * d, mid.y + 10.0f + rnd() * 50.0f, mid.z + std::sin(a) * d);
        if (props) {
            place("cloud", p, rnd() * 6.28f, vec3(1.2f + rnd() * 1.0f));
            continue;
        }
        for (int k = 0; k < 4; ++k)
            b.sphere(p + vec3(rnd() * 16.0f - 8.0f, rnd() * 3.0f, rnd() * 10.0f - 5.0f), 5.0f + rnd() * 5.0f,
                     rgba(250, 250, 255), 14, 10);
    }
    if (th.scenery == S::City || th.scenery == S::Lava) {
        for (int i = 0; i < 160; ++i) {
            const float a = rnd() * 2.0f * kPi, e = 0.15f + rnd() * 1.2f;
            const float d = 700.0f;
            const vec3 p = mid + vec3(std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)) * d;
            b.icosphere(p, 1.2f + rnd() * 1.0f, th.scenery == S::City ? rgba(255, 255, 230) : rgba(255, 140, 60));
        }
    }
    hills(b, t, th, rnd, mid, span, floor_y);
    if (!props) { // with props, the painted trackside draws these
        kerbs(b, t);
        wall_rails(b, t, th);
    }
    start_gantry(b, t, th);
    grandstands(b, t, th, rnd);
    {
        const auto [left, right] = walls_of(t);
        if (!props) flags(b, t, th, left, right);
        if (props) {
            wall_props(t, th, left, right, rnd, *props);
            roadside_islands(b, t, th, rnd, *props);
            // Obstacles as painted props, fitted to their collision shapes.
            for (const auto& o : t.obstacles) {
                const bool pillar = o.sides > 4;
                const std::string& th_name = t.theme;
                const char* kind = th_name == "desert" || th_name == "canyon" ? "rock_red"
                                   : th_name == "snow"                       ? "crystal"
                                   : th_name == "volcano"                    ? "lava_rock"
                                   : pillar ? (th_name == "sky" || th_name == "night" ? "tire_stack" : "barrel")
                                            : "crate";
                const float h = 2.0f * o.half.y + 0.3f;
                props->push_back({kind, mat4(vec4(o.right * o.half.x, 0.0f), vec4(o.up * h, 0.0f), vec4(o.forward * o.half.z, 0.0f), vec4(o.pos, 1.0f)), true});
            }
        }
    }
    return b.take();
}

MeshData make_spark_mesh() {
    MeshBuilder b;
    b.icosphere({0, 0, 0}, 0.12f, rgba(255, 255, 255));
    return b.take();
}

MeshData make_flame_mesh() {
    MeshBuilder b;
    // Cone pointing backwards (-Z) from the exhaust.
    const int sides = 5;
    const float r = 0.16f, len = 0.9f;
    const vec3 tip(0, 0, -len);
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(sides), a1 = 2.0f * kPi * float(i + 1) / float(sides);
        const vec3 p0(std::cos(a0) * r, std::sin(a0) * r, 0), p1(std::cos(a1) * r, std::sin(a1) * r, 0);
        b.tri(p1, p0, tip, rgba(255, 255, 255));
        b.tri(vec3(0), p0, p1, rgba(255, 255, 255));
    }
    return b.take();
}

} // namespace gk::render
