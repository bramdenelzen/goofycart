#include "track_mesh.h"

#include <cmath>

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

MeshData make_track_mesh(const sim::Track& t) {
    const ThemeStyle& th = theme_style(t.theme);
    MeshBuilder b;
    const sim::CenterSample& start = t.center.front();
    for (const auto& tri : t.triangles) {
        const vec3 a = t.vertices[tri.a], c1 = t.vertices[tri.b], c2 = t.vertices[tri.c];
        const vec3 centroid = (a + c1 + c2) / 3.0f;
        uint32_t color = surface_color(th, tri.surface);
        if (tri.surface == sim::Surface::Wall) {
            color = checker(centroid, 2.0f) ? th.wall_a : th.wall_b;
        } else if (tri.surface == sim::Surface::OutOfBounds) {
            // keep flat
        } else {
            color = shade(color, checker(centroid, 3.0f) ? 1.0f : 0.92f);
            const float along = glm::dot(centroid - start.pos, start.tangent);
            const float across = glm::dot(centroid - start.pos, start.right);
            if (tri.surface == sim::Surface::Road && std::abs(along) < 1.0f && std::abs(across) < start.half_width)
                color = (int(std::floor(across)) & 1) ? rgba(20, 20, 20) : rgba(240, 240, 240);
        }
        b.tri(a, c1, c2, color);
        if (tri.surface == sim::Surface::OutOfBounds) continue;
        if (tri.surface == sim::Surface::Wall) {
            b.tri(a, c2, c1, shade(color, 0.7f)); // outside face
        } else {
            const vec3 down(0.0f, -1.2f, 0.0f);
            b.tri(a + down, c2 + down, c1 + down, th.underside);
        }
    }
    return b.take();
}

