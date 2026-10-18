#include "track_mesh.h"

#include "lava.h"
#include "textures.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <unordered_set>
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
    case Surface::Oil: return th.road;
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

static_assert(TexLava == 19 && TexWater == 7, "mesh.vert and mesh.frag test these layer numbers");

// Water's vertex alpha: metres to the shore, 0 at the bank .. 1 from 6 m out
// (mesh.vert damps the swell and mesh.frag foams near the bank by it).
uint8_t shore_alpha(float metres) { return uint8_t(std::clamp(metres / 6.0f, 0.0f, 1.0f) * 255.0f + 0.5f); }
// Turns the faces added since `from_index` to face up (grids are wound by
// their point order).
void face_up(MeshBuilder& b, size_t from_vertex, size_t from_index) {
    auto& d = b.data();
    if (from_vertex >= d.vertices.size() || d.vertices[from_vertex].normal.y >= 0.0f) return;
    for (size_t i = from_vertex; i < d.vertices.size(); ++i) d.vertices[i].normal = -d.vertices[i].normal;
    for (size_t i = from_index; i + 2 < d.indices.size(); i += 3) std::swap(d.indices[i + 1], d.indices[i + 2]);
}
// A flat sheet of water in rows and columns of points, with each point's
// distance to the shore.
void water_grid(MeshBuilder& b, const std::vector<vec3>& pts, const std::vector<float>& shore, int rows, int cols, uint32_t color) {
    const size_t v0 = b.data().vertices.size(), i0 = b.data().indices.size();
    b.grid(pts, rows, cols, false, color);
    auto& v = b.data().vertices;
    for (size_t i = 0; i < pts.size() && v0 + i < v.size(); ++i)
        v[v0 + i].color = (v[v0 + i].color & 0x00ffffffu) | (uint32_t(shore_alpha(shore[i])) << 24);
    face_up(b, v0, i0);
}

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
        {"market", {rgba(168, 156, 140), rgba(190, 178, 160), rgba(150, 120, 90), rgba(120, 90, 60), rgba(222, 196, 130),
                    rgba(186, 228, 245), rgba(70, 140, 190), rgba(238, 226, 200), rgba(170, 70, 50), rgba(120, 105, 90),
                    rgba(60, 120, 170), {0.62f, 0.8f, 0.97f}, 0.0028f, 0.5f, S::Market}},
        {"rainbow", {rgba(120, 100, 160), rgba(90, 80, 140), rgba(80, 70, 120), rgba(60, 50, 90), rgba(200, 180, 220),
                     rgba(200, 220, 255), rgba(60, 60, 140), rgba(255, 120, 200), rgba(120, 220, 255), rgba(40, 30, 60),
                     rgba(30, 20, 60), {0.9f, 0.55f, 0.6f}, 0.0012f, 0.6f, S::Clouds}},
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
    if (theme == "market" && (s == Surface::Road || s == Surface::OffroadLight)) return TexBrick; // cobbles
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
    case Surface::OutOfBounds: return theme == "volcano" ? TexLava : theme == "harbor" ? TexWater : terrain(false); // a lava pool, the sea
    case Surface::Oil: return TexAsphalt;
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

