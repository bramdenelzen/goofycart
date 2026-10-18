#include "gobby_assets.h"
#include "gobby_util.h"

#include <atomic>
#include <thread>

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace gk::render {

void PaintedModel::finish() {
    bounds_min = vec3(1e30f);
    bounds_max = vec3(-1e30f);
    for (const auto& v : mesh.vertices) {
        bounds_min = glm::min(bounds_min, v.pos);
        bounds_max = glm::max(bounds_max, v.pos);
    }
    if (mesh.vertices.empty()) bounds_min = bounds_max = vec3(0.0f);
}

namespace {

using namespace gobby;

PaintedModel bullet() {
    // A rocket with a grin, 2.8 m long, axis at y = 0.8, nose at +z.
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const float y = 0.8f;
    const UvRect body = span(0, 0, 2, 4);
    region(b, body);
    // Tail to nose: a flat tail, a straight body, a rounded nose.
    std::vector<vec2> prof{{0.0f, -1.35f}, {0.42f, -1.35f}, {0.55f, -1.2f}, {0.55f, 0.85f}};
    for (int i = 1; i <= 8; ++i) {
        const float a = kPi * 0.5f * float(i) / 8.0f;
        prof.push_back({0.55f * std::cos(a), 0.85f + 0.6f * std::sin(a)});
    }
    b.lathe({0, y, 0}, {0, 0, 1}, prof, 32, kWhite, 1.0f, 1.0f);
    // v = 0 is the tail, v = 1 the nose. Lathe u around the z axis: for axis z, u = 0 at +x... the face goes at u where +y: cols run z-> (u=cross(ref,a)=x), v=cross(a,u)=y, so +y is u = 0.25.
    t.gradient_u(body, col(30, 32, 44), col(20, 22, 32));
    t.glow(body, {0.25f, 0.5f}, 0.5f, vec4(0.6f, 0.65f, 0.9f, 0.3f), 1.0f);
    t.band_v(body, 0.0f, 0.1f, col(225, 60, 50), 0.01f);
    t.band_v(body, 0.1f, 0.13f, col(250, 210, 80), 0.005f);
    t.band_v(body, 0.72f, 0.75f, col(250, 210, 80), 0.005f);
    // Angry eyes on the sides of the nose (u around: 0 = +x side, 0.5 = -x side; eyes slightly up at u = 0.1 and 0.4).
    t.eye(body, {0.15f, 0.8f}, {0.06f, 0.055f}, col(240, 240, 240), 0.4f);
    t.eye(body, {0.35f, 0.8f}, {0.06f, 0.055f}, col(240, 240, 240), 0.4f);
    t.mouth(body, {0.75f, 0.84f}, 0.22f, 0.03f, 0.018f, col(250, 250, 250));
    // Fins and two little arms.
    const UvRect fin = span(2, 0, 4, 2);
    region(b, fin);
    for (int i = 0; i < 4; ++i) {
        const float a = kPi * 0.5f * float(i) + kPi * 0.25f;
        const vec3 d(std::cos(a), std::sin(a), 0);
        const vec3 root = vec3(0, y, 0) + d * 0.5f;
        leaf_quad(b, root + vec3(0, 0, -1.3f), root + d * 0.35f + vec3(0, 0, -1.35f), root + d * 0.35f + vec3(0, 0, -1.0f), root + vec3(0, 0, -0.7f), kWhite);
    }
    t.gradient_v(fin, col(230, 70, 55), col(170, 40, 35));
    const UvRect arm = span(2, 2, 4, 4);
    region(b, arm);
    for (float x : {-1.0f, 1.0f}) {
        b.capsule({x * 0.5f, y - 0.1f, 0.2f}, {x * 0.75f, y - 0.35f, 0.55f}, 0.08f, kWhite, 12, 4);
        b.sphere({x * 0.78f, y - 0.4f, 0.62f}, 0.14f, kWhite, 14, 10);
    }
    shade_round(t, arm, col(235, 235, 240), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel chomper() {
    // A snapper plant in a pot, 0.9 m tall, jaws open towards +z.
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect pot = span(0, 0, 2, 1);
    region(b, pot);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.14f, 0.0f}, {0.17f, 0.2f}, {0.2f, 0.22f}, {0.2f, 0.27f}, {0.0f, 0.27f}}, 20, kWhite, 1.0f, 1.0f);
    t.gradient_v(pot, col(150, 80, 50), col(215, 120, 70));
    t.band_v(pot, 0.78f, 0.95f, col(230, 140, 85), 0.02f);
    t.band_v(pot, 0.95f, 1.0f, col(70, 40, 25), 0.0f);
    const UvRect stem = span(2, 0, 3, 1);
    region(b, stem);
    b.sweep({{0, 0.25f, 0}, {0, 0.42f, -0.03f}, {0.01f, 0.56f, 0.02f}, {0, 0.64f, 0.06f}}, {0.03f, 0.035f, 0.035f, 0.045f}, kWhite, 10);
    t.gradient_u(stem, col(90, 170, 70), col(50, 120, 45));
    // Head: a red sphere with white spots; mouth cut into the front.
    const vec3 hc(0, 0.72f, 0.1f);
    const float hr = 0.17f;
    const UvRect head = span(0, 1, 2, 3);
    region(b, head);
    b.sphere(hc, vec3(hr, hr * 0.95f, hr * 1.05f), kWhite, 32, 20);
    shade_round(t, head, col(225, 40, 40), 1.3f);
    t.spots(head, 10, 0.07f, col(255, 255, 255), 55, 0.25f);
    // Lips: a white ring on the front, dark mouth inside.
    t.ellipse(head, {0.25f, 0.55f}, {0.11f, 0.2f}, col(250, 250, 250), 0.03f);
    t.ellipse(head, {0.25f, 0.55f}, {0.08f, 0.15f}, col(60, 10, 20), 0.03f);
    for (int i = 0; i < 5; ++i) { // teeth
        const float u = 0.19f + 0.03f * float(i);
        t.ellipse(head, {u, 0.44f}, {0.012f, 0.025f}, col(255, 255, 255), 0.01f);
        t.ellipse(head, {u + 0.015f, 0.67f}, {0.012f, 0.025f}, col(255, 255, 255), 0.01f);
    }
    // Leaves.
    const UvRect leaf = span(2, 1, 4, 2);
    region(b, leaf);
    for (float a : {0.6f, 2.4f, 4.1f}) {
        const vec3 d(std::cos(a), 0.0f, std::sin(a));
        const vec3 s(-std::sin(a), 0.0f, std::cos(a));
        const vec3 root(0, 0.33f, 0);
        leaf_quad(b, root + s * 0.03f, root + d * 0.3f + vec3(0, 0.12f, 0), root + d * 0.3f + vec3(0, 0.12f, 0) + s * 0.001f, root - s * 0.03f + d * 0.1f + vec3(0, 0.04f, 0), kWhite);
        leaf_quad(b, root - s * 0.03f, root + d * 0.1f + vec3(0, 0.04f, 0), root + d * 0.3f + vec3(0, 0.12f, 0), root + s * 0.09f + d * 0.15f + vec3(0, 0.06f, 0), kWhite);
    }
    t.gradient_u(leaf, col(70, 160, 60), col(40, 110, 40));
    t.band_v(leaf, 0.47f, 0.53f, col(120, 200, 90, 0.7f), 0.02f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel boomerang() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect body = span(0, 0, 4, 2);
    region(b, body);
    std::vector<vec3> path;
    std::vector<vec2> sizes;
    for (int i = 0; i <= 12; ++i) {
        const float s = float(i) / 12.0f;
        const float a = (s - 0.5f) * 1.9f;
        path.push_back({std::sin(a) * 0.55f, 0.0f, (std::cos(a) - 0.7f) * 0.45f});
        sizes.push_back({0.09f - 0.03f * std::abs(s - 0.5f) * 2.0f, 0.035f});
    }
    ribbon(b, path, sizes, kWhite, 10);
    t.gradient_v(body, col(70, 140, 250), col(30, 80, 190));
    t.band_v(body, 0.0f, 0.12f, col(250, 210, 60), 0.02f);
    t.band_v(body, 0.88f, 1.0f, col(250, 210, 60), 0.02f);
    t.band_v(body, 0.47f, 0.53f, col(250, 250, 250, 0.8f), 0.02f);
    t.glow(body, {0.5f, 0.3f}, 0.4f, vec4(1.0f, 1.0f, 1.0f, 0.25f), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel mushroom(vec4 cap_color, vec4 spot_color, bool face, bool gold) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect cap = span(0, 0, 2, 2);
    region(b, cap);
    b.lathe({0, 0.26f, 0}, {0, 1, 0}, {{0.0f, -0.04f}, {0.28f, -0.03f}, {0.31f, 0.02f}, {0.3f, 0.12f}, {0.22f, 0.25f}, {0.1f, 0.32f}, {0.0f, 0.34f}}, 28, kWhite, 1.0f, 1.0f);
    shade_round(t, cap, cap_color, 1.3f);
    if (!gold) t.spots(cap, 7, 0.13f, spot_color, 31, 0.15f);
    else {
        t.glow(cap, {0.25f, 0.4f}, 0.45f, vec4(1.0f, 1.0f, 0.8f, 0.5f), 1.0f);
        t.band_v(cap, 0.55f, 0.6f, col(255, 240, 160, 0.6f), 0.03f);
    }
    t.band_v(cap, 0.0f, 0.1f, col(240, 230, 200), 0.02f); // the cap's underside rim
    const UvRect stem = span(2, 0, 4, 2);
    region(b, stem);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.19f, 0.0f}, {0.2f, 0.12f}, {0.17f, 0.25f}, {0.0f, 0.26f}}, 24, kWhite, 1.0f, 1.0f);
    shade_round(t, stem, gold ? col(245, 225, 150) : col(248, 240, 220), 0.9f);
    if (face) {
        t.eye(stem, {0.19f, 0.45f}, {0.035f, 0.07f}, col(30, 30, 40), 0.0f);
        t.eye(stem, {0.31f, 0.45f}, {0.035f, 0.07f}, col(30, 30, 40), 0.0f);
        t.mouth(stem, {0.25f, 0.66f}, 0.07f, 0.03f, 0.012f, col(60, 30, 30, 0.9f));
    }
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel star() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect body = span(0, 0, 4, 4);
    region(b, body);
    // A puffy five-point star: a grid of rings from a centre point outwards,
    // thickness falling off towards the tips.
    std::vector<std::vector<vec3>> rings;
    const int cols = 40;
    for (int side = -1; side <= 1; side += 2)
        for (int r = 0; r <= 6; ++r) {
            const float s = side < 0 ? float(r) / 6.0f : 1.0f - float(r) / 6.0f; // 0 centre .. 1 tip
            std::vector<vec3> ring;
            for (int c = 0; c < cols; ++c) {
                const float a = 2.0f * kPi * float(c) / float(cols);
                const float k = std::cos(5.0f * (a + kPi * 0.5f));
                const float rr = 0.34f * (0.6f + 0.4f * (k > 0 ? std::pow(k, 0.6f) : 0.0f)) * s;
                const float thick = 0.1f * std::sqrt(std::max(0.0f, 1.0f - s * s * 0.9f));
                ring.push_back({std::cos(a) * rr, 0.36f - std::sin(a) * rr, float(side) * thick});
            }
            rings.push_back(ring);
        }
    b.loft(rings, kWhite, true);
    t.gradient_v(body, col(255, 235, 90), col(255, 190, 40));
    t.glow(body, {0.5f, 0.4f}, 0.35f, vec4(1.0f, 1.0f, 0.9f, 0.45f), 1.0f);
    // The grid's u runs around the outline and v from the back face to the
    // front: eyes near the centre on the front half.
    t.eye(body, {0.53f, 0.82f}, {0.03f, 0.05f}, col(30, 30, 40), 0.0f);
    t.eye(body, {0.47f, 0.82f}, {0.03f, 0.05f}, col(30, 30, 40), 0.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel lightning() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect bolt = span(0, 0, 4, 4);
    region(b, bolt);
    // A zig-zag bolt, two-sided and a little thick.
    const std::vector<vec2> outline{{0.05f, 0.8f}, {-0.2f, 0.8f}, {-0.05f, 0.45f}, {-0.22f, 0.45f}, {0.0f, 0.0f}, {0.08f, 0.32f}, {-0.04f, 0.32f}, {0.22f, 0.8f}};
    std::vector<std::vector<vec3>> rings;
    for (float z : {-0.03f, 0.03f}) {
        std::vector<vec3> ring;
        for (const vec2& p : outline) ring.push_back({p.x, p.y, z});
        rings.push_back(ring);
    }
    b.loft(rings, kWhite, true);
    t.gradient_v(bolt, col(255, 250, 160), col(255, 200, 40));
    t.glow(bolt, {0.5f, 0.5f}, 0.5f, vec4(1.0f, 1.0f, 1.0f, 0.35f), 1.0f);
    m.mesh = b.take();
    m.two_sided = true;
    m.finish();
    return m;
}

PaintedModel blooper() {
    // An inky squid, 0.8 m, facing +z.
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect head = span(0, 0, 2, 2);
    region(b, head);
    b.lathe({0, 0.3f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.18f, 0.0f}, {0.2f, 0.15f}, {0.15f, 0.35f}, {0.06f, 0.48f}, {0.0f, 0.52f}}, 24, kWhite, 1.0f, 1.0f);
    shade_round(t, head, col(245, 245, 250), 0.9f);
    t.eye(head, {0.2f, 0.78f}, {0.04f, 0.08f}, col(30, 30, 40), 0.3f);
    t.eye(head, {0.3f, 0.78f}, {0.04f, 0.08f}, col(30, 30, 40), 0.3f);
    const UvRect tent = span(2, 0, 4, 2);
    region(b, tent);
    for (int i = 0; i < 6; ++i) {
        const float a = 2.0f * kPi * float(i) / 6.0f;
        const vec3 d(std::cos(a), 0.0f, std::sin(a));
        b.sweep({vec3(0, 0.32f, 0) + d * 0.1f, vec3(0, 0.18f, 0) + d * 0.17f, vec3(0, 0.05f, 0) + d * 0.14f}, {0.045f, 0.035f, 0.015f}, kWhite, 10);
    }
    t.gradient_v(tent, col(240, 240, 248), col(180, 185, 200));
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel horn() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect bell = span(0, 0, 2, 2);
    region(b, bell);
    b.lathe({0, 0.25f, 0}, {0, 0, 1}, {{0.0f, -0.3f}, {0.06f, -0.3f}, {0.07f, -0.05f}, {0.12f, 0.12f}, {0.22f, 0.26f}, {0.3f, 0.3f}, {0.26f, 0.3f}, {0.0f, 0.22f}}, 28, kWhite, 1.0f, 1.0f);
    shade_round(t, bell, col(230, 60, 50), 1.2f);
    t.band_v(bell, 0.83f, 0.9f, col(250, 210, 70), 0.01f);
    t.band_v(bell, 0.0f, 0.2f, col(200, 200, 210), 0.01f);
    const UvRect grip = span(2, 0, 3, 1);
    region(b, grip);
    b.torus({0, 0.25f, -0.15f}, {0, 0, 1}, 0.11f, 0.03f, kWhite, 20, 8);
    t.fill(grip, col(50, 50, 60));
    m.mesh = b.take();
    m.finish();
    return m;
}

