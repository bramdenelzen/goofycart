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
    t.emission_fill(lamp, 0.9f);
    m.glow = true;
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

// A corner sign: yellow board with a black chevron on two posts. Front +Z.
PaintedModel turn_sign(bool left) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect post = span(0, 3, 4, 4);
    region(b, post);
    for (float x : {-0.55f, 0.55f}) b.tube({x, 0.0f, 0}, {x, 1.2f, 0}, 0.06f, 0.05f, kWhite, 8, true);
    t.gradient_v(post, col(200, 205, 215), col(110, 115, 128));
    const UvRect board = span(0, 0, 4, 3);
    region(b, board);
    b.box_painted({0, 1.55f, 0}, {0.85f, 0.5f, 0.05f}, kWhite);
    t.fill(board, col(255, 205, 30));
    t.rounded(board, {0.04f, 0.06f, 0.96f, 0.94f}, 0.05f, col(250, 196, 20));
    // Chevron pointing +u (the way to turn): two thick strokes.
    t.custom(board, [left](float u, float v) {
        const float x = (left ? 1.0f - u : u) - 0.35f, y = std::abs(v - 0.5f);
        const float d = std::abs(x - (0.32f - y * 0.9f));
        return d < 0.09f && y < 0.36f ? vec4(0.06f, 0.06f, 0.08f, 1.0f) : vec4(0.0f);
    });
    t.vignette(board, 0.2f, 0.06f);
    m.mesh = b.take();
    m.finish();
    return m;
}

// A feather flag on a bent pole, like a sponsor banner at a race.
PaintedModel feather_flag(vec4 a, vec4 c2, uint32_t seed) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect pole = span(0, 3, 1, 4);
    region(b, pole);
    std::vector<vec3> path;
    for (int i = 0; i <= 8; ++i) {
        const float s = float(i) / 8.0f;
        path.push_back({0.25f * s * s, 3.6f * s, 0.0f});
    }
    b.sweep(path, 0.045f, kWhite, 6, true);
    t.gradient_v(pole, col(230, 232, 238), col(120, 124, 135));
    // The cloth: a curved sail from the pole, two-sided, taller than wide.
    const UvRect cloth = span(1, 0, 4, 4);
    region(b, cloth);
    const int rows = 6;
    for (int i = 0; i < rows; ++i) {
        const float s0 = float(i) / float(rows), s1 = float(i + 1) / float(rows);
        auto edge = [&](float s) { return vec3(0.04f + 0.25f * (0.9f * s + 0.1f) * (0.9f * s + 0.1f), 0.9f + 2.6f * s, 0.0f); };
        auto out = [&](float s) { return vec3(0.75f - 0.18f * (1.0f - s) * (1.0f - s) + 0.1f * s, 0.75f + 2.7f * s, 0.12f * std::sin(s * 3.0f)); };
        const vec3 a0 = edge(s0), a1 = edge(s1), b0 = out(s0), b1 = out(s1);
        b.quad_uv(a0, b0, b1, a1, {0, 1 - s0}, {1, 1 - s0}, {1, 1 - s1}, {0, 1 - s1}, kWhite);
        b.quad_uv(a1, b1, b0, a0, {0, 1 - s1}, {1, 1 - s1}, {1, 1 - s0}, {0, 1 - s0}, kWhite);
    }
    t.fill(cloth, a);
    t.band_u(cloth, 0.0f, 0.18f, c2, 0.01f);
    t.star(cloth, {0.58f, 0.3f}, 0.14f, c2);
    for (int i = 0; i < 3; ++i) t.band_v(cloth, 0.5f + 0.1f * float(i), 0.54f + 0.1f * float(i), vec4(c2.r, c2.g, c2.b, 0.85f), 0.01f);
    t.noise(cloth, 0.06f, 6.0f, seed, vec4(1.0f));
    m.mesh = b.take();
    m.two_sided = true;
    m.finish();
    return m;
}