MeshData make_track_mesh(const sim::Track& t, bool skip_obstacles, int group) {
    const ThemeStyle& th = theme_style(t.theme);
    MeshBuilder b;
    const sim::CenterSample& start = t.center.front();
    const int n = int(t.center.size());
    constexpr int kStride = 9; // vertices per centre sample (track_builder.cpp)
    const uint32_t grid_end = uint32_t(n * kStride);
    std::vector<int> obstacle_of(t.triangles.size(), -1);
    for (size_t o = 0; o < t.obstacles.size(); ++o)
        for (uint32_t i = 0; i < t.obstacles[o].tri_count; ++i) obstacle_of[t.obstacles[o].first_tri + i] = int(o);
    // Every road's vertex grid: the main loop's, then each branch's.
    const std::vector<sim::Track::Strip> strips = t.strips();
    auto strip_of = [&](uint32_t vi) {
        for (size_t k = 0; k < strips.size(); ++k)
            if (vi >= strips[k].first_vertex && vi < strips[k].first_vertex + uint32_t(strips[k].count * kStride)) return int(k);
        return -1;
    };

    // Texture coordinates that follow the road: along it by distance and
    // across it by metres from the centre (or up a wall).
    auto road_uv = [&](uint32_t vi, bool wall, float tile, bool seam) {
        const sim::Track::Strip& st = strips[size_t(std::max(strip_of(vi), 0))];
        const uint32_t local = vi - st.first_vertex;
        const int sample = int(local / kStride), col = int(local % kStride);
        const sim::CenterSample& c = t.road[size_t(st.first + sample)];
        const float s = (seam && sample == 0) ? t.length : c.u;
        const vec3 d = t.vertices[vi] - c.pos;
        if (wall) return vec2(s / tile, (col >= 7 ? -glm::dot(d, c.up) : 0.0f) / tile);
        return vec2(glm::dot(d, c.right) / tile, s / tile);
    };

    // Water on the road: how far each of its vertices is from the nearest
    // vertex it shares with anything else (the bank).
    std::vector<float> shore(t.vertices.size(), 6.0f);
    {
        std::vector<uint8_t> touch(t.vertices.size(), 0);
        for (const auto& tri : t.triangles) {
            if (tri.surface == sim::Surface::OutOfBounds) continue;
            const uint8_t bit = tri.surface == sim::Surface::Water ? 1 : 2;
            touch[tri.a] |= bit, touch[tri.b] |= bit, touch[tri.c] |= bit;
        }
        std::vector<vec3> bank;
        for (size_t v = 0; v < touch.size(); ++v)
            if (touch[v] == 3) bank.push_back(t.vertices[v]);
        for (size_t v = 0; v < touch.size(); ++v) {
            if (touch[v] == 3) shore[v] = 0.0f;
            if (touch[v] != 1) continue;
            for (const vec3& p : bank) shore[v] = std::min(shore[v], glm::distance(p, t.vertices[v]));
        }
    }
    float sea_y = 0.0f;
    bool sea = false;

    // Floor edges that border nothing else: where two roads meet edge to
    // edge their vertices don't line up, and a hairline of whatever is below
    // can show between them. A short skirt hangs under each such edge.
    std::map<std::pair<uint32_t, uint32_t>, int> floor_edges;
    {
        auto key = [](uint32_t a, uint32_t b) { return std::pair(std::min(a, b), std::max(a, b)); };
        for (const auto& tri : t.triangles) {
            const bool floor = tri.surface != sim::Surface::Wall && tri.surface != sim::Surface::OutOfBounds;
            const uint32_t v[3] = {tri.a, tri.b, tri.c};
            for (int k = 0; k < 3; ++k) floor_edges[key(v[k], v[(k + 1) % 3])] += floor ? 1 : 100; // a wall on it: no skirt
        }
    }
    auto skirts = [&](const sim::TrackTriangle& tri, uint32_t color) {
        const uint32_t v[3] = {tri.a, tri.b, tri.c};
        for (int k = 0; k < 3; ++k) {
            const uint32_t i = v[k], j = v[(k + 1) % 3];
            if (floor_edges[std::pair(std::min(i, j), std::max(i, j))] != 1) continue;
            const vec3 p = t.vertices[i], q = t.vertices[j], down(0.0f, -0.35f, 0.0f);
            b.quad(p, q, q + down, p + down, color);
            b.quad(p, p + down, q + down, q, color);
        }
    };

    for (size_t ti = 0; ti < t.triangles.size(); ++ti) {
        const auto& tri = t.triangles[ti];
        if (group >= 0 && tri.group != group) continue;
        const vec3 a = t.vertices[tri.a], c1 = t.vertices[tri.b], c2 = t.vertices[tri.c];
        const vec3 centroid = (a + c1 + c2) / 3.0f;
        if (tri.surface == sim::Surface::OutOfBounds && th.scenery == ThemeStyle::Scenery::Harbor) {
            sea = true, sea_y = a.y; // the sea, built below as a fine grid that can swell
            continue;
        }
        if (obstacle_of[ti] >= 0) {
            if (skip_obstacles) continue; // drawn as props
            const auto& o = t.obstacles[size_t(obstacle_of[ti])];
            const auto [tex, color] = obstacle_look(t.theme, o.sides > 4);
            b.texture(tex, o.sides > 4 ? 2.0f : 2.0f * std::max(o.half.x, o.half.y));
            b.tri(a, c1, c2, color);
            continue;
        }
        if (!t.stairs.empty() && tri.surface != sim::Surface::Wall && tri.a < grid_end && tri.b < grid_end && tri.c < grid_end) {
            const int sa = int(tri.a / kStride), sb = int(tri.b / kStride), sc = int(tri.c / kStride);
            if (t.on_stairs(t.center[size_t(sa)].s) && t.on_stairs(t.center[size_t(sb)].s) && t.on_stairs(t.center[size_t(sc)].s)) continue;
        }
        uint32_t color = surface_color(th, tri.surface);
        if (tri.surface == sim::Surface::Wall) color = checker(centroid, 2.0f) ? th.wall_a : th.wall_b;
        const float along = glm::dot(centroid - start.pos, start.tangent);
        const float across = glm::dot(centroid - start.pos, start.right);
        if (tri.surface == sim::Surface::Road && std::abs(along) < 1.0f && std::abs(across) < start.half_width)
            color = (int(std::floor(across)) & 1) ? rgba(20, 20, 20) : rgba(240, 240, 240);
        b.texture(surface_texture(t.theme, tri.surface), tri.surface == sim::Surface::OutOfBounds ? 8.0f : 2.0f);

        const int sa = strip_of(tri.a), sb = strip_of(tri.b), sc = strip_of(tri.c);
        const bool on_grid = sa >= 0 && sb >= 0 && sc >= 0;
        if (!on_grid) {
            b.tri(a, c1, c2, color);
            if (tri.surface != sim::Surface::Wall) skirts(tri, color);
            continue;
        }
        const bool wall = tri.surface == sim::Surface::Wall;
        if (wall && skip_obstacles) continue; // painted walls (trackside.cpp), the joins between roads too
        const bool seam = sa == 0 && sb == 0 && sc == 0 && std::max({tri.a, tri.b, tri.c}) / kStride == uint32_t(n - 1) &&
                          std::min({tri.a, tri.b, tri.c}) / kStride == 0;
        const bool pad = tri.surface == sim::Surface::BoostPad || tri.surface == sim::Surface::GlideRamp;
        const float tile = pad ? 4.0f : tri.surface == sim::Surface::TrickRamp ? 3.0f : 2.0f;
        const vec2 ua = road_uv(tri.a, wall, tile, seam), ub = road_uv(tri.b, wall, tile, seam),
                   uc = road_uv(tri.c, wall, tile, seam);
        b.tri_uv(a, c1, c2, ua, ub, uc, color);
        if (tri.surface == sim::Surface::Water) {
            auto& vs = b.data().vertices;
            const uint32_t ids[3] = {tri.a, tri.b, tri.c};
            for (int k = 0; k < 3; ++k) {
                Vertex& vx = vs[vs.size() - 3 + size_t(k)];
                vx.color = (vx.color & 0x00ffffffu) | (uint32_t(shore_alpha(shore[ids[k]])) << 24);
            }
        }
        if (wall) {
            b.tri_uv(a, c2, c1, ua, uc, ub, shade(color, 0.7f)); // outside face
        } else {
            skirts(tri, color);
            const vec3 down(0.0f, -1.2f, 0.0f);
            b.texture(TexRock, 3.0f);
            b.tri(a + down, c2 + down, c1 + down, th.underside);
        }
    }
    if (sea) {
        // A 4 m grid of water around the track, so it can swell, and the
        // rest of the sea out to the horizon as four flat sheets a little
        // lower, tucked under the grid's edge.
        vec2 lo(1e9f), hi(-1e9f);
        for (const auto& c : t.road) lo = glm::min(lo, vec2(c.pos.x, c.pos.z)), hi = glm::max(hi, vec2(c.pos.x, c.pos.z));
        lo -= vec2(150.0f), hi += vec2(150.0f);
        const float cell = 4.0f;
        const int cols = int((hi.x - lo.x) / cell) + 1, rows = int((hi.y - lo.y) / cell) + 1;
        std::vector<vec3> pts;
        pts.reserve(size_t(rows * cols));
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) pts.push_back({lo.x + float(c) * cell, sea_y, lo.y + float(r) * cell});
        b.texture(TexWater, 4.0f);
        water_grid(b, pts, std::vector<float>(pts.size(), 6.0f), rows, cols, th.below);
        const float far = std::max(hi.x - lo.x, hi.y - lo.y) + 1200.0f, y = sea_y - 0.25f;
        const vec2 mid = 0.5f * (lo + hi), in_lo = lo + vec2(cell), in_hi = vec2(lo.x + float(cols - 2) * cell, lo.y + float(rows - 2) * cell);
        auto sheet = [&](vec2 p0, vec2 p1) { // x0,z0 .. x1,z1, facing up
            b.quad(vec3(p0.x, y, p0.y), vec3(p0.x, y, p1.y), vec3(p1.x, y, p1.y), vec3(p1.x, y, p0.y), th.below);
        };
        sheet(mid - vec2(far), vec2(mid.x + far, in_lo.y));                  // south
        sheet(vec2(mid.x - far, in_hi.y), mid + vec2(far));                  // north
        sheet(vec2(mid.x - far, in_lo.y), vec2(in_lo.x, in_hi.y));           // west
        sheet(vec2(in_hi.x, in_lo.y), vec2(mid.x + far, in_hi.y));           // east
    }
    // Stairs: stone steps over the smooth slope the karts drive on, each
    // tread level at the slope's height under its middle.
    for (const auto& [sa, sb] : t.stairs) {
        const float tread = 1.2f;
        const int steps = std::max(1, int((sb - sa) / tread));
        auto at = [&](float s) { // interpolated centre sample at distance s
            const float f = std::fmod(std::fmod(s / std::max(t.length, 1.0f), 1.0f) + 1.0f, 1.0f) * float(n);
            const int i0 = int(f) % n, i1 = (i0 + 1) % n;
            const float k = f - std::floor(f);
            sim::CenterSample c = t.center[size_t(i0)];
            const sim::CenterSample& d = t.center[size_t(i1)];
            c.pos = glm::mix(c.pos, d.pos, k);
            c.half_width = glm::mix(c.half_width, d.half_width, k);
            return c;
        };
        for (int k = 0; k < steps; ++k) {
            const float s0 = sa + float(k) * (sb - sa) / float(steps), s1 = sa + float(k + 1) * (sb - sa) / float(steps);
            const sim::CenterSample c0 = at(s0), c1 = at(s1), cm = at(0.5f * (s0 + s1));
            const float y = cm.pos.y + 0.03f, bottom = c1.pos.y - 0.3f;
            const float w = cm.half_width + 1.0f;
            auto P = [&](const sim::CenterSample& c, float side, float yy) { return vec3(c.pos.x, yy, c.pos.z) + c.right * (side * w); };
            const vec3 a0 = P(c0, -1, y), a1 = P(c0, 1, y), b0 = P(c1, -1, y), b1 = P(c1, 1, y);
            b.texture(TexBrick, 1.2f);
            const uint32_t stone = (k & 1) ? rgba(214, 202, 182) : rgba(200, 188, 168);
            auto fq = [&](vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 want, uint32_t col) {
                if (glm::dot(glm::cross(p1 - p0, p2 - p0), want) < 0.0f) b.quad(p0, p3, p2, p1, col);
                else b.quad(p0, p1, p2, p3, col);
            };
            fq(a0, b0, b1, a1, vec3(0, 1, 0), stone); // tread
            const vec3 r0 = P(c1, -1, bottom), r1 = P(c1, 1, bottom);
            fq(b0, r0, r1, b1, c1.tangent, rgba(140, 128, 112));                 // riser, facing down the flight
            fq(a0, P(c0, -1, bottom), r0, b0, -c0.right, rgba(160, 150, 135));    // sides
            fq(b1, r1, P(c0, 1, bottom), a1, c0.right, rgba(160, 150, 135));
            b.texture(TexWhite);
            const vec3 up(0, 0.015f, 0);
            fq(a0 + up, a0 + c0.tangent * 0.14f + up, a1 + c0.tangent * 0.14f + up, a1 + up, vec3(0, 1, 0), rgba(226, 218, 204)); // worn nosing
        }
    }
    return b.take();
}