// An oil can: a squat black drum with a yellow cap and a drip, held behind
// the kart and tipped out as a slick.
PaintedModel oil_can() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect drum = span(0, 0, 2, 2), cap = span(2, 0, 3, 1), label = span(0, 2, 2, 3);
    region(b, drum);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.2f, 0.0f}, {0.24f, 0.05f}, {0.25f, 0.42f}, {0.2f, 0.5f}, {0.0f, 0.5f}}, 24, kWhite, 1.0f, 1.0f);
    t.gradient_v(drum, col(55, 52, 62), col(22, 20, 26));
    t.band_u(drum, 0.1f, 0.25f, col(120, 118, 130, 0.35f), 0.06f); // a highlight down one side
    region(b, label);
    b.lathe({0, 0.15f, 0}, {0, 1, 0}, {{0.252f, 0.0f}, {0.252f, 0.2f}}, 24, kWhite, 1.0f, 1.0f);
    t.fill(label, col(240, 190, 40));
    t.ellipse(label, {0.25f, 0.5f}, {0.05f, 0.09f}, col(30, 28, 34), 0.02f);   // a drop
    t.ellipse(label, {0.75f, 0.5f}, {0.05f, 0.09f}, col(30, 28, 34), 0.02f);
    region(b, cap);
    b.lathe({0.12f, 0.5f, 0.0f}, {0, 1, 0}, {{0.0f, 0.0f}, {0.07f, 0.0f}, {0.07f, 0.08f}, {0.0f, 0.08f}}, 12, kWhite, 1.0f, 1.0f);
    t.gradient_v(cap, col(250, 200, 50), col(200, 150, 30));
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel coin() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect face = span(0, 0, 2, 2);
    region(b, face);
    const float r = 0.25f;
    // Two faces as fans with planar UVs, and a rim.
    const int n = 32;
    for (float side : {1.0f, -1.0f}) {
        for (int i = 0; i < n; ++i) {
            const float a0 = 2.0f * kPi * float(i) / float(n), a1 = 2.0f * kPi * float(i + 1) / float(n);
            const vec3 c(0, 0.3f, side * 0.04f), p0(std::cos(a0) * r, 0.3f + std::sin(a0) * r, side * 0.04f), p1(std::cos(a1) * r, 0.3f + std::sin(a1) * r, side * 0.04f);
            const vec2 uc(0.5f, 0.5f), u0(0.5f + 0.5f * std::cos(a0), 0.5f - 0.5f * std::sin(a0)), u1(0.5f + 0.5f * std::cos(a1), 0.5f - 0.5f * std::sin(a1));
            if (side > 0) b.tri_uv(c, p0, p1, uc, u0, u1, kWhite);
            else b.tri_uv(c, p1, p0, uc, u1, u0, kWhite);
        }
    }
    t.fill(face, col(250, 205, 60));
    t.glow(face, {0.35f, 0.35f}, 0.45f, vec4(1.0f, 1.0f, 0.85f, 0.4f), 1.0f);
    t.ellipse(face, {0.5f, 0.5f}, {0.4f, 0.4f}, col(215, 160, 30), 0.02f);
    t.ellipse(face, {0.5f, 0.5f}, {0.34f, 0.34f}, col(250, 205, 60), 0.02f);
    t.star(face, {0.5f, 0.5f}, 0.22f, col(225, 170, 40));
    const UvRect rim = span(2, 0, 4, 1);
    region(b, rim);
    b.lathe({0, 0.3f, 0}, {0, 0, 1}, {{r, -0.04f}, {r, 0.04f}}, 32, kWhite, 1.0f, 1.0f);
    t.stripes_u(rim, 32, col(240, 190, 50), col(205, 150, 30));
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel fireball() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect body = span(0, 0, 2, 2);
    region(b, body);
    b.sphere({0, 0, 0}, 0.3f, kWhite, 24, 16);
    t.gradient_v(body, col(255, 240, 120), col(255, 90, 20));
    t.noise(body, 0.6f, 6.0f, 9, col(255, 120, 30));
    t.glow(body, {0.5f, 0.3f}, 0.4f, vec4(1.0f, 1.0f, 0.7f, 0.5f), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace

void parallel_for(int n, const std::function<void(int)>& job) {
    const int workers = std::max(1, std::min(n, int(std::thread::hardware_concurrency())));
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back([&] {
            for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) job(i);
        });
    for (std::thread& t : pool) t.join();
}