// A small grandstand of cheering fans: tiers, a roof, coloured blobs. Front +Z.
PaintedModel grandstand(uint32_t seed) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect steel = span(0, 3, 4, 4);
    const UvRect seats = span(0, 0, 4, 2);
    const UvRect roof = span(0, 2, 4, 3);
    region(b, seats);
    for (int i = 0; i < 4; ++i) b.box_painted({0, 0.35f + 0.55f * float(i), -0.6f * float(i)}, {3.2f, 0.18f, 0.32f}, kWhite);
    region(b, steel);
    for (float x : {-3.1f, 3.1f}) {
        b.box_painted({x, 1.6f, -1.0f}, {0.08f, 1.6f, 0.08f}, kWhite);
        b.box_painted({x, 2.2f, -2.2f}, {0.08f, 2.2f, 0.08f}, kWhite);
    }
    region(b, roof);
    b.box_painted({0, 4.45f, -1.3f}, {3.4f, 0.08f, 1.4f}, kWhite);
    // The crowd: little heads and bodies along each tier.
    struct {
        uint32_t s;
        float unit() {
            s = s * 1664525u + 1013904223u;
            return float(s >> 8) / float(1u << 24);
        }
    } r{seed * 2654435761u + 7u};
    const vec4 shirts[6] = {col(230, 60, 50), col(60, 140, 240), col(250, 200, 40), col(70, 190, 90), col(240, 120, 200), col(250, 250, 250)};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 9; ++k) {
            if (r.unit() < 0.2f) continue;
            const float x = -2.9f + 0.72f * float(k) + 0.15f * (r.unit() - 0.5f), y = 0.55f + 0.55f * float(i), z = -0.6f * float(i) - 0.05f;
            region(b, span(int(r.unit() * 3.99f), 0, int(r.unit() * 3.99f) + 1, 1, 8));
            b.box_painted({x, y + 0.2f, z}, {0.16f, 0.2f, 0.1f}, kWhite);
            region(b, span(4, 1, 5, 2, 8));
            b.box_painted({x, y + 0.5f, z}, {0.1f, 0.1f, 0.1f}, kWhite);
        }
    t.gradient_v(seats, col(70, 90, 160), col(40, 50, 100));
    for (int i = 0; i < 8; ++i) t.band_u(seats, float(i) / 8.0f, float(i) / 8.0f + 0.01f, col(20, 25, 50, 0.5f), 0.0f);
    t.gradient_v(steel, col(200, 205, 215), col(120, 125, 140));
    t.stripes_u(roof, 10, col(230, 50, 45), col(250, 250, 250));
    for (int c = 0; c < 4; ++c) t.fill(span(c, 0, c + 1, 1, 8), shirts[(c + seed) % 6]);
    t.fill(span(4, 1, 5, 2, 8), col(240, 200, 160));
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

// One loose tire from a toppled stack, lying flat.
PaintedModel loose_tire() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect rubber = span(0, 0, 4, 4);
    region(b, rubber);
    b.torus({0, 0.18f, 0}, {0, 1, 0}, 0.42f, 0.18f, kWhite, 24, 12);
    t.gradient_v(rubber, col(60, 60, 66), col(25, 25, 30));
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
    t.emission_from_brightness(face, 0.5f, 0.7f); // lit windows glow
    m.glow = true;
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


// ---- market town