// Whether `p` is within `margin` of a side road's edge (from above when
// `flat`, and only within `max_dy` up or down): scenery keeps off them.
bool near_branch(const sim::Track& t, vec3 p, float margin, bool flat, float max_dy = 1e9f) {
    for (size_t j = t.center.size(); j < t.road.size(); j += 2) {
        const sim::CenterSample& o = t.road[j];
        const float d = flat ? glm::length(vec2(o.pos.x - p.x, o.pos.z - p.z)) : glm::distance(o.pos, p);
        if (d < o.half_width + margin && std::abs(o.pos.y - p.y) < max_dy) return true;
    }
    return false;
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
        if (!clear || near_branch(t, base, 10.0f, true)) continue;
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
    if (th.scenery == S::City || th.scenery == S::Clouds || th.scenery == S::Market) return;
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
        if (!clear || near_branch(t, p, r * 0.9f, true)) continue;
        std::vector<vec2> prof{{r, -2.0f}, {r * 0.92f, h * 0.2f}, {r * 0.7f, h * 0.55f}, {r * 0.38f, h * 0.85f}, {0.0f, h}};
        b.lathe(p, kUp, prof, 18, col, 8.0f, 2.0f);
    }
}

// Lamp posts on the walls, leaning over the road, and billboards on the
// outside of the walls along straights, facing the road.
// `ground_y`: the terrain's height, NaN without terrain.
void wall_props(const sim::Track& t, const ThemeStyle& th, const std::vector<bool>& left, const std::vector<bool>& right, Rng& rnd,
                std::vector<PropPlacement>& out, const std::function<float(float, float)>& ground_y) {
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
        if (th.scenery == S::City) { // the lamp's head reaches about 0.9 m over the road; its light falls further in
            const vec3 pool = c.pos + c.right * float(side) * (c.half_width - 2.5f) + c.up * 0.02f;
            const int k = (i / lamp_every) % 3;
            out.push_back({k == 0 ? "pool_warm" : k == 1 ? "pool_pink" : "pool_cyan", glm::translate(mat4(1.0f), pool)});
        }
    }
    // Chevron signs round the outside of every proper bend, facing the karts.
    for (int i = 12; i < n - 12; i += 6) {
        const sim::CenterSample& c = t.center[size_t(i)];
        const vec3 t0 = t.center[size_t(i - 10)].tangent, t1 = t.center[size_t(i + 10)].tangent;
        if (glm::dot(t0, t1) > 0.82f) continue;
        const bool turns_right = glm::dot(glm::cross(t0, t1), c.up) < 0.0f;
        const int side = turns_right ? -1 : 1; // the outside of the bend
        if (!(side > 0 ? right[size_t(i)] : left[size_t(i)])) continue;
        const vec3 top = t.vertices[size_t(i * kStride + (side > 0 ? 8 : 7))];
        const vec3 outward = c.right * float(side);
        out.push_back({turns_right ? "turn_sign" : "turn_sign_left", oriented(c, top + outward * 0.5f - c.up * 0.1f, glm::cross(c.up, -c.tangent), 1.0f)});
    }
    // Feather flags along the walls, alternating sides and colours.
    if (th.scenery != S::City && th.scenery != S::Lava)
        for (int i = 16; i < n - 8; i += 26) {
            const int side = (i / 26) % 2 ? 1 : -1;
            if (!(side > 0 ? right[size_t(i)] : left[size_t(i)])) continue;
            const sim::CenterSample& c = t.center[size_t(i)];
            const vec3 top = t.vertices[size_t(i * kStride + (side > 0 ? 8 : 7))];
            const vec3 outward = c.right * float(side);
            const int k = (i / 26) % 3;
            out.push_back({k == 0 ? "flag_red" : k == 1 ? "flag_blue" : "flag_green",
                           oriented(c, top + outward * 1.1f - c.up * 1.4f, c.tangent * float(side), 1.0f + 0.15f * rnd())});
        }
    // Grandstands full of fans where the road runs straight.
    for (int i = 20; i < n - 20; i += 85) {
        const sim::CenterSample& c = t.center[size_t(i)];
        if (glm::dot(t.center[size_t(i - 10)].tangent, t.center[size_t(i + 10)].tangent) < 0.985f) continue;
        const int side = (i / 85) % 2 ? 1 : -1;
        if (!(side > 0 ? right[size_t(i)] : left[size_t(i)])) continue;
        const vec3 top = t.vertices[size_t(i * kStride + (side > 0 ? 8 : 7))];
        const vec3 outward = c.right * float(side);
        vec3 pos = top + outward * 4.5f - c.up * 1.6f;
        {   // On terrain: stand on it, on the lowest of the stand's corners so no foot hangs.
            const vec3 along = c.tangent * 7.0f, across = outward * 4.0f;
            float low = 1e9f;
            for (vec3 q : {pos - along, pos + along, pos - along + across, pos + along + across}) {
                const float h = ground_y(q.x, q.z);
                if (std::isfinite(h)) low = std::min(low, h);
            }
            if (low < 1e8f) pos.y = low;
        }
        bool clear = true;
        for (int j = 0; j < n && clear; j += 3) {
            const int along = std::abs(j - i);
            if (std::min(along, n - along) < 14) continue;
            clear = glm::distance(t.center[size_t(j)].pos, pos) > t.center[size_t(j)].half_width + 10.0f;
        }
        if (clear && !near_branch(t, pos, 10.0f, false)) out.push_back({"grandstand", oriented(c, pos, glm::cross(c.up, -outward), 1.0f)});
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
        if (!clear || near_branch(t, pos, 8.0f, false)) continue;
        // The board's front is +Z: turn it to face the road.
        const vec3 z = -outward;
        const vec3 x = glm::normalize(glm::cross(c.up, z));
        const char* kind = th.scenery == S::City ? (rnd() < 0.5f ? "neon_pink" : "neon_cyan")
                           : int(rnd() * 3.0f) == 0 ? "billboard" : rnd() < 0.5f ? "billboard_2" : "billboard_3";
        out.push_back({kind, mat4(vec4(x, 0.0f), vec4(c.up, 0.0f), vec4(glm::cross(x, c.up), 0.0f), vec4(pos, 1.0f))});
    }
}