std::map<std::string, PaintedModel> build_models_parallel(std::vector<ModelJob> jobs) {
    std::vector<PaintedModel> built(jobs.size());
    parallel_for(int(jobs.size()), [&](int i) { built[size_t(i)] = jobs[size_t(i)].second(); });
    std::map<std::string, PaintedModel> out;
    for (size_t i = 0; i < jobs.size(); ++i) out[jobs[i].first] = std::move(built[i]);
    return out;
}

std::map<std::string, PaintedModel> make_gobby_items() {
    std::vector<ModelJob> jobs;
    jobs.emplace_back("banana", [] { return make_banana_peel(); });
    jobs.emplace_back("green_shell", [] { return make_turtle_shell(col(52, 150, 52), false); });
    jobs.emplace_back("red_shell", [] { return make_turtle_shell(col(178, 32, 38), false); });
    jobs.emplace_back("spiked_shell", [] { return make_turtle_shell(col(32, 58, 170), true); });
    jobs.emplace_back("bomb", [] { return make_tnt_bundle(); });
    jobs.emplace_back("item_box", [] { return make_glass_item_box(); });
    jobs.emplace_back("bullet", [] { return bullet(); });
    jobs.emplace_back("chomper", [] { return chomper(); });
    jobs.emplace_back("boomerang", [] { return boomerang(); });
    jobs.emplace_back("mushroom", [] { return mushroom(col(230, 50, 50), col(255, 255, 255), true, false); });
    jobs.emplace_back("golden_mushroom", [] { return mushroom(col(250, 200, 60), col(255, 255, 255), true, true); });
    jobs.emplace_back("star", [] { return star(); });
    jobs.emplace_back("lightning", [] { return lightning(); });
    jobs.emplace_back("blooper", [] { return blooper(); });
    jobs.emplace_back("horn", [] { return horn(); });
    jobs.emplace_back("coin", [] { return coin(); });
    jobs.emplace_back("oil_slick", [] { return oil_can(); });
    jobs.emplace_back("fireball", [] { return fireball(); });
    return build_models_parallel(std::move(jobs));
}