// A narrow town house, 4 m wide, 3 m deep, `floors` storeys of 3 m, facing
// +Z: plaster walls, shuttered windows, a door, a striped awning over the
// shop front and a tiled roof. The back and sides are plain.
PaintedModel town_house(vec4 wall, vec4 shutter, vec4 awning_a, int floors, uint32_t seed) {
    PaintedModel m;
    m.texture = Canvas(512, col(128, 128, 128));
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r_front = span(0, 0, 2, 4), r_side = span(2, 0, 3, 2), r_roof = span(3, 0, 4, 2), r_awning = span(2, 2, 4, 3), r_trim = span(2, 3, 4, 4);
    const float w = 2.0f, d = 1.5f, h = 3.0f * float(floors);
    // Front face (+Z), with its own UVs; sides and back.
    region(b, r_front);
    b.quad_uv({-w, 0, d}, {w, 0, d}, {w, h, d}, {-w, h, d}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    region(b, r_side);
    b.quad_uv({w, 0, d}, {w, 0, -d}, {w, h, -d}, {w, h, d}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    b.quad_uv({-w, 0, -d}, {-w, 0, d}, {-w, h, d}, {-w, h, -d}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    b.quad_uv({w, 0, -d}, {-w, 0, -d}, {-w, h, -d}, {w, h, -d}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    // Pitched roof, ridge along X, overhanging.
    region(b, r_roof);
    const float o = 0.25f, ridge = 1.4f;
    b.quad_uv({-w - o, h - 0.1f, d + o}, {w + o, h - 0.1f, d + o}, {w + o, h + ridge, 0}, {-w - o, h + ridge, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    b.quad_uv({w + o, h - 0.1f, -d - o}, {-w - o, h - 0.1f, -d - o}, {-w - o, h + ridge, 0}, {w + o, h + ridge, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    region(b, r_side);
    b.tri({w, h, d}, {w, h, -d}, {w, h + ridge - 0.1f, 0}, kWhite);
    b.tri({-w, h, -d}, {-w, h, d}, {-w, h + ridge - 0.1f, 0}, kWhite);
    // Awning over the shop front: a sloped striped sheet with a scalloped edge.
    region(b, r_awning);
    b.quad_uv({-w + 0.2f, 2.0f, d + 1.1f}, {w - 0.2f, 2.0f, d + 1.1f}, {w - 0.2f, 2.7f, d + 0.02f}, {-w + 0.2f, 2.7f, d + 0.02f}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    b.quad_uv({-w + 0.2f, 2.7f, d + 0.02f}, {w - 0.2f, 2.7f, d + 0.02f}, {w - 0.2f, 2.0f, d + 1.1f}, {-w + 0.2f, 2.0f, d + 1.1f}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, kWhite);
    // A chimney.
    region(b, r_trim);
    b.box_painted({w * 0.5f, h + ridge * 0.6f, -0.4f}, {0.25f, 0.6f, 0.25f}, kWhite);

    // Paint: plaster with a stone base and a cornice.
    t.fill(r_front, wall);
    t.noise(r_front, 0.15f, 7.0f, seed, wall);
    const float base_v = 1.0f - 0.9f / h; // the bottom 0.9 m
    t.band_v(r_front, base_v, 1.0f, col(150, 140, 125), 0.0f);
    t.band_v(r_front, 0.0f, 0.04f, shade(wall, 0.7f), 0.0f);
    // Ground floor: a door and a shop window.
    const float gf = 1.0f - 3.0f / h;
    t.rounded(r_front, {0.12f, gf + 0.35f / h * 3.0f * 0.33f, 0.32f, 1.0f}, 0.03f, col(110, 70, 40));
    t.rounded(r_front, {0.45f, gf + 0.12f, 0.88f, base_v - 0.02f}, 0.02f, col(70, 110, 140));
    t.rounded(r_front, {0.47f, gf + 0.14f, 0.86f, base_v - 0.04f}, 0.02f, col(150, 200, 230));
    t.band_u({r_front.x0, r_front.y0 + (gf + 0.12f) * r_front.h(), r_front.x1, r_front.y0 + (base_v - 0.02f) * r_front.h()}, 0.66f, 0.67f, col(70, 50, 40), 0.0f);
    // Upper floors: two shuttered windows each, a flower box under some.
    uint32_t rs = seed;
    auto rnd = [&rs]() { rs = rs * 1664525u + 1013904223u; return float(rs >> 8) / float(1u << 24); };
    for (int f = 1; f < floors; ++f) {
        const float v0 = 1.0f - (3.0f * float(f + 1) - 0.7f) / h, v1 = 1.0f - (3.0f * float(f) + 0.8f) / h;
        for (float u : {0.28f, 0.72f}) {
            t.rounded(r_front, {u - 0.11f, v0, u + 0.11f, v1}, 0.02f, col(60, 80, 100));
            t.rounded(r_front, {u - 0.1f, v0 + 0.01f, u + 0.1f, v1 - 0.01f}, 0.02f, rnd() < 0.5f ? col(255, 220, 140) : col(130, 175, 210));
            t.band_u({r_front.x0, r_front.y0 + v0 * r_front.h(), r_front.x1, r_front.y0 + v1 * r_front.h()}, u - 0.005f, u + 0.005f, col(60, 50, 40), 0.0f);
            t.rounded(r_front, {u - 0.2f, v0, u - 0.115f, v1}, 0.01f, shutter);
            t.rounded(r_front, {u + 0.115f, v0, u + 0.2f, v1}, 0.01f, shutter);
            if (rnd() < 0.6f) {
                t.rounded(r_front, {u - 0.13f, v1, u + 0.13f, v1 + 0.025f}, 0.005f, col(120, 80, 50));
                t.spots({r_front.x0 + (u - 0.13f) * r_front.w(), r_front.y0 + (v1 - 0.025f) * r_front.h(), r_front.x0 + (u + 0.13f) * r_front.w(), r_front.y0 + v1 * r_front.h()}, 6, 0.18f,
                        rnd() < 0.5f ? col(230, 70, 90) : col(250, 200, 60), seed + uint32_t(f), 0.3f);
            }
        }
        t.band_v(r_front, 1.0f - 3.0f * float(f) / h - 0.01f, 1.0f - 3.0f * float(f) / h + 0.01f, shade(wall, 0.85f), 0.005f);
    }
    t.vignette(r_front, 0.15f, 0.05f);
    t.fill(r_side, shade(wall, 0.9f));
    t.noise(r_side, 0.15f, 6.0f, seed + 3, wall);
    // Roof tiles: rows of rounded terracotta.
    t.custom(r_roof, [](float u, float v) {
        const float row = std::fmod(v * 14.0f, 1.0f), col_ = std::fmod(u * 18.0f + (int(v * 14.0f) % 2) * 0.5f, 1.0f);
        const float shape = 0.75f + 0.25f * std::sin(col_ * 3.14159f) - 0.25f * (row > 0.85f ? 1.0f : 0.0f);
        return vec4(vec3(0.72f, 0.33f, 0.2f) * shape, 1.0f);
    });
    t.custom(r_awning, [awning_a](float u, float v) {
        const bool stripe = int(u * 10.0f) % 2 == 0;
        const vec3 c = stripe ? vec3(awning_a) : vec3(0.97f, 0.95f, 0.9f);
        const float scallop = std::fmod(u * 10.0f, 1.0f);
        const float edge = 0.88f + 0.08f * std::sin(scallop * 3.14159f);
        return vec4(c * (0.8f + 0.2f * (1.0f - v)), v < edge ? 1.0f : 0.0f);
    });
    t.gradient_v(r_trim, col(170, 160, 150), col(110, 100, 95));
    m.mesh = b.take();
    m.two_sided = false; // both faces are built: culling keeps them apart // the awning
    m.finish();
    return m;
}

// A market stall, 3 m wide: a wooden counter with crates of fruit and a
// striped canopy on four poles. Faces +Z.
PaintedModel market_stall(vec4 canopy, uint32_t seed) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r_wood = span(0, 0, 2, 2), r_canopy = span(2, 0, 4, 2), r_fruit = span(0, 2, 2, 4), r_pole = span(2, 2, 3, 4), r_crate = span(3, 2, 4, 4);
    region(b, r_wood);
    b.box_painted({0, 0.45f, 0}, {1.4f, 0.45f, 0.5f}, kWhite);
    region(b, r_pole);
    for (float x : {-1.35f, 1.35f})
        for (float z : {-0.45f, 0.45f}) b.tube({x, 0, z}, {x, 2.3f + (z < 0 ? 0.3f : 0.0f), z}, 0.05f, 0.05f, kWhite, 6, true);
    region(b, r_canopy);
    b.quad_uv({-1.6f, 2.2f, 0.9f}, {1.6f, 2.2f, 0.9f}, {1.6f, 2.65f, -0.7f}, {-1.6f, 2.65f, -0.7f}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
    b.quad_uv({-1.6f, 2.65f, -0.7f}, {1.6f, 2.65f, -0.7f}, {1.6f, 2.2f, 0.9f}, {-1.6f, 2.2f, 0.9f}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, kWhite);
    // Three crates on the counter, heaped with fruit.
    uint32_t rs = seed;
    auto rnd = [&rs]() { rs = rs * 1664525u + 1013904223u; return float(rs >> 8) / float(1u << 24); };
    for (int c = 0; c < 3; ++c) {
        const float x = -0.9f + 0.9f * float(c);
        region(b, r_crate);
        b.box_painted({x, 1.0f, 0.05f}, {0.4f, 0.1f, 0.35f}, kWhite);
        region(b, r_fruit);
        const int kind = int(rnd() * 4.0f) % 4; // cell of the fruit texture
        const UvRect f = cell(kind % 2, kind / 2, 2);
        b.uv_region(r_fruit.x0 + f.x0 * r_fruit.w(), r_fruit.y0 + f.y0 * r_fruit.h(), r_fruit.x0 + f.x1 * r_fruit.w(), r_fruit.y0 + f.y1 * r_fruit.h());
        for (int k = 0; k < 6; ++k)
            b.sphere({x - 0.25f + 0.25f * float(k % 3), 1.17f + 0.04f * float(k / 3), -0.12f + 0.24f * float(k / 3)}, 0.12f, kWhite, 8, 5);
    }
    // Paint.
    const vec4 wood = col(160, 110, 65);
    t.fill(r_wood, wood);
    for (int i = 0; i < 6; ++i) t.band_v(r_wood, float(i) / 6.0f, float(i) / 6.0f + 0.02f, shade(wood, 0.6f), 0.005f);
    t.noise(r_wood, 0.2f, 5.0f, seed, wood);
    t.vignette(r_wood, 0.25f, 0.08f);
    t.custom(r_canopy, [canopy](float u, float v) {
        const bool stripe = int(u * 8.0f) % 2 == 0;
        const float scallop = std::fmod(u * 8.0f, 1.0f);
        const float edge = 0.86f + 0.1f * std::sin(scallop * 3.14159f);
        return vec4((stripe ? vec3(canopy) : vec3(0.98f, 0.96f, 0.92f)) * (0.85f + 0.15f * (1.0f - v)), v < edge ? 1.0f : 0.0f);
    });
    t.gradient_u(r_pole, col(140, 100, 60), col(90, 60, 35));
    const vec4 fruit[4] = {col(220, 40, 40), col(250, 150, 30), col(250, 220, 60), col(110, 190, 60)};
    for (int k = 0; k < 4; ++k) {
        const UvRect f = cell(k % 2, k / 2, 2);
        const UvRect r{r_fruit.x0 + f.x0 * r_fruit.w(), r_fruit.y0 + f.y0 * r_fruit.h(), r_fruit.x0 + f.x1 * r_fruit.w(), r_fruit.y0 + f.y1 * r_fruit.h()};
        shade_round(t, r, fruit[k], 1.2f);
    }
    t.fill(r_crate, col(190, 150, 95));
    for (int i = 0; i < 3; ++i) t.band_v(r_crate, float(i) / 3.0f, float(i) / 3.0f + 0.04f, col(120, 85, 50), 0.01f);
    m.mesh = b.take();
    m.two_sided = false; // both faces are built: culling keeps them apart
    m.finish();
    return m;
}

PaintedModel fountain() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r_stone = span(0, 0, 4, 2), r_water = span(0, 2, 2, 4), r_gold = span(2, 2, 4, 4);
    region(b, r_stone);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 0.6f}, {1.8f, 0.7f}, {1.75f, 0.35f}, {0.5f, 0.35f}, {0.4f, 1.6f}, {0.9f, 1.7f}, {0.9f, 1.85f}, {0.25f, 1.9f}, {0.2f, 2.6f}, {0.0f, 2.7f}}, 20, kWhite, 1.0f, 1.0f);
    region(b, r_water);
    b.lathe({0, 0.5f, 0}, {0, 1, 0}, {{1.78f, 0.0f}, {0.0f, 0.02f}}, 20, kWhite, 1.0f, 1.0f);
    b.lathe({0, 1.78f, 0}, {0, 1, 0}, {{0.85f, 0.0f}, {0.0f, 0.02f}}, 16, kWhite, 1.0f, 1.0f);
    region(b, r_gold);
    b.sphere({0, 2.85f, 0}, 0.22f, kWhite, 10, 6);
    const vec4 stone = col(210, 200, 185);
    t.gradient_v(r_stone, shade(stone, 1.1f), shade(stone, 0.75f));
    for (int i = 0; i < 16; ++i) t.band_u(r_stone, float(i) / 16.0f, float(i) / 16.0f + 0.006f, shade(stone, 0.65f), 0.003f);
    t.noise(r_stone, 0.2f, 8.0f, 23, stone);
    t.gradient_v(r_water, col(120, 200, 240), col(60, 140, 200));
    t.spots(r_water, 12, 0.04f, col(230, 250, 255, 0.7f), 29, 0.6f);
    shade_round(t, r_gold, col(250, 200, 60), 1.3f);
    m.mesh = b.take();
    m.finish();
    return m;
}

// Lanterns and bunting are drawn in the scenery; this is one paper lantern.
PaintedModel lantern() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r_paper = span(0, 0, 4, 3), r_cap = span(0, 3, 4, 4);
    region(b, r_paper);
    b.lathe({0, -0.35f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.15f, 0.02f}, {0.24f, 0.17f}, {0.24f, 0.33f}, {0.15f, 0.48f}, {0.0f, 0.5f}}, 10, kWhite, 1.0f, 1.0f);
    region(b, r_cap);
    b.tube({0, 0.14f, 0}, {0, 0.22f, 0}, 0.08f, 0.08f, kWhite, 6, true);
    t.gradient_v(r_paper, col(255, 200, 90), col(240, 90, 50));
    for (int i = 1; i < 5; ++i) t.band_v(r_paper, float(i) / 5.0f - 0.01f, float(i) / 5.0f + 0.01f, col(150, 50, 30, 0.6f), 0.005f);
    t.glow(r_paper, {0.5f, 0.5f}, 0.5f, vec4(1.0f, 0.95f, 0.7f, 0.35f), 1.0f);
    t.fill(r_cap, col(40, 35, 35));
    t.emission_fill(r_paper, 0.6f);
    m.glow = true;
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel hay_bale() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r_side = span(0, 0, 4, 3), r_end = span(0, 3, 4, 4);
    region(b, r_side);
    b.lathe({-0.7f, 0.6f, 0}, {1, 0, 0}, {{0.0f, 0.0f}, {0.55f, 0.02f}, {0.6f, 0.1f}, {0.6f, 1.3f}, {0.55f, 1.38f}, {0.0f, 1.4f}}, 12, kWhite, 1.0f, 1.0f);
    t.fill(r_side, col(225, 190, 95));
    t.custom(r_side, [](float u, float v) { return vec4(0.55f, 0.4f, 0.15f, (std::sin(u * 220.0f + v * 13.0f) > 0.7f) ? 0.35f : 0.0f); });
    t.band_v(r_side, 0.3f, 0.33f, col(150, 100, 40), 0.0f);
    t.band_v(r_side, 0.67f, 0.7f, col(150, 100, 40), 0.0f);
    (void)r_end;
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel fence() {
    // 4 m of white picket fence along X.
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r = span(0, 0, 4, 4);
    region(b, r);
    for (int i = 0; i <= 8; ++i) b.box_painted({-2.0f + 0.5f * float(i), 0.5f, 0}, {0.06f, 0.5f, 0.04f}, kWhite);
    b.box_painted({0, 0.75f, 0.05f}, {2.05f, 0.05f, 0.02f}, kWhite);
    b.box_painted({0, 0.3f, 0.05f}, {2.05f, 0.05f, 0.02f}, kWhite);
    t.gradient_v(r, col(250, 250, 245), col(200, 200, 195));
    t.vignette(r, 0.2f, 0.1f);
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel windmill() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r_tower = span(0, 0, 2, 4), r_roof = span(2, 0, 4, 1), r_sail = span(2, 1, 4, 4);
    region(b, r_tower);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {2.6f, 0.0f}, {1.8f, 9.0f}, {0.0f, 9.0f}}, 10, kWhite, 1.0f, 1.0f);
    region(b, r_roof);
    b.lathe({0, 9.0f, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {2.1f, 0.0f}, {0.0f, 2.2f}}, 10, kWhite, 1.0f, 1.0f);
    region(b, r_sail);
    const vec3 hub(0, 8.6f, 2.0f);
    for (int k = 0; k < 4; ++k) {
        const float a = kPi * 0.5f * float(k) + 0.4f;
        const vec3 d(std::cos(a), std::sin(a), 0), s(-std::sin(a), std::cos(a), 0);
        const vec3 p0 = hub + d * 0.6f, p1 = hub + d * 6.0f;
        b.quad_uv(p0, p1, p1 + s * 1.2f, p0 + s * 1.2f, {0, 1}, {1, 1}, {1, 0}, {0, 0}, kWhite);
        b.quad_uv(p0 + s * 1.2f, p1 + s * 1.2f, p1, p0, {0, 0}, {1, 0}, {1, 1}, {0, 1}, kWhite);
    }
    t.fill(r_tower, col(240, 232, 215));
    t.noise(r_tower, 0.12f, 6.0f, 31, col(230, 220, 200));
    t.rounded(r_tower, {0.2f, 0.75f, 0.32f, 1.0f}, 0.02f, col(110, 70, 40));
    for (float v : {0.25f, 0.5f}) t.rounded(r_tower, {0.6f, v, 0.7f, v + 0.1f}, 0.02f, col(70, 100, 130));
    t.custom(r_roof, [](float u, float v) { return vec4(vec3(0.62f, 0.25f, 0.18f) * (0.8f + 0.2f * (int(u * 20.0f) % 2)), 1.0f); });
    t.fill(r_sail, col(245, 240, 225));
    t.custom(r_sail, [](float u, float v) { const float g = std::min(std::fmod(u * 6.0f, 1.0f), std::fmod(v * 3.0f, 1.0f)); return vec4(0.45f, 0.3f, 0.2f, g < 0.08f ? 1.0f : 0.0f); });
    m.mesh = b.take();
    m.two_sided = false; // both faces are built: culling keeps them apart
    m.finish();
    return m;
}

PaintedModel igloo() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r = span(0, 0, 4, 4);
    region(b, r);
    b.sphere({0, 0, 0}, vec3(2.2f, 1.9f, 2.2f), kWhite, 14, 8);
    b.capsule({0, 0.3f, 2.0f}, {0, 0.3f, 2.8f}, 0.75f, kWhite, 10, 3);
    t.fill(r, col(240, 246, 255));
    for (int i = 1; i < 7; ++i) t.band_v(r, float(i) / 7.0f, float(i) / 7.0f + 0.01f, col(170, 195, 225), 0.004f);
    t.custom(r, [](float u, float v) { const int row = int(v * 7.0f); const float x = std::fmod(u * 16.0f + 0.5f * (row % 2), 1.0f); return vec4(0.66f, 0.76f, 0.88f, x < 0.05f ? 1.0f : 0.0f); });
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel neon_sign(vec4 glow_col, uint32_t seed) {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r = span(0, 0, 4, 3), r_post = span(0, 3, 4, 4);
    region(b, r);
    b.box_painted({0, 3.5f, 0}, {2.0f, 1.0f, 0.1f}, kWhite);
    region(b, r_post);
    b.tube({0, 0, 0}, {0, 2.5f, 0}, 0.12f, 0.12f, kWhite, 6, true);
    t.fill(r, col(20, 18, 30));
    t.rounded(r, {0.05f, 0.1f, 0.95f, 0.9f}, 0.08f, glow_col);
    t.rounded(r, {0.08f, 0.16f, 0.92f, 0.84f}, 0.06f, col(20, 18, 30));
    if (seed % 2) t.star(r, {0.5f, 0.5f}, 0.3f, glow_col);
    else for (int k = 0; k < 3; ++k) t.ellipse(r, {0.28f + 0.22f * float(k), 0.5f}, {0.07f, 0.22f}, glow_col, 0.03f);
    t.gradient_u(r_post, col(80, 80, 95), col(30, 30, 40));
    t.emission_from_brightness(r, 0.35f, 0.55f);
    m.glow = true;
    m.mesh = b.take();
    m.finish();
    return m;
}

// A star: a tiny self-lit diamond. The sky is made of scaled copies.
PaintedModel night_star() {
    PaintedModel m;
    m.texture = Canvas(16, col(255, 255, 255));
    MeshBuilder b;
    b.uv_region(0.0f, 0.0f, 1.0f, 1.0f);
    b.sphere({0, 0, 0}, 1.0f, kWhite, 4, 2);
    m.texture.fill({0, 0, 1, 1}, col(255, 250, 225));
    m.texture.emission_fill({0, 0, 1, 1}, 1.0f);
    m.glow = true;
    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel moon() {
    PaintedModel m;
    m.texture = Canvas(256, col(255, 255, 255));
    MeshBuilder b;
    Canvas& t = m.texture;
    const UvRect r{0, 0, 1, 1};
    region(b, r);
    b.sphere({0, 0, 0}, 1.0f, kWhite, 16, 10);
    t.fill(r, col(240, 238, 222));
    t.spots(r, 9, 0.07f, col(200, 198, 185), 77, 0.5f);
    t.spots(r, 6, 0.04f, col(215, 212, 198), 79, 0.5f);
    t.emission_fill(r, 0.95f);
    m.glow = true;
    m.mesh = b.take();
    m.finish();
    return m;
}

// A soft pool of lamplight on the ground, 8 m across: drawn see-through.
PaintedModel light_pool(vec4 c) {
    PaintedModel m;
    m.texture = Canvas(64, vec4(c.r, c.g, c.b, 0.0f));
    MeshBuilder b;
    b.quad_uv({-4, 0.04f, -4}, {-4, 0.04f, 4}, {4, 0.04f, 4}, {4, 0.04f, -4}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, kWhite);
    m.texture.custom({0, 0, 1, 1}, [c](float u, float v) {
        const float d = std::hypot(u - 0.5f, v - 0.5f) * 2.0f;
        const float a = std::max(0.0f, 1.0f - d);
        return vec4(c.r, c.g, c.b, 0.45f * a * a);
    });
    m.unlit = true;
    m.blended = true;
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
    out["tire"] = loose_tire();
    out["turn_sign"] = turn_sign(false);
    out["turn_sign_left"] = turn_sign(true);
    out["flag_red"] = feather_flag(col(225, 40, 40), col(255, 255, 255), 1);
    out["flag_blue"] = feather_flag(col(40, 110, 220), col(255, 210, 40), 2);
    out["flag_green"] = feather_flag(col(40, 170, 90), col(255, 255, 255), 3);
    out["grandstand"] = grandstand(4);
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
    out["house_1"] = town_house(col(240, 210, 150), col(70, 120, 90), col(200, 50, 50), 2, 11);
    out["house_2"] = town_house(col(230, 160, 140), col(60, 90, 140), col(40, 110, 170), 3, 12);
    out["house_3"] = town_house(col(200, 220, 190), col(150, 80, 50), col(240, 170, 40), 2, 13);
    out["house_4"] = town_house(col(250, 235, 200), col(120, 60, 90), col(60, 150, 80), 3, 14);
    out["house_5"] = town_house(col(180, 205, 230), col(160, 60, 50), col(230, 100, 150), 2, 15);
    out["stall_red"] = market_stall(col(210, 50, 50), 21);
    out["stall_blue"] = market_stall(col(50, 110, 190), 22);
    out["stall_green"] = market_stall(col(60, 150, 70), 23);
    out["fountain"] = fountain();
    out["lantern"] = lantern();
    out["hay_bale"] = hay_bale();
    out["fence"] = fence();
    out["windmill"] = windmill();
    out["igloo"] = igloo();
    out["neon_pink"] = neon_sign(col(255, 80, 200), 1);
    out["night_star"] = night_star();
    out["moon"] = moon();
    out["pool_warm"] = light_pool(col(255, 200, 110));
    out["pool_pink"] = light_pool(col(255, 90, 210));
    out["pool_cyan"] = light_pool(col(70, 220, 255));
    out["neon_cyan"] = neon_sign(col(60, 230, 255), 2);
    return out;
}

} // namespace gk::render