// Small islands just outside the walls, at road height, each with a few of
// the theme's props, so the roadside is never empty.
void roadside_islands(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th, Rng& rnd, std::vector<PropPlacement>& out) {
    using S = ThemeStyle::Scenery;
    if (th.scenery == S::City || th.scenery == S::Market) return;
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
        if (!clear || near_branch(t, centre, r + 5.0f, true, 14.0f)) continue;
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
            if (rnd() < 0.25f) place("hay_bale", spot(0.7f), rnd() * 6.28f, 1.0f);
            if (rnd() < 0.2f) place("fence", spot(0.6f), rnd() * 6.28f, 1.0f);
            if (rnd() < 0.06f) place("windmill", top, rnd() * 6.28f, 0.6f);
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
            if (rnd() < 0.15f) place("igloo", spot(0.4f), rnd() * 6.28f, 0.7f);
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

// The market town: houses shoulder to shoulder along both sides of the
// road on stone plinths, stalls round the plaza, strings of lanterns across
// the street, and a canal under the glide.
void market_town(MeshBuilder& b, const sim::Track& t, const ThemeStyle& th, Rng& rnd, float floor_y, std::vector<PropPlacement>& out) {
    constexpr int kStride = 9;
    const int n = int(t.center.size());
    auto place = [&](const char* name, vec3 pos, vec3 facing, float scale) {
        const vec3 z = glm::normalize(vec3(facing.x, 0.0f, facing.z)), x = glm::cross(kUp, z);
        out.push_back({name, mat4(vec4(x * scale, 0.0f), vec4(kUp * scale, 0.0f), vec4(z * scale, 0.0f), vec4(pos, 1.0f))});
    };
    auto clear_of_track = [&](vec3 p, float r, int self) {
        for (int j = 0; j < n; j += 2) {
            const int along = std::abs(j - self);
            if (std::min(along, n - along) < 8) continue;
            const sim::CenterSample& o = t.center[size_t(j)];
            if (glm::length(vec2(o.pos.x - p.x, o.pos.z - p.z)) < o.half_width + r + 2.0f && std::abs(o.pos.y - p.y) < 12.0f) return false;
        }
        return !near_branch(t, p, r + 2.0f, true, 12.0f);
    };
    const char* houses[5] = {"house_1", "house_2", "house_3", "house_4", "house_5"};
    std::vector<std::pair<vec3, vec3>> fronts[2]; // per side: house front top points, for lantern strings
    for (int side = 0; side < 2; ++side) {
        const float sgn = side ? 1.0f : -1.0f;
        float next_s = rnd() * 3.0f;
        for (int i = 0; i < n; ++i) {
            const sim::CenterSample& c = t.center[size_t(i)];
            if (c.s < next_s) continue;
            next_s = c.s + 4.4f;
            if (!c.has_floor || t.on_stairs(c.s)) continue;
            const vec3 wall = t.vertices[size_t(i * kStride + (side ? 6 : 0))];
            const vec3 face = -c.right * sgn; // towards the road
            const vec3 pos = wall + c.right * sgn * 2.2f;
            if (!clear_of_track(pos, 3.0f, i)) continue;
            const int kind = int(rnd() * 5.0f) % 5;
            place(houses[kind], pos, face, 1.0f);
            // A stone plinth down to the ground.
            b.texture(TexBrick, 2.0f);
            const float h = pos.y - floor_y;
            if (h > 0.5f) {
                const vec3 x = glm::normalize(glm::cross(kUp, face));
                MeshBuilder pl;
                pl.texture(TexBrick, 2.0f);
                pl.box({0, -h * 0.5f, 0}, {2.25f, h * 0.5f, 1.7f}, th.underside);
                b.append(pl.take(), mat4(vec4(x, 0.0f), vec4(kUp, 0.0f), vec4(glm::normalize(vec3(face.x, 0, face.z)), 0.0f), vec4(pos, 1.0f)));
            }
            fronts[side].push_back({pos + face * 1.5f + kUp * 5.8f, face});
            // Stalls in front of houses where the verge is wide (the plaza).
            const float verge = glm::length(wall - t.vertices[size_t(i * kStride + (side ? 5 : 1))]);
            if (verge > 5.0f && rnd() < 0.7f) {
                const char* stall = rnd() < 0.33f ? "stall_red" : rnd() < 0.5f ? "stall_blue" : "stall_green";
                place(stall, wall - c.right * sgn * 2.0f, face, 1.0f);
            }
        }
    }
    // Lantern strings across the street, between facing houses.
    b.texture(TexWhite);
    for (size_t k = 0; k < fronts[0].size(); k += 3) {
        const vec3 a = fronts[0][k].first;
        // The nearest house front on the other side.
        float best = 1e9f;
        vec3 bpos{};
        for (const auto& f : fronts[1]) {
            const float d = glm::distance(a, f.first);
            if (d < best) best = d, bpos = f.first;
        }
        if (best > 26.0f || best < 6.0f) continue;
        std::vector<vec3> cable;
        for (int s = 0; s <= 8; ++s) {
            const float u = float(s) / 8.0f;
            cable.push_back(glm::mix(a, bpos, u) - kUp * (1.2f * 4.0f * u * (1.0f - u)));
        }
        b.sweep(cable, 0.025f, rgba(50, 40, 35), 4, false);
        for (int s = 1; s < 8; ++s) out.push_back({"lantern", glm::translate(mat4(1.0f), cable[size_t(s)] - kUp * 0.35f)});
    }
    // Water in the canal under every gap: one sheet per gap, about 2.5 m
    // between points across and a road sample (2 m) along, so it can swell.
    b.texture(TexWater, 4.0f);
    for (int i0 = 0; i0 < n;) {
        if (t.center[size_t(i0)].has_floor) {
            ++i0;
            continue;
        }
        int i1 = i0;
        while (i1 < n && !t.center[size_t(i1)].has_floor) ++i1;
        const int rows = i1 - i0 + 1; // the gap's samples and the one after it
        float y = 1e9f, w = 0.0f;
        for (int r = 0; r < rows; ++r) {
            const sim::CenterSample& c = t.center[size_t((i0 + r) % n)];
            y = std::min(y, c.pos.y - 5.0f), w = std::max(w, c.half_width + 14.0f);
        }
        const int cols = std::max(2, int(std::ceil(2.0f * w / 2.5f)) + 1);
        std::vector<vec3> pts;
        std::vector<float> shore;
        for (int r = 0; r < rows; ++r) {
            const sim::CenterSample& c = t.center[size_t((i0 + r) % n)];
            const vec3 right = glm::normalize(vec3(c.right.x, 0.0f, c.right.z) + vec3(1e-6f));
            for (int k = 0; k < cols; ++k) {
                const float x = -w + 2.0f * w * float(k) / float(cols - 1);
                pts.push_back(vec3(c.pos.x, y, c.pos.z) + right * x);
                shore.push_back(std::min(w - std::abs(x), 2.0f * float(std::min(r, rows - 1 - r)))); // the banks and the ends
            }
        }
        water_grid(b, pts, shore, rows, cols, th.water);
        i0 = i1;
    }
}

// ---- terrain under a grounded track

namespace {

float hash01(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}
float value_noise(float x, float y, uint32_t seed) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    const float tx = fx * fx * (3.0f - 2.0f * fx), ty = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash01(x0, y0, seed), c = hash01(x0 + 1, y0, seed), d = hash01(x0, y0 + 1, seed), e = hash01(x0 + 1, y0 + 1, seed);
    return glm::mix(glm::mix(a, c, tx), glm::mix(d, e, tx), ty);
}
float fbm2(float x, float y, uint32_t seed) {
    return 0.55f * value_noise(x, y, seed) + 0.3f * value_noise(x * 2.1f, y * 2.1f, seed + 1) + 0.15f * value_noise(x * 4.3f, y * 4.3f, seed + 2);
}

// Rolling terrain under a grounded track: a height field that follows the
// road a little below it (so the road rides a ridge or an embankment), drops
// into a ravine under glide gaps, and rolls off into the theme's hills away
// from the road.
struct Ground {
    float x0 = 0.0f, z0 = 0.0f, cell = 5.0f;
    int nx = 0, nz = 0;
    std::vector<float> h;
    bool valid() const { return nx > 1 && nz > 1; }
    float at(int i, int j) const { return h[size_t(std::clamp(j, 0, nz - 1) * nx + std::clamp(i, 0, nx - 1))]; }
    // On the ground mesh itself: two triangles per cell, the diagonal
    // alternating as add_ground_mesh lays them, so props sit right on it.
    float height(float x, float z) const {
        if (!valid()) return 0.0f;
        const float u = (x - x0) / cell, v = (z - z0) / cell;
        const int i = std::clamp(int(std::floor(u)), 0, nx - 2), j = std::clamp(int(std::floor(v)), 0, nz - 2);
        const float fu = std::clamp(u - float(i), 0.0f, 1.0f), fv = std::clamp(v - float(j), 0.0f, 1.0f);
        const float a = at(i, j), c = at(i + 1, j), d = at(i + 1, j + 1), e = at(i, j + 1);
        if ((i + j) & 1) // diagonal a-d
            return fv > fu ? a + fu * (d - e) + fv * (e - a) : a + fu * (c - a) + fv * (d - c);
        return fu + fv < 1.0f ? a + fu * (c - a) + fv * (e - a) : d + (1.0f - fu) * (e - d) + (1.0f - fv) * (c - d); // diagonal c-e
    }
};

struct GroundLook {
    float amp;
    uint32_t tex, color, steep_color;
};
GroundLook ground_look(const ThemeStyle& th) {
    using S = ThemeStyle::Scenery;
    switch (th.scenery) {
    case S::Cactus: return {5.0f, TexSand, rgba(226, 196, 132), rgba(190, 140, 90)};
    case S::Pines: return {12.0f, TexSnow, rgba(236, 242, 250), rgba(150, 160, 180)};
    case S::Mesas: return {10.0f, TexDirt, rgba(206, 140, 86), rgba(160, 82, 52)};
    case S::Lava: return {8.0f, TexRock, rgba(66, 54, 56), rgba(38, 30, 32)};
    default: return {7.0f, TexGrass, rgba(96, 168, 70), rgba(120, 96, 70)};
    }
}

Ground make_ground(const sim::Track& t, const ThemeStyle& th) {
    Ground g;
    const GroundLook look = ground_look(th);
    vec3 lo(1e9f), hi(-1e9f);
    for (const auto& c : t.road) lo = glm::min(lo, c.pos), hi = glm::max(hi, c.pos);
    const float margin = 280.0f;
    g.x0 = lo.x - margin, g.z0 = lo.z - margin;
    g.nx = int((hi.x - lo.x + 2.0f * margin) / g.cell) + 1;
    g.nz = int((hi.z - lo.z + 2.0f * margin) / g.cell) + 1;
    const float base = lo.y - 2.5f;
    const uint32_t seed = hash_name(t.id);
    std::vector<float> target(size_t(g.nx * g.nz), 1e9f), weight(size_t(g.nx * g.nz), 0.0f), near(size_t(g.nx * g.nz), 1e9f);
    // On a volcano the ravines go down below the lava pool, so it shows in them.
    float ravine_floor = 1e9f;
    if (th.scenery == ThemeStyle::Scenery::Lava)
        for (const auto& tri : t.triangles)
            if (tri.surface == sim::Surface::OutOfBounds) {
                ravine_floor = t.vertices[tri.a].y - 4.0f;
                break;
            }
    // Every road sample pulls the ground towards just under itself, strongly
    // close by and fading out over ~45 m; the lowest nearby road wins, so a
    // bridge never buries the road beneath it.
    // How far each sample's road reaches out, verges included: a wide verge
    // must stay clear of the ground too.
    std::vector<float> reach(t.road.size(), 0.0f);
    for (const sim::Track::Strip& st : t.strips())
        for (int k = 0; k < st.count; ++k) {
            const sim::CenterSample& c = t.road[size_t(st.first + k)];
            const uint32_t v = st.first_vertex + uint32_t(k) * 9; // kStride (track_builder.cpp)
            for (uint32_t col : {0u, 6u})
                if (v + col < t.vertices.size()) reach[size_t(st.first + k)] = std::max(reach[size_t(st.first + k)], glm::distance(t.vertices[v + col], c.pos));
        }
    for (size_t ri = 0; ri < t.road.size(); ++ri) {
        const sim::CenterSample& c = t.road[ri];
        const float edge = std::max(c.half_width, reach[ri]);
        const float inner = edge + 5.0f, outer = edge + 46.0f;
        // A ravine under a glide, and under a road that can vanish.
        const bool can_vanish = c.branch >= 0 && t.branches[size_t(c.branch)].vanish;
        const float y = c.has_floor && !can_vanish ? c.pos.y - 0.9f : std::min(c.pos.y - 16.0f, ravine_floor);
        const int i0 = std::max(0, int((c.pos.x - outer - g.x0) / g.cell)), i1 = std::min(g.nx - 1, int((c.pos.x + outer - g.x0) / g.cell) + 1);
        const int j0 = std::max(0, int((c.pos.z - outer - g.z0) / g.cell)), j1 = std::min(g.nz - 1, int((c.pos.z + outer - g.z0) / g.cell) + 1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const float x = g.x0 + float(i) * g.cell, z = g.z0 + float(j) * g.cell;
                const float d = glm::length(vec2(x - c.pos.x, z - c.pos.z));
                if (d > outer) continue;
                const float w = 1.0f - glm::smoothstep(inner, outer, d);
                const size_t k = size_t(j * g.nx + i);
                weight[k] = std::max(weight[k], w);
                target[k] = std::min(target[k], y + (1.0f - w) * 30.0f);
                near[k] = std::min(near[k], d);
            }
    }
    g.h.resize(size_t(g.nx * g.nz));
    for (int j = 0; j < g.nz; ++j)
        for (int i = 0; i < g.nx; ++i) {
            const size_t k = size_t(j * g.nx + i);
            const float x = g.x0 + float(i) * g.cell, z = g.z0 + float(j) * g.cell;
            // Hills grow bigger further from the track, into a skyline.
            const float far = glm::smoothstep(60.0f, 260.0f, near[k] < 1e8f ? near[k] : 1e4f);
            const float amp = look.amp * (1.0f + 2.5f * far);
            const float n = fbm2(x * 0.011f, z * 0.011f, seed) * 0.75f + fbm2(x * 0.004f, z * 0.004f, seed + 7) * 0.25f;
            const float hills = base + amp * n + far * look.amp * 1.5f;
            float hgt = glm::mix(hills, target[k], weight[k]);
            if (target[k] < 1e8f) hgt = std::min(hgt, target[k]);
            g.h[k] = hgt;
        }
    return g;
}

void add_ground_mesh(MeshBuilder& b, const Ground& g, const ThemeStyle& th, uint32_t seed) {
    const GroundLook look = ground_look(th);
    auto P = [&](int i, int j) { return vec3(g.x0 + float(i) * g.cell, g.at(i, j), g.z0 + float(j) * g.cell); };
    auto tri = [&](vec3 a, vec3 c, vec3 d, int i, int j) {
        const vec3 n = glm::normalize(glm::cross(c - a, d - a));
        const float steep = 1.0f - glm::smoothstep(0.6f, 0.85f, n.y);
        // A little patchiness so big slopes are not one flat colour.
        const float patch = 0.9f + 0.2f * hash01(i, j, seed);
        const uint32_t ground = shade(look.color, patch), rock = shade(look.steep_color, patch);
        b.texture(steep > 0.5f ? TexRock : look.tex, 6.0f);
        b.tri(a, c, d, steep > 0.5f ? rock : ground);
    };
    for (int j = 0; j + 1 < g.nz; ++j)
        for (int i = 0; i + 1 < g.nx; ++i) {
            const vec3 a = P(i, j), c = P(i + 1, j), d = P(i + 1, j + 1), e = P(i, j + 1);
            if ((i + j) & 1) tri(a, e, d, i, j), tri(a, d, c, i, j);
            else tri(a, e, c, i, j), tri(c, e, d, i, j);
        }
}

} // namespace