bool export_painted_model(const PaintedModel& model, const std::string& dir, const std::string& name, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path folder = fs::path(dir) / name;
    fs::create_directories(folder, ec);
    if (ec) {
        if (error) *error = "cannot create " + folder.string();
        return false;
    }
    const std::string png = name + ".png";
    if (!stbi_write_png((folder / png).string().c_str(), model.texture.size(), model.texture.size(), 4, model.texture.rgba().data(),
                        model.texture.size() * 4)) {
        if (error) *error = "cannot write " + (folder / png).string();
        return false;
    }
    std::ofstream mtl(folder / (name + ".mtl"));
    mtl << "newmtl " << name << "\nKd 1 1 1\nmap_Kd " << png << "\n";
    std::ofstream obj(folder / (name + ".obj"));
    if (!obj) {
        if (error) *error = "cannot write " + (folder / (name + ".obj")).string();
        return false;
    }
    obj << "# Gobby Kart model: " << name << "\nmtllib " << name << ".mtl\n";
    for (const auto& v : model.mesh.vertices) obj << "v " << v.pos.x << ' ' << v.pos.y << ' ' << v.pos.z << '\n';
    for (const auto& v : model.mesh.vertices) obj << "vt " << v.uv.x << ' ' << 1.0f - v.uv.y << '\n';
    for (const auto& v : model.mesh.vertices) obj << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
    obj << "usemtl " << name << "\n";
    for (size_t i = 0; i + 2 < model.mesh.indices.size(); i += 3) {
        const uint32_t a = model.mesh.indices[i] + 1, b = model.mesh.indices[i + 1] + 1, c = model.mesh.indices[i + 2] + 1;
        obj << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b << '/' << b << ' ' << c << '/' << c << '/' << c << '\n';
    }
    return true;
}

} // namespace gk::render
