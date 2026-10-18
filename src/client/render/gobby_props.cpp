#include "gobby_assets.h"
#include "gobby_util.h"

namespace gk::render {
namespace {

using namespace gobby;

// Bark: a brown trunk with vertical grain and a few knots.
void paint_bark(Canvas& t, const UvRect& r, vec4 base) {
    t.gradient_u(r, shade(base, 1.15f), shade(base, 0.8f));
    for (int i = 0; i < 9; ++i) t.band_u(r, float(i) / 9.0f + 0.03f, float(i) / 9.0f + 0.05f, shade(base, 0.6f), 0.01f);
    t.noise(r, 0.25f, 5.0f, 3, base);
    t.spots(r, 3, 0.05f, shade(base, 0.55f), 11, 0.4f);
    t.band_v(r, 0.0f, 0.12f, vec4(0.0f, 0.0f, 0.0f, 0.3f), 0.1f);
}

// Foliage: a green ball with lighter leaf clumps on top and shadow below.
void paint_leaves(Canvas& t, const UvRect& r, vec4 base) {
    t.gradient_v(r, shade(base, 1.2f), shade(base, 0.55f));
    t.noise(r, 0.35f, 7.0f, 21, base);
    t.spots(r, 14, 0.08f, shade(base, 1.35f), 5, 0.6f);
    t.spots(r, 8, 0.06f, shade(base, 0.7f), 9, 0.6f);
    t.glow(r, {0.25f, 0.25f}, 0.3f, vec4(1.0f, 1.0f, 0.8f, 0.25f), 1.0f);
}

void paint_stone(Canvas& t, const UvRect& r, vec4 base) {
    t.gradient_v(r, shade(base, 1.15f), shade(base, 0.7f));
    t.noise(r, 0.45f, 6.0f, 31, base);
    t.spots(r, 10, 0.07f, shade(base, 0.8f), 15, 0.7f);
    t.spots(r, 6, 0.04f, shade(base, 1.25f), 17, 0.6f);
    t.jagged(r, {0.1f, 0.3f}, {0.6f, 0.8f}, 0.006f, shade(base, 0.45f), 41, 0.06f);
    t.jagged(r, {0.5f, 0.1f}, {0.95f, 0.5f}, 0.005f, shade(base, 0.45f), 43, 0.06f);
}

PaintedModel tree_round(uint32_t seed, vec4 leaf, float height) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect bark = span(0, 0, 1, 4);
    region(b, bark);
    const float trunk_h = height * 0.42f;
    b.lathe({0, -0.2f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.32f, 0.0f}, {0.26f, 0.3f}, {0.2f, trunk_h * 0.6f}, {0.17f, trunk_h + 0.4f}, {0.0f, trunk_h + 0.45f}}, 14, kWhite, 1.0f, 1.0f);
    paint_bark(t, bark, col(120, 78, 46));
    const UvRect leaves = span(1, 0, 4, 4);
    region(b, leaves);
    const float r = height * 0.33f;
    const vec3 c(0, trunk_h + r * 0.85f, 0);
    lumpy_sphere(b, c, vec3(r, r * 0.9f, r), 0.08f, seed, kWhite, 26, 18);
    lumpy_sphere(b, c + vec3(r * 0.55f, -r * 0.25f, r * 0.1f), vec3(r * 0.62f), 0.1f, seed + 1, kWhite, 18, 12);
    lumpy_sphere(b, c + vec3(-r * 0.5f, -r * 0.2f, -r * 0.3f), vec3(r * 0.6f), 0.1f, seed + 2, kWhite, 18, 12);
    lumpy_sphere(b, c + vec3(r * 0.1f, r * 0.5f, -r * 0.4f), vec3(r * 0.55f), 0.1f, seed + 3, kWhite, 18, 12);
    paint_leaves(t, leaves, leaf);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel tree_pine(bool snowy) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect bark = span(0, 0, 1, 4);
    region(b, bark);
    b.lathe({0, -0.2f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.28f, 0.0f}, {0.2f, 0.4f}, {0.14f, 2.0f}, {0.0f, 2.1f}}, 12, kWhite, 1.0f, 1.0f);
    paint_bark(t, bark, col(95, 62, 40));
    const UvRect needles = span(1, 0, 4, 4);
    region(b, needles);
    struct Tier { float y, r, h; };
    for (const Tier& tier : {Tier{1.0f, 1.9f, 2.4f}, Tier{2.4f, 1.5f, 2.1f}, Tier{3.7f, 1.05f, 1.8f}, Tier{4.9f, 0.6f, 1.3f}}) {
        std::vector<vec2> prof{{0.0f, 0.0f}, {tier.r * 0.75f, 0.1f}, {tier.r, 0.18f * tier.h}, {tier.r * 0.78f, 0.5f * tier.h}, {tier.r * 0.42f, 0.8f * tier.h}, {0.0f, tier.h}};
        b.lathe({0, tier.y, 0}, {0, 1, 0}, prof, 16, kWhite, 1.0f, 1.0f);
    }
    const vec4 green = snowy ? col(40, 105, 70) : col(42, 120, 60);
    t.gradient_v(needles, shade(green, 0.7f), shade(green, 1.25f));
    for (int i = 0; i < 10; ++i) t.band_v(needles, float(i) / 10.0f + 0.04f, float(i) / 10.0f + 0.06f, shade(green, 0.5f), 0.01f);
    t.noise(needles, 0.3f, 6.0f, 23, green);
    if (snowy) {
        t.band_v(needles, 0.86f, 1.0f, col(240, 246, 255), 0.06f); // snow on the top of each tier
        t.spots(needles, 10, 0.05f, col(240, 246, 255, 0.8f), 27, 0.5f);
    }
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel tree_palm() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect bark = span(0, 0, 1, 4);
    region(b, bark);
    std::vector<vec3> path;
    std::vector<float> radii;
    for (int i = 0; i <= 8; ++i) {
        const float s = float(i) / 8.0f;
        path.push_back({s * s * 1.2f, s * 5.0f, 0.0f});
        radii.push_back(0.26f - 0.1f * s);
    }
    b.sweep(path, radii, kWhite, 12, true);
    t.gradient_u(bark, col(170, 130, 85), col(110, 80, 50));
    for (int i = 0; i < 14; ++i) t.band_v(bark, float(i) / 14.0f, float(i) / 14.0f + 0.025f, col(80, 55, 35), 0.005f);
    const UvRect frond = span(1, 0, 4, 2);
    region(b, frond);
    const vec3 top = path.back();
    for (int i = 0; i < 7; ++i) {
        const float a = 2.0f * kPi * float(i) / 7.0f + 0.3f;
        const vec3 d(std::cos(a), 0.0f, std::sin(a));
        std::vector<vec3> fp;
        std::vector<vec2> fs;
        for (int k = 0; k <= 6; ++k) {
            const float s = float(k) / 6.0f;
            fp.push_back(top + d * (s * 2.6f) + vec3(0, 0.6f * s - 1.6f * s * s, 0));
            fs.push_back({0.02f + 0.03f * (1.0f - s), 0.5f * std::sin(s * kPi * 0.9f + 0.1f) + 0.05f});
        }
        ribbon(b, fp, fs, kWhite, 8);
    }
    t.gradient_v(frond, col(60, 150, 60), col(30, 100, 40));
    for (int i = 0; i < 24; ++i) t.band_v(frond, float(i) / 24.0f, float(i) / 24.0f + 0.015f, col(25, 80, 35, 0.8f), 0.004f);
    const UvRect nuts = span(1, 2, 2, 3);
    region(b, nuts);
    for (int i = 0; i < 3; ++i) b.sphere(top + vec3(std::cos(float(i) * 2.1f) * 0.25f, -0.25f, std::sin(float(i) * 2.1f) * 0.25f), 0.17f, kWhite, 12, 8);
    shade_round(t, nuts, col(120, 85, 45), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel dead_tree() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect bark = span(0, 0, 4, 4);
    region(b, bark);
    b.sweep({{0, -0.2f, 0}, {0.05f, 1.2f, 0}, {0.0f, 2.4f, 0.1f}, {-0.1f, 3.4f, 0.2f}}, {0.3f, 0.2f, 0.14f, 0.05f}, kWhite, 10, true);
    b.sweep({{0.05f, 1.4f, 0}, {0.6f, 2.0f, 0.2f}, {1.2f, 2.9f, 0.1f}}, {0.1f, 0.07f, 0.02f}, kWhite, 8, true);
    b.sweep({{0, 2.2f, 0.1f}, {-0.6f, 2.8f, -0.2f}, {-1.0f, 3.5f, -0.4f}}, {0.09f, 0.06f, 0.02f}, kWhite, 8, true);
    b.sweep({{-0.05f, 2.9f, 0.15f}, {0.3f, 3.5f, 0.5f}, {0.5f, 4.1f, 0.7f}}, {0.07f, 0.05f, 0.015f}, kWhite, 8, true);
    paint_bark(t, bark, col(95, 80, 70));
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel bush(vec4 leaf) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect leaves = span(0, 0, 4, 4);
    region(b, leaves);
    lumpy_sphere(b, {0, 0.55f, 0}, vec3(0.8f, 0.6f, 0.75f), 0.1f, 7, kWhite, 20, 12);
    lumpy_sphere(b, {0.55f, 0.4f, 0.2f}, vec3(0.5f, 0.45f, 0.5f), 0.12f, 8, kWhite, 14, 10);
    lumpy_sphere(b, {-0.5f, 0.42f, -0.25f}, vec3(0.5f, 0.45f, 0.5f), 0.12f, 9, kWhite, 14, 10);
    paint_leaves(t, leaves, leaf);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel flower(vec4 petal) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect stem = cell(0, 0);
    region(b, stem);
    b.sweep({{0, 0, 0}, {0.02f, 0.3f, 0}, {0.0f, 0.55f, 0.03f}}, 0.025f, kWhite, 8);
    t.fill(stem, col(70, 150, 60));
    const UvRect petals = span(1, 0, 4, 3);
    region(b, petals);
    const vec3 c(0, 0.58f, 0.03f);
    for (int i = 0; i < 6; ++i) {
        const float a = 2.0f * kPi * float(i) / 6.0f;
        const vec3 d(std::cos(a), 0.1f, std::sin(a));
        b.sphere(c + d * 0.14f, vec3(0.12f, 0.03f, 0.08f), kWhite, 12, 6);
    }
    t.gradient_v(petals, shade(petal, 1.15f), shade(petal, 0.8f));
    t.glow(petals, {0.5f, 0.5f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.3f), 1.0f);
    const UvRect centre = cell(0, 1);
    region(b, centre);
    b.sphere(c + vec3(0, 0.03f, 0), vec3(0.08f, 0.05f, 0.08f), kWhite, 12, 8);
    shade_round(t, centre, col(250, 200, 50), 1.0f);
    const UvRect leaf = cell(0, 2);
    region(b, leaf);
    leaf_quad(b, {0.02f, 0.2f, 0}, {0.3f, 0.3f, 0.1f}, {0.3f, 0.3f, 0.1f}, {0.1f, 0.4f, 0.0f}, kWhite);
    t.fill(leaf, col(60, 140, 55));
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel rock(float size, vec4 base, uint32_t seed) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect stone = span(0, 0, 4, 4);
    region(b, stone);
    lumpy_sphere(b, {0, size * 0.45f, 0}, vec3(size, size * 0.6f, size * 0.85f), 0.18f, seed, kWhite, 22, 14);
    paint_stone(t, stone, base);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel lamp_post() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect metal = span(0, 0, 2, 4);
    region(b, metal);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.22f, 0.0f}, {0.2f, 0.08f}, {0.12f, 0.1f}, {0.09f, 5.0f}, {0.0f, 5.05f}}, 12, kWhite, 1.0f, 1.0f);
    b.sweep({{0, 4.95f, 0}, {0.4f, 5.4f, 0}, {1.0f, 5.5f, 0}}, 0.06f, kWhite, 8);
    b.lathe({1.0f, 5.5f, 0}, {0, -1, 0}, {{0.0f, 0.0f}, {0.2f, 0.0f}, {0.32f, 0.15f}, {0.26f, 0.3f}, {0.0f, 0.3f}}, 14, kWhite, 1.0f, 1.0f);
    t.gradient_u(metal, col(70, 75, 90), col(35, 38, 48));
    t.band_v(metal, 0.0f, 0.03f, col(90, 95, 110), 0.0f);
    const UvRect lamp = span(2, 0, 4, 2);
    region(b, lamp);
    b.sphere({1.0f, 5.32f, 0}, 0.2f, kWhite, 14, 10);
    t.gradient_v(lamp, col(255, 250, 215), col(255, 225, 150));
    t.glow(lamp, {0.5f, 0.4f}, 0.4f, vec4(1.0f, 1.0f, 1.0f, 0.6f), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel billboard(uint32_t seed) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect post = span(0, 3, 4, 4);
    region(b, post);
    for (float x : {-2.2f, 2.2f}) b.tube({x, -0.3f, 0}, {x, 3.2f, 0}, 0.12f, 0.1f, kWhite, 10, true);
    t.gradient_v(post, col(120, 125, 140), col(60, 62, 75));
    const UvRect board = span(0, 0, 4, 3);
    region(b, board);
    b.box_painted({0, 4.4f, 0}, {2.8f, 1.5f, 0.08f}, kWhite);
    // An ad for the game itself: bold stripes, a big star and a racing kart badge.
    const vec4 a = seed % 3 == 0 ? col(240, 70, 60) : seed % 3 == 1 ? col(60, 140, 240) : col(250, 180, 40);
    const vec4 c2 = seed % 3 == 0 ? col(250, 220, 90) : seed % 3 == 1 ? col(250, 250, 250) : col(40, 60, 120);
    t.fill(board, a);
    t.stripes_u(board, 12, vec4(1.0f, 1.0f, 1.0f, 0.12f), vec4(0.0f, 0.0f, 0.0f, 0.1f), 0.4f);
    t.rounded(board, {0.06f, 0.1f, 0.94f, 0.9f}, 0.06f, c2);
    t.rounded(board, {0.09f, 0.15f, 0.91f, 0.85f}, 0.05f, a);
    t.star(board, {0.25f, 0.5f}, 0.2f, c2);
    t.ellipse(board, {0.65f, 0.5f}, {0.22f, 0.28f}, c2, 0.03f);
    t.ellipse(board, {0.65f, 0.5f}, {0.16f, 0.2f}, a, 0.03f);
    t.vignette(board, 0.25f, 0.05f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel tire_stack() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect rubber = span(0, 0, 4, 4);
    region(b, rubber);
    for (int i = 0; i < 3; ++i) b.torus({0, 0.18f + 0.36f * float(i), 0}, {0, 1, 0}, 0.42f, 0.18f, kWhite, 24, 12);
    t.gradient_v(rubber, col(60, 60, 66), col(25, 25, 30));
    t.band_u(rubber, 0.0f, 1.0f, col(40, 40, 46, 0.3f), 0.0f);
    t.band_v(rubber, 0.15f, 0.4f, col(235, 235, 235), 0.02f);
    for (int i = 0; i < 24; ++i) t.band_u(rubber, float(i) / 24.0f, float(i) / 24.0f + 0.012f, col(15, 15, 18, 0.6f), 0.004f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel cone() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect body = span(0, 0, 4, 4);
    region(b, body);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.32f, 0.0f}, {0.32f, 0.04f}, {0.2f, 0.06f}, {0.05f, 0.72f}, {0.0f, 0.74f}}, 16, kWhite, 1.0f, 1.0f);
    t.gradient_u(body, col(255, 130, 40), col(200, 85, 20));
    t.band_v(body, 0.0f, 0.08f, col(40, 40, 45), 0.0f);
    t.band_v(body, 0.4f, 0.55f, col(250, 250, 250), 0.015f);
    t.band_v(body, 0.7f, 0.8f, col(250, 250, 250), 0.015f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel crate() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect face = span(0, 0, 4, 4);
    region(b, face);
    b.box_painted({0, 0.6f, 0}, vec3(0.6f), kWhite);
    const vec4 wood = col(190, 140, 85);
    t.fill(face, wood);
    for (int i = 0; i < 5; ++i) t.band_v(face, float(i) / 5.0f + 0.19f, float(i) / 5.0f + 0.21f, shade(wood, 0.55f), 0.004f);
    t.noise(face, 0.25f, 3.0f, 61, wood);
    for (int i = 0; i < 30; ++i) t.band_v(face, float(i) / 30.0f, float(i) / 30.0f + 0.006f, shade(wood, 0.85f), 0.003f);
    t.rounded(face, {0.0f, 0.0f, 1.0f, 0.1f}, 0.02f, shade(wood, 0.8f));
    t.rounded(face, {0.0f, 0.9f, 1.0f, 1.0f}, 0.02f, shade(wood, 0.8f));
    t.rounded(face, {0.0f, 0.0f, 0.1f, 1.0f}, 0.02f, shade(wood, 0.8f));
    t.rounded(face, {0.9f, 0.0f, 1.0f, 1.0f}, 0.02f, shade(wood, 0.8f));
    t.jagged(face, {0.08f, 0.08f}, {0.92f, 0.92f}, 0.04f, shade(wood, 0.78f), 1, 0.0f);
    t.jagged(face, {0.92f, 0.08f}, {0.08f, 0.92f}, 0.04f, shade(wood, 0.78f), 1, 0.0f);
    t.vignette(face, 0.3f, 0.06f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel barrel(vec4 paint) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect body = span(0, 0, 4, 4);
    region(b, body);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.4f, 0.0f}, {0.44f, 0.08f}, {0.46f, 0.5f}, {0.44f, 0.92f}, {0.4f, 1.0f}, {0.0f, 1.0f}}, 20, kWhite, 1.0f, 1.0f);
    t.gradient_u(body, shade(paint, 1.15f), shade(paint, 0.75f));
    for (float v : {0.12f, 0.5f, 0.86f}) t.band_v(body, v - 0.03f, v + 0.03f, col(150, 150, 160), 0.008f);
    t.band_v(body, 0.0f, 0.04f, col(110, 110, 120), 0.0f);
    t.band_v(body, 0.96f, 1.0f, col(110, 110, 120), 0.0f);
    t.noise(body, 0.15f, 5.0f, 71, paint);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel cactus() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect skin = span(0, 0, 4, 4);
    region(b, skin);
    b.capsule({0, 0.3f, 0}, {0, 3.2f, 0}, 0.45f, kWhite, 16, 6);
    b.sweep({{0.3f, 1.6f, 0}, {1.0f, 1.7f, 0.05f}, {1.15f, 2.6f, 0.1f}}, {0.3f, 0.3f, 0.26f}, kWhite, 12);
    b.sweep({{-0.3f, 2.1f, 0}, {-0.9f, 2.2f, -0.1f}, {-1.0f, 2.9f, -0.15f}}, {0.26f, 0.26f, 0.22f}, kWhite, 12);
    const vec4 green = col(70, 150, 80);
    t.gradient_u(skin, shade(green, 1.2f), shade(green, 0.75f));
    for (int i = 0; i < 12; ++i) t.band_u(skin, float(i) / 12.0f, float(i) / 12.0f + 0.02f, shade(green, 0.55f), 0.008f);
    t.spots(skin, 30, 0.012f, col(240, 240, 200), 77, 0.3f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel snowman() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect snow = span(0, 0, 2, 2);
    region(b, snow);
    b.sphere({0, 0.6f, 0}, 0.65f, kWhite, 24, 16);
    b.sphere({0, 1.5f, 0}, 0.48f, kWhite, 22, 14);
    shade_round(t, snow, col(245, 248, 255), 0.8f);
    t.spots(snow, 6, 0.03f, col(30, 30, 35), 5, 0.1f); // coal buttons scatter
    const UvRect head = span(2, 0, 4, 2);
    region(b, head);
    b.sphere({0, 2.2f, 0}, 0.36f, kWhite, 22, 14);
    shade_round(t, head, col(245, 248, 255), 0.8f);
    t.ellipse(head, {0.2f, 0.42f}, {0.03f, 0.05f}, col(30, 30, 35), 0.01f);
    t.ellipse(head, {0.3f, 0.42f}, {0.03f, 0.05f}, col(30, 30, 35), 0.01f);
    t.mouth(head, {0.25f, 0.62f}, 0.1f, 0.04f, 0.015f, col(30, 30, 35));
    const UvRect carrot = cell(0, 2);
    region(b, carrot);
    b.tube({0, 2.2f, 0.33f}, {0, 2.15f, 0.65f}, 0.05f, 0.005f, kWhite, 10, true);
    t.gradient_v(carrot, col(255, 150, 50), col(220, 100, 30));
    const UvRect hat = cell(1, 2);
    region(b, hat);
    b.lathe({0, 2.45f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.45f, 0.0f}, {0.45f, 0.05f}, {0.28f, 0.06f}, {0.3f, 0.5f}, {0.0f, 0.5f}}, 16, kWhite, 1.0f, 1.0f);
    t.fill(hat, col(30, 30, 36));
    t.band_v(hat, 0.2f, 0.32f, col(220, 50, 50), 0.0f);
    const UvRect scarf = cell(2, 2);
    region(b, scarf);
    b.torus({0, 1.95f, 0}, {0, 1, 0}, 0.36f, 0.09f, kWhite, 18, 8);
    b.sweep({{0.2f, 1.9f, 0.3f}, {0.3f, 1.5f, 0.45f}, {0.35f, 1.2f, 0.5f}}, {0.08f, 0.07f, 0.06f}, kWhite, 8);
    t.stripes_u(scarf, 8, col(220, 50, 50), col(250, 240, 240));
    const UvRect sticks = cell(3, 2);
    region(b, sticks);
    for (float x : {-1.0f, 1.0f}) b.sweep({{x * 0.4f, 1.6f, 0}, {x * 0.9f, 1.95f, 0.1f}, {x * 1.15f, 2.2f, 0.15f}}, {0.04f, 0.035f, 0.02f}, kWhite, 6);
    t.fill(sticks, col(90, 60, 35));
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel crystal() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect ice = span(0, 0, 4, 4);
    region(b, ice);
    struct Shard { vec3 base, dir; float r, h; };
    for (const Shard& s : {Shard{{0, 0, 0}, {0, 1, 0}, 0.45f, 2.6f}, Shard{{0.5f, 0, 0.2f}, {0.5f, 1, 0.1f}, 0.3f, 1.7f}, Shard{{-0.5f, 0, -0.1f}, {-0.4f, 1, -0.2f}, 0.28f, 1.5f},
                          Shard{{0.1f, 0, -0.55f}, {0.1f, 1, -0.5f}, 0.22f, 1.2f}})
        b.lathe(s.base, s.dir, {{0.0f, -0.2f}, {s.r, 0.0f}, {s.r * 0.85f, s.h * 0.75f}, {0.0f, s.h}}, 6, kWhite, 1.0f, 1.0f);
    t.gradient_v(ice, col(230, 245, 255, 0.85f), col(120, 190, 240, 0.85f));
    t.stripes_u(ice, 6, vec4(1.0f, 1.0f, 1.0f, 0.12f), vec4(0.0f, 0.2f, 0.5f, 0.12f), 0.0f);
    t.glow(ice, {0.3f, 0.2f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.5f), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel boat() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect hull = span(0, 0, 4, 2);
    region(b, hull);
    std::vector<std::vector<vec3>> rings;
    struct Sec { float z, w, bottom, top; };
    for (const Sec& s : {Sec{-3.0f, 0.3f, 0.3f, 1.3f}, Sec{-2.0f, 1.1f, 0.1f, 1.2f}, Sec{0.0f, 1.3f, 0.0f, 1.1f}, Sec{2.0f, 1.0f, 0.15f, 1.25f}, Sec{3.2f, 0.15f, 0.6f, 1.5f}}) {
        std::vector<vec3> ring;
        for (int i = 0; i < 16; ++i) {
            const float a = 2.0f * kPi * float(i) / 16.0f;
            const float cx = std::cos(a), sy = std::sin(a);
            const float p = 3.0f;
            const float x = std::copysign(std::pow(std::abs(cx), 2.0f / p), cx) * s.w;
            const float y = 0.5f * (s.top + s.bottom) + std::copysign(std::pow(std::abs(sy), 2.0f / p), sy) * 0.5f * (s.top - s.bottom);
            ring.push_back({x, y, s.z});
        }
        rings.push_back(ring);
    }
    b.loft(rings, kWhite, true);
    t.gradient_v(hull, col(240, 240, 235), col(200, 60, 50));
    t.band_v(hull, 0.45f, 0.5f, col(40, 60, 120), 0.01f);
    t.noise(hull, 0.1f, 4.0f, 81, col(255, 255, 255));
    const UvRect cabin = span(0, 2, 4, 4);
    region(b, cabin);
    b.box_painted({0, 1.75f, -0.5f}, {0.8f, 0.6f, 1.1f}, kWhite);
    b.tube({0, 2.3f, 0.6f}, {0, 5.5f, 0.6f}, 0.08f, 0.06f, kWhite, 8, true);
    t.fill(cabin, col(250, 250, 245));
    t.windows(cabin, 3, 2, col(250, 250, 245), col(130, 200, 240), col(90, 140, 190), 3, 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel building(uint32_t seed, vec4 wall, vec4 lit) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect face = span(0, 0, 4, 4);
    region(b, face);
    b.box_painted({0, 0.5f, 0}, vec3(0.5f), kWhite); // unit cube, scaled per instance
    t.fill(face, wall);
    t.windows(face, 6, 8, wall, lit, shade(wall, 0.45f), seed, 0.6f);
    t.band_v(face, 0.0f, 0.03f, shade(wall, 0.6f), 0.0f);
    t.band_v(face, 0.94f, 1.0f, shade(wall, 0.7f), 0.0f);
    t.vignette(face, 0.2f, 0.04f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel tower() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect body = span(0, 0, 4, 3);
    region(b, body);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {2.2f, 0.0f}, {2.0f, 0.3f}, {1.4f, 9.0f}, {1.6f, 9.2f}, {1.6f, 9.8f}, {0.0f, 9.8f}}, 20, kWhite, 1.0f, 1.0f);
    t.fill(body, col(245, 245, 240));
    for (int i = 0; i < 3; ++i) t.band_v(body, 0.15f + 0.28f * float(i), 0.28f + 0.28f * float(i), col(220, 50, 45), 0.01f);
    t.band_v(body, 0.0f, 0.04f, col(120, 120, 125), 0.0f);
    t.windows(body, 10, 1, col(245, 245, 240, 0.0f), col(80, 120, 160), col(80, 120, 160), 5, 1.0f);
    const UvRect top = span(0, 3, 4, 4);
    region(b, top);
    b.lathe({0, 9.8f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {1.3f, 0.0f}, {1.2f, 1.4f}, {1.5f, 1.5f}, {0.0f, 2.3f}}, 16, kWhite, 1.0f, 1.0f);
    t.gradient_v(top, col(255, 245, 160), col(255, 220, 90));
    t.band_v(top, 0.6f, 1.0f, col(60, 60, 70), 0.02f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel lava_rock() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect stone = span(0, 0, 4, 4);
    region(b, stone);
    lumpy_sphere(b, {0, 1.2f, 0}, vec3(2.4f, 1.6f, 2.2f), 0.22f, 91, kWhite, 24, 14);
    paint_stone(t, stone, col(55, 45, 48));
    const vec4 glow = col(255, 110, 20);
    t.jagged(stone, {0.05f, 0.5f}, {0.5f, 0.3f}, 0.012f, glow, 93, 0.08f);
    t.jagged(stone, {0.45f, 0.35f}, {0.95f, 0.6f}, 0.01f, glow, 95, 0.08f);
    t.jagged(stone, {0.3f, 0.1f}, {0.6f, 0.75f}, 0.008f, glow, 97, 0.1f);
    t.glow(stone, {0.5f, 0.45f}, 0.25f, vec4(1.0f, 0.4f, 0.1f, 0.25f), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel cloud() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect puff = span(0, 0, 4, 4);
    region(b, puff);
    lumpy_sphere(b, {0, 0, 0}, vec3(5.0f, 3.0f, 4.0f), 0.1f, 51, kWhite, 20, 12);
    lumpy_sphere(b, {4.0f, 0.6f, 0.5f}, vec3(3.6f, 2.6f, 3.0f), 0.1f, 52, kWhite, 16, 10);
    lumpy_sphere(b, {-4.2f, 0.3f, -0.8f}, vec3(3.4f, 2.4f, 3.0f), 0.1f, 53, kWhite, 16, 10);
    lumpy_sphere(b, {0.8f, 2.0f, -1.5f}, vec3(3.0f, 2.2f, 2.6f), 0.1f, 54, kWhite, 16, 10);
    t.gradient_v(puff, col(255, 255, 255), col(205, 215, 235));
    t.glow(puff, {0.3f, 0.25f}, 0.4f, vec4(1.0f, 1.0f, 1.0f, 0.4f), 1.0f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel mesa() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect stone = span(0, 0, 4, 4);
    region(b, stone);
    // Unit height: scaled per instance. A skirt, two ledges and a flat cap.
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {1.3f, 0.0f}, {1.05f, 0.12f}, {0.95f, 0.4f}, {0.8f, 0.44f}, {0.76f, 0.72f}, {0.62f, 0.76f}, {0.58f, 0.98f}, {0.5f, 1.0f}, {0.0f, 1.0f}}, 18, kWhite, 1.0f, 1.0f);
    const vec4 red = col(200, 120, 74);
    t.gradient_v(stone, shade(red, 0.75f), shade(red, 1.1f));
    for (int i = 0; i < 12; ++i) t.band_v(stone, float(i) / 12.0f + 0.02f, float(i) / 12.0f + 0.05f, shade(red, 0.78f), 0.01f);
    t.band_v(stone, 0.38f, 0.44f, shade(red, 1.25f), 0.01f);
    t.band_v(stone, 0.7f, 0.76f, shade(red, 1.25f), 0.01f);
    t.noise(stone, 0.3f, 5.0f, 33, red);
    t.band_v(stone, 0.94f, 1.0f, col(222, 165, 110), 0.02f);
    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace

std::map<std::string, PaintedModel> make_gobby_props() {
    std::map<std::string, PaintedModel> out;
    out["tree_round"] = tree_round(1, col(60, 150, 60), 5.0f);
    out["tree_round_2"] = tree_round(7, col(85, 160, 55), 4.2f);
    out["tree_autumn"] = tree_round(13, col(220, 130, 50), 4.6f);
    out["tree_pine"] = tree_pine(false);
    out["tree_pine_snow"] = tree_pine(true);
    out["tree_palm"] = tree_palm();
    out["dead_tree"] = dead_tree();
    out["bush"] = bush(col(70, 150, 60));
    out["flower_pink"] = flower(col(240, 110, 170));
    out["flower_yellow"] = flower(col(250, 210, 70));
    out["rock"] = rock(1.0f, col(140, 140, 135), 3);
    out["boulder"] = rock(2.6f, col(125, 120, 112), 5);
    out["rock_red"] = rock(1.4f, col(175, 115, 80), 9);
    out["lamp_post"] = lamp_post();
    out["billboard"] = billboard(0);
    out["billboard_2"] = billboard(1);
    out["billboard_3"] = billboard(2);
    out["tire_stack"] = tire_stack();
    out["cone"] = cone();
    out["crate"] = crate();
    out["barrel"] = barrel(col(60, 110, 200));
    out["barrel_red"] = barrel(col(210, 60, 50));
    out["cactus"] = cactus();
    out["snowman"] = snowman();
    out["crystal"] = crystal();
    out["boat"] = boat();
    out["building"] = building(1, col(40, 42, 62), col(255, 225, 130));
    out["building_2"] = building(2, col(60, 48, 70), col(120, 220, 255));
    out["building_3"] = building(3, col(34, 50, 70), col(255, 150, 220));
    out["tower"] = tower();
    out["lava_rock"] = lava_rock();
    out["cloud"] = cloud();
    out["mesa"] = mesa();
    return out;
}

} // namespace gk::render