std::function<float(float, float)> make_terrain_sampler(const sim::Track& t) {
    if (!t.ground) return [](float, float) { return std::numeric_limits<float>::quiet_NaN(); };
    auto g = std::make_shared<Ground>(make_ground(t, theme_style(t.theme)));
    return [g](float x, float z) { return g->height(x, z); };
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
    // Terrain under a grounded track; the floating islands and distant
    // hills belong to the other kind.
    const Ground ground = t.ground ? make_ground(t, th) : Ground{};
    if (ground.valid()) add_ground_mesh(b, ground, th, hash_name(t.id));

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
            const float bottom = ground.valid() ? ground.height(top.x, top.z) - 1.0f : floor_y;
            const float h = top.y - bottom;
            if (h < 1.0f) continue;
            b.texture(t.theme == "night" || t.theme == "volcano" || t.theme == "sky" ? TexPanel : TexRock, 2.0f);
            b.box(vec3(top.x, bottom + h * 0.5f, top.z), vec3(0.8f, h * 0.5f, 0.8f), th.wall_b);
        }
    }

    // A mesa is placed by its middle and its skirt reaches 1.3 times its
    // radius: the largest radius that keeps it all off every road.
    auto mesa_room = [&](vec3 at) {
        float room = 1e9f;
        for (const auto& c : t.road) room = std::min(room, glm::length(vec2(c.pos.x - at.x, c.pos.z - at.z)) - c.half_width - 8.0f);
        return room / 1.3f;
    };
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
            if (!clear || near_branch(t, base, 12.0f, true)) continue;
            if (ground.valid() && props) { // standing on the terrain, no islands
                base.y = ground.height(base.x, base.z) + 0.1f;
                auto on_ground = [&](vec3 p) { return vec3(p.x, ground.height(p.x, p.z), p.z); };
                const float yaw = rnd() * 6.28f;
                switch (th.scenery) {
                case S::Cactus:
                    place("cactus", base, yaw, vec3(1.1f + rnd() * 0.9f));
                    if (rnd() < 0.5f) place(pick({"rock_red", "boulder"}), on_ground(base + vec3(3.0f, 0.0f, 1.5f)), rnd() * 6.28f, vec3(0.9f + rnd() * 1.2f));
                    if (rnd() < 0.2f) place("dead_tree", on_ground(base + vec3(-2.5f, 0.0f, -2.0f)), rnd() * 6.28f, vec3(1.2f));
                    break;
                case S::Pines:
                    for (int k = 0; k < 2 + int(rnd() * 3.0f); ++k) {
                        const vec3 p = base + vec3(rnd() * 8.0f - 4.0f, 0.0f, rnd() * 8.0f - 4.0f);
                        place("tree_pine_snow", vec3(p.x, ground.height(p.x, p.z), p.z), rnd() * 6.28f, vec3(0.8f + rnd() * 0.5f));
                    }
                    if (rnd() < 0.3f) place(pick({"snowman", "crystal", "rock"}), on_ground(base + vec3(3.0f, 0.0f, -2.0f)), yaw, vec3(0.9f + rnd() * 0.5f));
                    break;
                case S::Mesas:
                    if (rnd() < 0.45f) {
                        float r = 5.0f + rnd() * 8.0f;
                        const float h = 8.0f + rnd() * 22.0f;
                        r = std::min(r, mesa_room(base));
                        if (r >= 3.0f) place("mesa", base - vec3(0, 1.0f, 0), yaw, vec3(r, h, r));
                    } else {
                        place(pick({"rock_red", "boulder", "rock_red"}), base, yaw, vec3(1.2f + rnd() * 1.6f));
                        if (rnd() < 0.5f) place("cactus", on_ground(base + vec3(2.5f, 0.0f, 2.0f)), rnd() * 6.28f, vec3(1.2f));
                    }
                    break;
                case S::Lava: {
                    const float r = 2.5f + rnd() * 3.0f, h = 3.0f + rnd() * 9.0f;
                    place("lava_rock", base - vec3(0, 0.6f, 0), yaw, vec3(r / 2.4f, h / 2.6f, r / 2.2f));
                    if (rnd() < 0.3f) place("dead_tree", on_ground(base + vec3(3.0f, 0.0f, 0.0f)), rnd() * 6.28f, vec3(1.0f));
                    break;
                }
                default: {
                    const int trees = 1 + int(rnd() * 3.0f);
                    for (int k = 0; k < trees; ++k) {
                        const vec3 p = base + vec3(rnd() * 8.0f - 4.0f, 0.0f, rnd() * 8.0f - 4.0f);
                        place(rnd() < 0.15f ? "tree_autumn" : pick({"tree_round", "tree_round_2"}), vec3(p.x, ground.height(p.x, p.z), p.z), rnd() * 6.28f, vec3(0.6f + rnd() * 0.5f));
                    }
                    for (int k = 0; k < 3; ++k) {
                        const vec3 p = base + vec3(rnd() * 10.0f - 5.0f, 0.0f, rnd() * 10.0f - 5.0f);
                        const float roll = rnd();
                        place(roll < 0.4f ? "bush" : roll < 0.7f ? "flower_pink" : roll < 0.9f ? "flower_yellow" : "rock", vec3(p.x, ground.height(p.x, p.z), p.z), rnd() * 6.28f,
                              vec3(roll < 0.4f ? 0.7f + rnd() * 0.5f : roll < 0.9f ? 1.6f + rnd() : 0.5f + rnd() * 0.4f));
                    }
                    if (rnd() < 0.12f) place(pick({"hay_bale", "fence", "windmill"}), on_ground(base + vec3(4.0f, 0.0f, 3.0f)), yaw, vec3(1.0f));
                    break;
                }
                }
                continue;
            }
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
                const float r = std::min(6.0f + rnd() * 9.0f, mesa_room(base));
                if (r < 3.0f) break;
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
            case S::Market: break; // market_town() lines the streets
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
    if (th.scenery == S::City && props) { // a starry sky and a moon
        for (int i = 0; i < 420; ++i) {
            const float a = rnd() * 2.0f * kPi, e = 0.08f + std::asin(rnd()) * 0.95f;
            const vec3 p = mid + vec3(std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)) * 650.0f;
            const float r = rnd() < 0.08f ? 1.8f + rnd() * 1.2f : 0.6f + rnd() * 0.9f;
            props->push_back({"night_star", glm::scale(glm::translate(mat4(1.0f), p), vec3(r))});
        }
        const vec3 moon_dir = glm::normalize(vec3(-0.35f, 0.55f, -0.45f));
        props->push_back({"moon", glm::scale(glm::translate(mat4(1.0f), mid + moon_dir * 600.0f), vec3(30.0f))});
    } else if (th.scenery == S::City || th.scenery == S::Lava) {
        for (int i = 0; i < 160; ++i) {
            const float a = rnd() * 2.0f * kPi, e = 0.15f + rnd() * 1.2f;
            const float d = 700.0f;
            const vec3 p = mid + vec3(std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)) * d;
            b.icosphere(p, 1.2f + rnd() * 1.0f, th.scenery == S::City ? rgba(255, 255, 230) : rgba(255, 140, 60));
        }
    }
    if (!ground.valid()) hills(b, t, th, rnd, mid, span, floor_y);
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
            const size_t first_wall_prop = props->size();
            wall_props(t, th, left, right, rnd, *props, [&](float x, float z) { return ground.valid() ? ground.height(x, z) : std::numeric_limits<float>::quiet_NaN(); });
            // With no ground under the track, grandstands stand on columns
            // down to the floor below, like the bridges.
            if (!ground.valid())
                for (size_t k = first_wall_prop; k < props->size(); ++k) {
                    const PropPlacement& p = (*props)[k];
                    if (p.name != "grandstand") continue;
                    b.texture(t.theme == "night" || t.theme == "volcano" || t.theme == "sky" ? TexPanel : TexRock, 2.0f);
                    for (float x : {-2.8f, 2.8f})   // the stand's corners (gobby_props.cpp grandstand)
                        for (float z : {0.0f, -1.8f}) {
                            const vec3 top(p.transform * vec4(x, 0.0f, z, 1.0f));
                            const float h = top.y - floor_y;
                            if (h > 1.0f) b.box(vec3(top.x, floor_y + h * 0.5f, top.z), vec3(0.35f, h * 0.5f, 0.35f), th.wall_b);
                        }
                }
            if (!ground.valid()) roadside_islands(b, t, th, rnd, *props);
            if (th.scenery == ThemeStyle::Scenery::Market) market_town(b, t, th, rnd, floor_y, *props);
            // Obstacles as painted props, fitted to their collision shapes.
            for (const auto& o : t.obstacles) {
                if (o.breakable != sim::Track::Breakable::None) continue; // drawn live: they break
                const bool pillar = o.sides > 4;
                const std::string& th_name = t.theme;
                const char* kind = th_name == "market" ? (pillar ? "fountain" : "crate")
                                   : th_name == "desert" || th_name == "canyon" ? "rock_red"
                                   : th_name == "snow"                       ? "crystal"
                                   : th_name == "volcano"                    ? "lava_rock"
                                   : pillar ? (th_name == "sky" || th_name == "night" ? "tire_stack" : "barrel")
                                            : "crate";
                const float h = 2.0f * o.half.y + 0.3f;
                // -right keeps the frame right-handed, so the faces point outward.
                props->push_back({kind, mat4(vec4(-o.right * o.half.x, 0.0f), vec4(o.up * h, 0.0f), vec4(o.forward * o.half.z, 0.0f), vec4(o.pos, 1.0f)), true});
            }
        }
    }
    return b.take();
}

