#include "track_mesh.h"

#include <cmath>

namespace gk::render {
namespace {

uint32_t surface_color(sim::Surface s) {
    using sim::Surface;
    switch (s) {
    case Surface::Road: return rgba(112, 112, 120);
    case Surface::OffroadLight: return rgba(98, 170, 70);
    case Surface::OffroadMedium: return rgba(150, 140, 110);
    case Surface::OffroadHeavy: return rgba(110, 76, 44);
    case Surface::Sand: return rgba(222, 196, 130);
    case Surface::Ice: return rgba(186, 228, 245);
    case Surface::Water: return rgba(60, 120, 200);
    case Surface::BoostPad: return rgba(255, 170, 30);
    case Surface::GlideRamp: return rgba(60, 130, 255);
    case Surface::TrickRamp: return rgba(240, 220, 60);
    case Surface::Wall: return rgba(220, 50, 50);
    case Surface::OutOfBounds: return rgba(50, 110, 170);
    case Surface::Count: break;
    }
    return rgba(255, 0, 255);
}

uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int shift) { return uint32_t(std::min(255.0f, float((c >> shift) & 255) * f)) << shift; };
    return ch(0) | ch(8) | ch(16) | (c & 0xff000000u);
}

int checker(vec3 p, float cell) {
    return (int(std::floor(p.x / cell)) + int(std::floor(p.z / cell)) + int(std::floor(p.y / cell))) & 1;
}

} // namespace

MeshData make_track_mesh(const sim::Track& t) {
    MeshBuilder b;
    const sim::CenterSample& start = t.center.front();
    for (const auto& tri : t.triangles) {
        const vec3 a = t.vertices[tri.a], c1 = t.vertices[tri.b], c2 = t.vertices[tri.c];
        const vec3 centroid = (a + c1 + c2) / 3.0f;
        uint32_t color = surface_color(tri.surface);
        if (tri.surface == sim::Surface::Wall) {
            color = checker(centroid, 2.0f) ? rgba(230, 230, 230) : rgba(210, 40, 40);
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
            b.tri(a + down, c2 + down, c1 + down, rgba(92, 70, 52)); // underside
        }
    }
    return b.take();
}

MeshData make_scenery_mesh(const sim::Track& t) {
    MeshBuilder b;
    MeshData tree = make_tree_mesh();
    uint32_t seed = 12345u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    };
    // Islands with trees floating below and beside the track.
    for (size_t i = 0; i < t.center.size(); i += 9) {
        const sim::CenterSample& c = t.center[i];
        for (float side : {-1.0f, 1.0f}) {
            if (rnd() < 0.45f) continue;
            const float dist = 26.0f + rnd() * 30.0f;
            const vec3 base = c.pos + c.right * side * dist + vec3(0, -6.0f - rnd() * 14.0f, 0);
            const float r = 3.0f + rnd() * 4.0f;
            b.cone(base, r, -3.5f - rnd() * 3.0f, 6, rgba(110, 82, 58));     // rocky underside (points down)
            b.cone(base, r, 0.4f, 6, rgba(96, 168, 70));                    // grassy top
            const int trees = 1 + int(rnd() * 3.0f);
            for (int k = 0; k < trees; ++k) {
                const vec3 off(rnd() * r - r * 0.5f, 0.2f, rnd() * r - r * 0.5f);
                b.append(tree, glm::scale(glm::translate(mat4(1.0f), base + off), vec3(0.8f + rnd() * 0.6f)));
            }
        }
    }
    // Clouds
    for (int i = 0; i < 40; ++i) {
        const float a = rnd() * 2.0f * kPi;
        const float d = 220.0f + rnd() * 380.0f;
        const vec3 p(80.0f + std::cos(a) * d, 10.0f + rnd() * 50.0f, 50.0f + std::sin(a) * d);
        for (int k = 0; k < 4; ++k)
            b.icosphere(p + vec3(rnd() * 16.0f - 8.0f, rnd() * 3.0f, rnd() * 10.0f - 5.0f), 5.0f + rnd() * 5.0f,
                        rgba(250, 250, 255));
    }
    return b.take();
}

MeshData make_glider_mesh(uint32_t color) {
    MeshBuilder b;
    const uint32_t frame = rgba(60, 60, 70);
    // Swept wing above the driver.
    b.quad({-1.4f, 1.75f, -0.4f}, {1.4f, 1.75f, -0.4f}, {0.5f, 1.85f, 0.5f}, {-0.5f, 1.85f, 0.5f}, color);
    b.quad({-0.5f, 1.85f, 0.5f}, {0.5f, 1.85f, 0.5f}, {1.4f, 1.75f, -0.4f}, {-1.4f, 1.75f, -0.4f}, color);
    b.box({0.35f, 1.4f, 0.0f}, {0.03f, 0.35f, 0.03f}, frame);
    b.box({-0.35f, 1.4f, 0.0f}, {0.03f, 0.35f, 0.03f}, frame);
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