MeshData make_scenery_mesh(const sim::Track& t) {
    using S = ThemeStyle::Scenery;
    const ThemeStyle& th = theme_style(t.theme);
    MeshBuilder b;
    MeshData tree = make_tree_mesh();
    Rng rnd{hash_name(t.id) | 1u};
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
            b.box(vec3(top.x, floor_y + h * 0.5f, top.z), vec3(0.8f, h * 0.5f, 0.8f), th.wall_b);
        }
    }

    // Props beside the track, away from the road.
    for (size_t i = 0; i < t.center.size(); i += 9) {
        const sim::CenterSample& c = t.center[i];
        for (float side : {-1.0f, 1.0f}) {
            if (rnd() < 0.45f) continue;
            const float dist = c.half_width + 18.0f + rnd() * 30.0f;
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
                b.cone(base, r, -3.5f - rnd() * 3.0f, 6, rgba(110, 82, 58));
                b.cone(base, r, 0.4f, 6, rgba(96, 168, 70));
                const int trees = 1 + int(rnd() * 3.0f);
                for (int k = 0; k < trees; ++k) {
                    const vec3 off(rnd() * r - r * 0.5f, 0.2f, rnd() * r - r * 0.5f);
                    b.append(tree, glm::scale(glm::translate(mat4(1.0f), base + off), vec3(0.8f + rnd() * 0.6f)));
                }
                break;
            }
            case S::Cactus: {
                base.y = floor_y;
                const float h = 4.0f + rnd() * 5.0f;
                const uint32_t g = rgba(70, 150, 80);
                b.box(base + vec3(0, h * 0.5f, 0), vec3(0.5f, h * 0.5f, 0.5f), g);
                b.box(base + vec3(0.9f, h * 0.55f, 0), vec3(0.4f, 0.3f, 0.4f), g);
                b.box(base + vec3(1.2f, h * 0.7f, 0), vec3(0.3f, h * 0.2f, 0.3f), g);
                b.cone(base, 3.0f + rnd() * 4.0f, 1.2f, 6, rgba(230, 196, 132));
                break;
            }
            case S::Harbor: {
                base.y = floor_y + 0.2f;
                if (rnd() < 0.5f) { // a little boat
                    const float yaw = rnd() * 6.28f;
                    const mat4 m = glm::rotate(glm::translate(mat4(1.0f), base), yaw, kUp);
                    MeshBuilder boat;
                    boat.box({0, 0.5f, 0}, {1.2f, 0.5f, 3.0f}, rgba(240, 240, 240));
                    boat.box({0, 1.4f, -0.5f}, {0.8f, 0.5f, 1.0f}, rgba(220, 60, 50));
                    boat.box({0, 3.0f, 0.8f}, {0.08f, 2.0f, 0.08f}, rgba(90, 70, 50));
                    b.append(boat.take(), m);
                } else { // buoy
                    b.cone(base, 0.8f, 1.8f, 6, rgba(250, 120, 40));
                }
                break;
            }
            case S::Pines: {
                base.y -= 4.0f + rnd() * 10.0f;
                b.cone(base, 5.0f + rnd() * 3.0f, -4.0f, 6, rgba(120, 120, 130));
                b.cone(base, 5.0f, 0.5f, 6, rgba(240, 245, 252));
                for (int k = 0; k < 2; ++k) {
                    const vec3 p = base + vec3(rnd() * 4.0f - 2.0f, 0.4f, rnd() * 4.0f - 2.0f);
                    b.cone(p, 1.6f, 3.0f, 6, rgba(40, 100, 70));
                    b.cone(p + vec3(0, 2.0f, 0), 1.2f, 2.6f, 6, rgba(46, 112, 78));
                    b.cone(p + vec3(0, 3.8f, 0), 0.7f, 1.4f, 6, rgba(240, 245, 252));
                }
                break;
            }
            case S::Mesas: {
                base.y = floor_y;
                const float h = (c.pos.y - floor_y) + 6.0f + rnd() * 25.0f;
                const float r = 5.0f + rnd() * 8.0f;
                for (int k = 0; k < 3; ++k) {
                    const float seg = h / 3.0f;
                    b.cone(base + vec3(0, seg * float(k), 0), r * (1.0f - 0.12f * float(k)), seg * 1.05f, 7,
                           k % 2 ? rgba(186, 104, 64) : rgba(206, 124, 78));
                }
                b.cone(base + vec3(0, h, 0), r * 0.64f, 0.5f, 7, rgba(216, 150, 96));
                break;
            }
            case S::Lava: {
                base.y = floor_y;
                const float h = (c.pos.y - floor_y) + 2.0f + rnd() * 14.0f;
                b.cone(base, 2.5f + rnd() * 3.0f, h, 5, rgba(46, 38, 40));
                break;
            }
            case S::Clouds: {
                base.y = c.pos.y - 8.0f - rnd() * 16.0f;
                for (int k = 0; k < 4; ++k)
                    b.icosphere(base + vec3(rnd() * 10.0f - 5.0f, rnd() * 2.0f, rnd() * 10.0f - 5.0f), 3.0f + rnd() * 4.0f,
                                rgba(250, 250, 255));
                break;
            }
            case S::City: {
                base.y = floor_y;
                const float h = (c.pos.y - floor_y) + 8.0f + rnd() * 40.0f;
                const float w = 4.0f + rnd() * 5.0f;
                b.box(base + vec3(0, h * 0.5f, 0), vec3(w, h * 0.5f, w), rgba(30, 30, 46));
                // Lit windows on the side facing the track.
                const vec3 face = glm::normalize(vec3(c.pos.x - base.x, 0, c.pos.z - base.z));
                const vec3 across = glm::cross(kUp, face);
                for (float wy = 3.0f; wy < h - 2.0f; wy += 3.0f)
                    for (float wx = -w + 1.0f; wx < w - 1.0f; wx += 2.0f)
                        if (rnd() < 0.55f) {
                            const vec3 p = base + face * (w + 0.05f) + across * wx + vec3(0, wy, 0);
                            const uint32_t lit = rnd() < 0.5f ? rgba(255, 220, 120) : rgba(120, 220, 255);
                            b.quad(p - across * 0.5f, p + across * 0.5f, p + across * 0.5f + kUp * 1.2f,
                                   p - across * 0.5f + kUp * 1.2f, lit);
                        }
                break;
            }
            }
        }
    }
    // Sky decoration: clouds by day, stars at night.
    const int clouds = th.scenery == S::City ? 0 : th.scenery == S::Clouds ? 70 : th.scenery == S::Lava ? 0 : 40;
    for (int i = 0; i < clouds; ++i) {
        const float a = rnd() * 2.0f * kPi;
        const float d = span * 0.8f + 120.0f + rnd() * 380.0f;
        const vec3 p(mid.x + std::cos(a) * d, mid.y + 10.0f + rnd() * 50.0f, mid.z + std::sin(a) * d);
        for (int k = 0; k < 4; ++k)
            b.icosphere(p + vec3(rnd() * 16.0f - 8.0f, rnd() * 3.0f, rnd() * 10.0f - 5.0f), 5.0f + rnd() * 5.0f,
                        rgba(250, 250, 255));
    }
    if (th.scenery == S::City || th.scenery == S::Lava) {
        for (int i = 0; i < 160; ++i) {
            const float a = rnd() * 2.0f * kPi, e = 0.15f + rnd() * 1.2f;
            const float d = 700.0f;
            const vec3 p = mid + vec3(std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)) * d;
            b.icosphere(p, 1.2f + rnd() * 1.0f, th.scenery == S::City ? rgba(255, 255, 230) : rgba(255, 140, 60));
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