LavaSpots find_lava(const sim::Track& t) {
    LavaSpots out;
    if (t.theme != "volcano") return out;
    // The pool is the out-of-bounds floor.
    for (const auto& tri : t.triangles)
        if (tri.surface == sim::Surface::OutOfBounds) {
            out.pool = true, out.pool_y = t.vertices[tri.a].y;
            break;
        }
    // Where the ground dips below the pool, near the road: the lava you can see.
    const ThemeStyle& th = theme_style(t.theme);
    if (out.pool && t.ground) {
        const Ground g = make_ground(t, th);
        std::unordered_set<int64_t> seen;
        for (const sim::CenterSample& c : t.road) {
            const float reach = c.half_width + 60.0f;
            const int x0 = int(std::floor((c.pos.x - reach) / kLavaCell)), x1 = int(std::floor((c.pos.x + reach) / kLavaCell));
            const int z0 = int(std::floor((c.pos.z - reach) / kLavaCell)), z1 = int(std::floor((c.pos.z + reach) / kLavaCell));
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    const int64_t key = LavaSpots::key(x, z);
                    if (!seen.insert(key).second) continue;
                    const float cx = (float(x) + 0.5f) * kLavaCell, cz = (float(z) + 0.5f) * kLavaCell;
                    if (g.height(cx, cz) < out.pool_y - 0.3f) out.pool_cells.insert(key);
                }
        }
        // Lights along the pool's edge, ~20 m apart.
        for (const int64_t key : out.pool_cells) {
            const int x = int(key >> 32), z = int(int32_t(key & 0xffffffff));
            const bool edge = !out.pool_cells.count(LavaSpots::key(x + 1, z)) || !out.pool_cells.count(LavaSpots::key(x - 1, z)) ||
                              !out.pool_cells.count(LavaSpots::key(x, z + 1)) || !out.pool_cells.count(LavaSpots::key(x, z - 1));
            if (!edge) continue;
            const vec3 p((float(x) + 0.5f) * kLavaCell, out.pool_y + 1.5f, (float(z) + 0.5f) * kLavaCell);
            bool near = false;
            for (const auto& l : out.lights) near = near || glm::distance(l.pos, p) < 18.0f;
            if (!near) out.lights.push_back({p, -1});
        }
    }
    // Lava zones on the road: their middle line, and a light every ~20 m.
    for (size_t zi = 0; zi < t.zones.size(); ++zi) {
        const auto& z = t.zones[zi];
        if (z.surface != sim::Surface::OutOfBounds) continue;
        for (int i = z.first; i < z.first + z.count; ++i) {
            const sim::CenterSample& c = t.road[size_t(i)];
            if (!c.has_floor) continue;
            const float w = c.half_width + 0.6f;
            const float lo = std::max(z.lo, -w), hi = std::min(z.hi, w);
            const vec3 mid = c.pos + c.right * (0.5f * (lo + hi)) + c.up * 0.05f;
            out.zone_lines.push_back({mid, c.right, c.tangent, 0.5f * (hi - lo), int(zi)});
            if ((i - z.first) % 10 == 0) out.lights.push_back({mid + c.up * 1.2f, int(zi)});
        }
    }
    return out;
}

MeshData make_spark_mesh() {
    MeshBuilder b;
    b.icosphere({0, 0, 0}, 0.12f, rgba(255, 255, 255));
    return b.take();
}

MeshData make_flame_mesh() {
    // A teardrop pointing backwards (-Z), unit long and unit wide, with
    // texture v from the root (0) to the tip (1) for the flame shader.
    MeshBuilder b;
    const int rows = 16, cols = 14;
    std::vector<vec3> pts;
    for (int r = 0; r < rows; ++r) {
        const float s = float(r) / float(rows - 1);
        const float rad = 0.5f * std::pow(std::sin(kPi * std::pow(s, 0.7f)), 0.8f) * (r == rows - 1 ? 0.0f : 1.0f);
        for (int c = 0; c < cols; ++c) {
            const float a = 2.0f * kPi * float(c) / float(cols);
            pts.push_back({std::cos(a) * rad, std::sin(a) * rad, -s});
        }
    }
    b.grid(pts, rows, cols, true, rgba(255, 255, 255), 1.0f, 1.0f);
    return b.take();
}

MeshData make_oil_mesh(const sim::Track& t) {
    MeshBuilder b;
    for (const auto& tri : t.triangles) {
        if (tri.surface != sim::Surface::Oil) continue;
        const vec3 lift(0.0f, 0.03f, 0.0f);
        b.tri(t.vertices[tri.a] + lift, t.vertices[tri.b] + lift, t.vertices[tri.c] + lift, rgba(255, 255, 255));
    }
    return b.take();
}

MeshData make_zone_mesh(const sim::Track& t, const sim::Track::Zone& z) {
    MeshBuilder b;
    const bool lava = z.surface == sim::Surface::OutOfBounds;
    const bool volcano = t.theme == "volcano";
    const uint32_t color = lava ? (volcano ? rgba(255, 120, 30) : rgba(40, 110, 200)) : surface_color(theme_style(t.theme), z.surface);
    const uint32_t tex = lava ? (volcano ? TexLava : TexWater) : surface_texture(t.theme, z.surface);
    b.texture(tex, z.surface == sim::Surface::BoostPad ? 4.0f : 2.0f);
    if (tex == TexWater) {
        // Water swells: points every ~1.5 m across, each knowing how far it
        // is from the zone's edge, a little above the road.
        auto edge = [&](const sim::CenterSample& e, float side) {
            const float w = e.half_width + 0.6f;
            return side < 0.0f ? std::max(z.lo, -w) : std::min(z.hi, w);
        };
        // Deep water: the sheet is the surface, level, over the stretch where
        // the road has gone under it.
        auto covered = [&](int k) {
            const sim::CenterSample& e = t.road[size_t(k)];
            return z.deep ? e.pos.y < z.level - 0.25f : e.has_floor;
        };
        const sim::CenterSample& c0 = t.road[size_t(z.first)];
        const int cols = std::max(2, int(std::ceil((edge(c0, 1) - edge(c0, -1)) / 1.5f)) + 1);
        int i = z.first;
        while (i + 1 < z.first + z.count) {
            if (!covered(i)) {
                ++i;
                continue;
            }
            int j = i;
            while (j + 1 < z.first + z.count && covered(j + 1)) ++j;
            const int rows = j - i + 1;
            if (rows < 2) {
                i = j + 1;
                continue;
            }
            std::vector<vec3> pts;
            std::vector<float> shore;
            for (int r = 0; r < rows; ++r) {
                const sim::CenterSample& e = t.road[size_t(i + r)];
                const float lo = edge(e, -1), hi = edge(e, 1);
                const float ends = 2.0f * float(std::min(r, rows - 1 - r));
                for (int k = 0; k < cols; ++k) {
                    const float lat = lo + (hi - lo) * float(k) / float(cols - 1);
                    if (z.deep) {
                        const vec3 flat = glm::normalize(vec3(e.right.x, 0.0f, e.right.z) + vec3(1e-6f));
                        pts.push_back(vec3(e.pos.x, z.level, e.pos.z) + flat * lat);
                    } else {
                        pts.push_back(e.pos + e.right * lat + e.up * 0.12f);
                    }
                    shore.push_back(std::min({lat - lo, hi - lat, ends}));
                }
            }
            water_grid(b, pts, shore, rows, cols, color);
            i = j + 1;
        }
        return b.take();
    }
    for (int i = z.first; i + 1 < z.first + z.count; ++i) {
        const sim::CenterSample &c = t.road[size_t(i)], &d = t.road[size_t(i + 1)];
        if (!c.has_floor || !d.has_floor) continue;
        auto edge = [&](const sim::CenterSample& e, float side) {
            const float w = e.half_width + 0.6f;
            const float lat = side < 0.0f ? std::max(z.lo, -w) : std::min(z.hi, w);
            return e.pos + e.right * lat + e.up * 0.05f;
        };
        const vec3 a0 = edge(c, -1), a1 = edge(c, 1), b0 = edge(d, -1), b1 = edge(d, 1);
        const vec2 ua(glm::dot(a0 - c.pos, c.right) * 0.5f, c.u * 0.5f), ub(glm::dot(a1 - c.pos, c.right) * 0.5f, c.u * 0.5f),
            uc(glm::dot(b1 - d.pos, d.right) * 0.5f, d.u * 0.5f), ud(glm::dot(b0 - d.pos, d.right) * 0.5f, d.u * 0.5f);
        if (glm::dot(glm::cross(a1 - a0, b1 - a0), c.up) >= 0.0f) b.quad_uv(a0, a1, b1, b0, ua, ub, uc, ud, color);
        else b.quad_uv(a0, b0, b1, a1, ua, ud, uc, ub, color);
    }
    return b.take();
}

MeshData make_barrier_mesh(const sim::Track::Barrier& bar) {
    // In the barrier's frame: x across, y up, z along the road. Red and white
    // panels between yellow posts, the classic closed-road look.
    MeshBuilder b;
    const float hx = bar.half.x, h = 2.0f * bar.half.y, hz = bar.half.z;
    const bool long_fence = hz > hx; // a fence along a road edge rather than across it
    b.texture(TexWhite);
    const float span = long_fence ? hz : hx;
    const int panels = std::max(1, int(span * 2.0f / 1.6f));
    for (int i = 0; i < panels; ++i) {
        const float a = -span + 2.0f * span * float(i) / float(panels), c = a + 2.0f * span / float(panels);
        const uint32_t col = (i & 1) ? rgba(245, 245, 245) : rgba(220, 45, 40);
        const vec3 centre = long_fence ? vec3(0.0f, 0.5f * h, 0.5f * (a + c)) : vec3(0.5f * (a + c), 0.5f * h, 0.0f);
        const vec3 half = long_fence ? vec3(std::min(hx, 0.2f), 0.42f * h, 0.5f * (c - a)) : vec3(0.5f * (c - a), 0.42f * h, std::min(hz, 0.2f));
        b.box(centre, half, col);
    }
    for (int i = 0; i <= panels; i += std::max(1, panels / 4)) {
        const float a = -span + 2.0f * span * float(i) / float(panels);
        const vec3 p = long_fence ? vec3(0.0f, 0.0f, a) : vec3(a, 0.0f, 0.0f);
        b.box(p + vec3(0.0f, 0.5f * h + 0.05f, 0.0f), vec3(0.16f, 0.5f * h + 0.05f, 0.16f), rgba(255, 205, 40));
    }
    b.box({0.0f, h + 0.06f, 0.0f}, long_fence ? vec3(0.12f, 0.06f, hz) : vec3(hx, 0.06f, 0.12f), rgba(255, 205, 40));
    return b.take();
}

MeshData make_switch_mesh(float radius, bool on) {
    // A big round button set in a hazard-striped ring, flat on the road.
    MeshBuilder b;
    b.texture(TexWhite);
    const int seg = 28;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(seg), a1 = 2.0f * kPi * float(i + 1) / float(seg);
        auto P = [](float a, float r, float y) { return vec3(std::cos(a) * r, y, std::sin(a) * r); };
        // Wound to face up and out.
        auto up_tri = [&](vec3 p, vec3 q, vec3 r, uint32_t col) {
            if (glm::cross(q - p, r - p).y < 0.0f) std::swap(q, r);
            b.tri(p, q, r, col);
        };
        auto up_quad = [&](vec3 p, vec3 q, vec3 r, vec3 w, uint32_t col) {
            if (glm::cross(q - p, r - p).y < 0.0f) b.quad(p, w, r, q, col);
            else b.quad(p, q, r, w, col);
        };
        const uint32_t ring = (i / 2) % 2 ? rgba(30, 30, 36) : rgba(255, 200, 30);
        up_quad(P(a0, radius, 0.03f), P(a0, radius * 0.72f, 0.03f), P(a1, radius * 0.72f, 0.03f), P(a1, radius, 0.03f), ring);
        const float top = on ? 0.06f : 0.22f;
        const uint32_t cap = on ? rgba(70, 230, 110) : rgba(230, 60, 50);
        up_tri(vec3(0.0f, top, 0.0f), P(a0, radius * 0.66f, top), P(a1, radius * 0.66f, top), cap);
        up_quad(P(a0, radius * 0.66f, top), P(a0, radius * 0.7f, 0.03f), P(a1, radius * 0.7f, 0.03f), P(a1, radius * 0.66f, top), shade(cap, 0.7f));
    }
    return b.take();
}

MeshData make_oil_disc_mesh() {
    MeshBuilder b;
    const int n = 28;
    std::vector<vec3> rim;
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * kPi * float(i) / float(n);
        const float r = 1.0f + 0.14f * std::sin(a * 3.0f) + 0.07f * std::sin(a * 7.0f + 1.3f);
        rim.push_back({std::cos(a) * r, 0.0f, std::sin(a) * r});
    }
    for (int i = 0; i < n; ++i) b.tri(vec3(0.0f), rim[size_t((i + 1) % n)], rim[size_t(i)], rgba(255, 255, 255));
    return b.take();
}

MeshData make_fireball_mesh() {
    MeshBuilder b;
    b.sphere({0, 0, 0}, 1.0f, rgba(255, 255, 255), 22, 14);
    return b.take();
}

} // namespace gk::render
