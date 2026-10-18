// Items after Albert's reference paintings: the glass item box in a riveted
// metal frame, the banana peel, the TNT bundle, and the turtle shells
// (green, red, and the winged, spiked blue one).
#include "gobby_assets.h"
#include "gobby_util.h"

namespace gk::render {
namespace {

using namespace gobby;

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xffff) / 65535.0f;
}

// Brushed, slightly blue-grey metal.
void paint_metal(Canvas& t, const UvRect& r) {
    t.gradient_v(r, col(196, 208, 226), col(104, 118, 145));
    t.band_v(r, 0.18f, 0.32f, vec4(1.0f, 1.0f, 1.0f, 0.35f), 0.08f);
    t.noise(r, 0.18f, 10.0f, 7, col(150, 165, 190));
    t.band_v(r, 0.92f, 1.0f, col(40, 48, 66, 0.6f), 0.05f);
}

// A small four-point sparkle.
void sparkle(Canvas& t, const UvRect& r, vec2 c, float size, vec4 color) {
    t.glow(r, c, size * 0.45f, color, 0.9f);
    t.jagged(r, c - vec2(size, 0.0f), c + vec2(size, 0.0f), size * 0.18f, color, 1, 0.0f);
    t.jagged(r, c - vec2(0.0f, size), c + vec2(0.0f, size), size * 0.18f, color, 1, 0.0f);
}

} // namespace

PaintedModel make_glass_item_box() {
    PaintedModel m;
    m.texture = Canvas(1024, col(128, 128, 128));
    Canvas& t = m.texture;
    MeshBuilder frame, glass;
    const float h = 0.5f;
    const UvRect r_glass = span(0, 0, 3, 3), r_metal = span(3, 0, 4, 1), r_plate = span(3, 1, 4, 2), r_rivet = span(3, 2, 4, 3);

    // Glass: six panels just inside the frame, see-through.
    region(glass, r_glass);
    glass.box_painted({0, 0, 0}, vec3(h * 0.93f), kWhite);
    t.gradient_v(r_glass, col(150, 226, 255, 0.34f), col(80, 160, 225, 0.42f));
    // Swirling cyan and gold mist.
    t.custom(r_glass, [](float u, float v) {
        const vec2 d(u - 0.5f, v - 0.5f);
        const float r = glm::length(d), a = std::atan2(d.y, d.x);
        const float fade = std::max(0.0f, 1.0f - r * 1.5f);
        const float cy = smooth01((std::sin(a * 2.0f + r * 15.0f) - 0.55f) / 0.45f) * fade;
        const float gd = smooth01((std::sin(a * 2.0f + r * 15.0f + 2.6f) - 0.75f) / 0.25f) * fade;
        if (cy >= gd) return vec4(0.5f, 0.92f, 1.0f, 0.5f * cy);
        return vec4(0.98f, 0.8f, 0.36f, 0.4f * gd);
    });
    // Bright bevelled edges, as thick glass catches the light.
    t.custom(r_glass, [](float u, float v) {
        const float e = std::min({u, 1.0f - u, v, 1.0f - v});
        return vec4(0.85f, 0.97f, 1.0f, 0.45f * (1.0f - smooth01(e / 0.07f)));
    });
    uint32_t seed = 17;
    for (int i = 0; i < 14; ++i) {
        const vec2 c(0.08f + 0.84f * hash01(seed++), 0.08f + 0.84f * hash01(seed++));
        sparkle(t, r_glass, c, 0.012f + 0.016f * hash01(seed++), vec4(1.0f, 1.0f, 0.95f, 0.95f));
    }
    // The question mark: dark blue outline, gold body, pale blue core.
    t.question_mark(r_glass, {0.5f, 0.5f}, 0.64f, 0.17f, col(28, 50, 130));
    t.question_mark(r_glass, {0.5f, 0.5f}, 0.64f, 0.12f, col(250, 206, 62));
    t.question_mark(r_glass, {0.5f, 0.5f}, 0.64f, 0.055f, col(160, 220, 255));

    // Frame: a bar along every edge, an L-plate over every corner face, rivets.
    region(frame, r_metal);
    const float bar = 0.045f;
    for (int axis = 0; axis < 3; ++axis)
        for (float a : {-h, h})
            for (float b : {-h, h}) {
                vec3 c(0.0f), half(bar);
                c[(axis + 1) % 3] = a;
                c[(axis + 2) % 3] = b;
                half[axis] = h * 0.9f;
                frame.rounded_box(c, half, 0.03f, kWhite, 14, 8);
            }
    paint_metal(t, r_metal);
    for (float sx : {-1.0f, 1.0f})
        for (float sy : {-1.0f, 1.0f})
            for (float sz : {-1.0f, 1.0f}) {
                const vec3 corner(sx * h, sy * h, sz * h);
                for (int axis = 0; axis < 3; ++axis) {
                    // The plate lies on the face whose normal is `axis`.
                    vec3 n(0.0f);
                    n[axis] = corner[axis] > 0 ? 1.0f : -1.0f;
                    const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
                    vec3 c = corner + n * (bar * 0.65f);
                    c[ua] -= (corner[ua] > 0 ? 1.0f : -1.0f) * 0.075f;
                    c[va] -= (corner[va] > 0 ? 1.0f : -1.0f) * 0.075f;
                    vec3 half(0.0f);
                    half[axis] = 0.022f;
                    half[ua] = 0.105f;
                    half[va] = 0.105f;
                    region(frame, r_plate);
                    frame.rounded_box(c, half, 0.02f, kWhite, 14, 8);
                    region(frame, r_rivet);
                    for (int k = 0; k < 2; ++k) { // two rivets per plate, along the edges
                        vec3 p = corner + n * (bar * 0.65f + 0.024f);
                        const int along = k == 0 ? ua : va, off = k == 0 ? va : ua;
                        p[along] -= (corner[along] > 0 ? 1.0f : -1.0f) * 0.12f;
                        p[off] -= (corner[off] > 0 ? 1.0f : -1.0f) * 0.035f;
                        frame.sphere(p, vec3(0.022f), kWhite, 10, 6);
                    }
                }
            }
    paint_metal(t, r_plate);
    t.vignette(r_plate, 0.3f, 0.1f);
    shade_round(t, r_rivet, col(80, 90, 110), 1.2f);

    m.mesh = frame.take();
    m.glass = glass.take();
    m.finish();
    // Bounds should include the glass.
    for (const auto& v : m.glass.vertices) {
        m.bounds_min = glm::min(m.bounds_min, v.pos);
        m.bounds_max = glm::max(m.bounds_max, v.pos);
    }
    return m;
}

PaintedModel make_banana_peel() {
    PaintedModel m;
    m.texture = Canvas(1024, col(128, 128, 128));
    Canvas& t = m.texture;
    MeshBuilder b;
    const UvRect r_body = span(0, 0, 2, 4), r_flap = span(2, 0, 4, 3), r_stem = span(2, 3, 3, 4);

    // The fruit standing up in the middle, leaning a little.
    region(b, r_body);
    std::vector<vec3> path;
    std::vector<float> radii;
    for (int i = 0; i <= 12; ++i) {
        const float s = float(i) / 12.0f;
        path.push_back({0.06f * std::sin(s * 2.4f), 0.04f + 0.56f * s, -0.05f * s * s});
        radii.push_back(0.115f * (1.0f - 0.55f * s * s) + 0.01f);
    }
    b.sweep(path, radii, kWhite, 18, true);
    // Yellow skin, ripening orange towards one side, green near the top.
    t.custom(r_body, [](float u, float v) {
        const float side = 0.5f + 0.5f * std::sin(u * 2.0f * kPi);
        const vec3 yellow(1.0f, 0.86f, 0.24f), orange(0.95f, 0.6f, 0.12f);
        vec3 c = glm::mix(orange, yellow, side);
        c = glm::mix(c, vec3(0.62f, 0.72f, 0.2f), smooth01((v - 0.72f) / 0.25f));
        return vec4(c, 1.0f);
    });
    // The peeled front shows the cream fruit, with soft fibres.
    t.custom(r_body, [](float u, float v) {
        const float a = smooth01((u - 0.56f) / 0.04f) * smooth01((0.94f - u) / 0.04f) * smooth01((0.8f - v) / 0.08f);
        const float fibre = 0.04f * std::sin(u * 160.0f);
        return vec4(0.98f - fibre, 0.94f - fibre, 0.76f, a);
    });
    t.custom(r_body, [](float u, float v) { // the yellow edge of the peel round the fruit
        const float e = std::min(std::abs(u - 0.56f), std::abs(u - 0.94f));
        return vec4(0.85f, 0.6f, 0.1f, (1.0f - smooth01(e / 0.012f)) * smooth01((0.82f - v) / 0.06f) * 0.8f);
    });
    t.spots(r_body, 7, 0.022f, col(120, 74, 30, 0.85f), 41, 0.4f);
    t.noise(r_body, 0.18f, 8.0f, 43, col(240, 200, 90));

    // Stem with a brown, cut tip.
    region(b, r_stem);
    const vec3 top = path.back();
    b.sweep({top - vec3(0.0f, 0.03f, 0.0f), top + vec3(0.02f, 0.06f, 0.0f), top + vec3(0.06f, 0.11f, 0.01f)}, {0.045f, 0.038f, 0.036f}, kWhite, 12, true);
    t.gradient_v(r_stem, col(140, 160, 60), col(110, 70, 34));
    t.band_v(r_stem, 0.88f, 1.0f, col(90, 56, 30), 0.03f);

    // Four peel flaps spreading on the ground, their tips curling up.
    region(b, r_flap);
    for (int i = 0; i < 4; ++i) {
        const float az = kPi * 0.25f + kPi * 0.5f * float(i) + 0.25f * std::sin(float(i) * 2.3f);
        const vec3 d(std::cos(az), 0.0f, std::sin(az));
        std::vector<vec3> fp;
        std::vector<vec2> fs;
        const float reach = 0.36f + 0.06f * std::sin(float(i) * 1.7f);
        for (int k = 0; k <= 7; ++k) { // out and down to the ground
            const float s = float(k) / 7.0f;
            fp.push_back(d * (0.05f + reach * s) + vec3(0.0f, 0.16f * (1.0f - s) * (1.0f - s) + 0.025f, 0.0f));
            fs.push_back({0.09f - 0.03f * s, 0.013f});
        }
        const float rc = 0.07f;
        const vec3 end = fp.back();
        for (int k = 1; k <= 6; ++k) { // the curl
            const float th = kPi * 1.15f * float(k) / 6.0f;
            fp.push_back(end + d * (rc * std::sin(th)) + vec3(0.0f, rc - rc * std::cos(th), 0.0f));
            fs.push_back({0.06f - 0.025f * float(k) / 6.0f, 0.012f});
        }
        ribbon(b, fp, fs, kWhite, 12);
    }
    // u 0..0.5 is the upper (inner) face: cream; 0.5..1 the outer skin: yellow.
    t.custom(r_flap, [](float u, float v) {
        const bool inner = u < 0.5f;
        const float edge = std::min(std::abs(u - 0.0f), std::min(std::abs(u - 0.5f), std::abs(u - 1.0f)));
        vec3 c = inner ? vec3(0.98f, 0.93f, 0.72f) : glm::mix(vec3(0.98f, 0.82f, 0.2f), vec3(0.93f, 0.58f, 0.1f), v);
        c = glm::mix(c, vec3(0.95f, 0.72f, 0.14f), 1.0f - smooth01(edge / 0.06f)); // yellow rim round the cream
        c = glm::mix(c, vec3(0.55f, 0.6f, 0.2f), smooth01((v - 0.86f) / 0.1f));     // green-brown tips
        return vec4(c, 1.0f);
    });
    t.noise(r_flap, 0.15f, 8.0f, 47, col(240, 210, 120));
    t.spots(r_flap, 5, 0.02f, col(120, 74, 30, 0.7f), 49, 0.4f);

    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel make_tnt_bundle() {
    PaintedModel m;
    m.texture = Canvas(1024, col(128, 128, 128));
    Canvas& t = m.texture;
    MeshBuilder b;
    const UvRect r_top = span(0, 0, 4, 2), r_stick = span(0, 2, 4, 3), r_band = span(0, 3, 2, 4), r_rope = span(2, 3, 3, 4),
                 r_spark = span(3, 3, 4, 4);
    const float rad = 0.14f, len = 0.7f;
    const int body_rows = 20;

    // A stick: a tube along X with crimped, rounded ends. The body rows are
    // evenly spaced so the texture's v runs evenly along it.
    auto stick = [&](vec3 centre, const UvRect& r) {
        region(b, r);
        std::vector<vec2> prof{{0.0f, 0.0f}, {rad * 0.6f, 0.006f}, {rad * 0.92f, 0.022f}};
        const float b0 = 0.045f, b1 = len - 0.045f;
        for (int i = 0; i <= body_rows; ++i) prof.push_back({rad, b0 + (b1 - b0) * float(i) / float(body_rows)});
        prof.push_back({rad * 0.92f, len - 0.022f});
        prof.push_back({rad * 0.6f, len - 0.006f});
        prof.push_back({0.0f, len});
        b.lathe(centre - vec3(len * 0.5f, 0.0f, 0.0f), {1, 0, 0}, prof, 28, kWhite, 1.0f, 1.0f);
    };
    const float rows = float(body_rows + 6);
    const float v_body0 = 3.0f / (rows - 1.0f), v_body1 = (rows - 4.0f) / (rows - 1.0f);
    auto paint_stick = [&](const UvRect& r) {
        // Red paper, lit from above (u = 0.25 is the top), painterly streaks.
        t.custom(r, [](float u, float v) {
            const float lit = 0.72f + 0.32f * std::sin(u * 2.0f * kPi);
            const float streak = 0.025f * std::sin(v * 70.0f + std::sin(u * 25.0f) * 1.5f);
            return vec4(glm::clamp(vec3(0.78f, 0.15f, 0.15f) * (lit + streak), 0.0f, 1.0f), 1.0f);
        });
        t.noise(r, 0.14f, 9.0f, 61, col(200, 60, 60));
        t.band_u(r, 0.6f, 0.612f, col(90, 16, 20, 0.7f), 0.004f); // the paper's seam
        // Crimped ends: folds radiating to a dark centre.
        for (float v0 : {0.0f, v_body1}) {
            const UvRect end{r.x0, r.y0 + v0 * r.h(), r.x1, r.y0 + (v0 + v_body0) * r.h()};
            t.stripes_u(end, 12, col(150, 28, 30), col(105, 18, 22), 0.0f);
        }
        t.band_v(r, 0.0f, 0.03f, col(60, 10, 14), 0.02f);
        t.band_v(r, 0.97f, 1.0f, col(60, 10, 14), 0.02f);
    };
    const float top_y = rad + rad * std::sqrt(3.0f);
    stick({0.0f, rad, -rad}, r_stick);
    stick({0.0f, rad, rad}, r_stick);
    stick({0.0f, top_y, 0.0f}, r_top);
    paint_stick(r_stick);
    paint_stick(r_top);

    // "TNT" on top of the top stick, along its length. Painted per pixel in
    // metres on the stick's surface so the letters keep their shape: up runs
    // round the stick (+u), the reader's right runs back along it (-v).
    {
        const float circ = 2.0f * kPi * rad, kv = (v_body1 - v_body0) / (len - 0.09f);
        // Straight up, centred 0.1 m towards the -X end (v runs with +X).
        const float uc = 0.25f, vc = v_body0 + (len * 0.5f - 0.045f - 0.1f) * kv;
        const float w = 0.095f, hgt = 0.13f, st = 0.032f, outline = 0.008f, gap = 0.12f;
        struct Seg { vec2 a, b; };
        std::vector<Seg> segs;
        auto add_t = [&](float cx) {
            segs.push_back({{cx - w * 0.5f, hgt * 0.5f - st * 0.5f}, {cx + w * 0.5f, hgt * 0.5f - st * 0.5f}});
            segs.push_back({{cx, hgt * 0.5f - st * 0.5f}, {cx, -hgt * 0.5f + st * 0.5f}});
        };
        add_t(-gap);
        add_t(gap);
        const float xl = -w * 0.5f + st * 0.5f, xr = w * 0.5f - st * 0.5f, yt = hgt * 0.5f - st * 0.5f, yb = -hgt * 0.5f + st * 0.5f;
        segs.push_back({{xl, yb}, {xl, yt}});
        segs.push_back({{xl, yt}, {xr, yb}});
        segs.push_back({{xr, yb}, {xr, yt}});
        t.custom(r_top, [&](float u, float v) {
            const vec2 p((vc - v) / kv, (u - uc) * circ);
            if (std::abs(p.x) > 0.3f || std::abs(p.y) > 0.15f) return vec4(0.0f);
            float d = 1e9f;
            for (const Seg& sg : segs) { // squared-off strokes: distance in the max norm along the segment
                const vec2 ab = sg.b - sg.a;
                const float tt = std::clamp(glm::dot(p - sg.a, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f, 1.0f);
                const vec2 q = p - (sg.a + ab * tt);
                d = std::min(d, std::max(std::abs(q.x), std::abs(q.y)) * 0.6f + glm::length(q) * 0.4f);
            }
            const float edge = 0.0015f;
            const float body = 1.0f - smooth01((d - st * 0.5f) / edge + 0.5f);
            const float rim = 1.0f - smooth01((d - st * 0.5f - outline) / edge + 0.5f);
            const vec3 yellow(0.99f, 0.74f, 0.16f), dark(0.28f, 0.05f, 0.06f);
            const vec3 c = glm::mix(dark, yellow * (0.9f + 0.25f * smooth01((p.y + hgt * 0.5f) / hgt)), body);
            return vec4(c, rim);
        });
    }

    // Black tape round the middle of the bundle.
    region(b, r_band);
    {
        const vec2 centres[3] = {{rad, -rad}, {rad, rad}, {top_y, 0.0f}}; // (y, z)
        std::vector<std::vector<vec3>> rings;
        for (float x : {0.1f, 0.23f}) { // nearer the fuse end, clear of the lettering
            std::vector<vec3> ring;
            for (int i = 0; i < 48; ++i) {
                const float a = 2.0f * kPi * float(i) / 48.0f;
                const vec2 d(std::cos(a), std::sin(a));
                float best = -1e9f;
                vec2 at(0.0f);
                for (const vec2& c : centres) {
                    const float s = glm::dot(c, d) + rad + 0.014f;
                    if (s > best) best = s, at = c + d * (rad + 0.014f);
                }
                ring.push_back({x, at.x, at.y});
            }
            rings.push_back(ring);
        }
        b.loft(rings, kWhite, false);
    }
    t.gradient_v(r_band, col(58, 56, 58), col(30, 28, 30));
    t.noise(r_band, 0.25f, 7.0f, 63, col(70, 66, 70));
    t.stripes_u(r_band, 40, vec4(1.0f, 1.0f, 1.0f, 0.0f), vec4(1.0f, 1.0f, 1.0f, 0.05f), 0.3f);

    // The fuse: twisted rope from the top stick's end, curling up.
    region(b, r_rope);
    const vec3 fuse0(len * 0.5f - 0.02f, top_y + 0.02f, 0.0f);
    std::vector<vec3> fuse{fuse0, fuse0 + vec3(0.08f, 0.03f, 0.02f), fuse0 + vec3(0.14f, 0.1f, 0.0f), fuse0 + vec3(0.13f, 0.2f, -0.04f), fuse0 + vec3(0.17f, 0.3f, -0.03f)};
    b.sweep(fuse, 0.017f, kWhite, 10, true);
    t.stripes_u(r_rope, 6, col(212, 170, 110), col(150, 110, 64), 3.0f);
    // A spark at its tip.
    region(b, r_spark);
    const vec3 tip = fuse.back();
    b.sphere(tip, vec3(0.04f), kWhite, 12, 8);
    for (int i = 0; i < 7; ++i) {
        const float a = 2.0f * kPi * float(i) / 7.0f;
        const vec3 d = glm::normalize(vec3(std::cos(a), 0.6f + 0.5f * std::sin(a * 3.0f), std::sin(a)));
        b.tube(tip, tip + d * (0.08f + 0.03f * float(i % 2)), 0.014f, 0.002f, kWhite, 6, true);
    }
    t.fill(r_spark, col(255, 196, 60));
    t.glow(r_spark, {0.5f, 0.5f}, 0.5f, vec4(1.0f, 1.0f, 0.9f, 0.9f), 0.8f);

    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel make_turtle_shell(vec4 color, bool spiked) {
    PaintedModel m;
    m.texture = Canvas(1024, col(128, 128, 128));
    Canvas& t = m.texture;
    MeshBuilder b;
    const UvRect r_dome = span(0, 0, 4, 2), r_rim = span(0, 2, 4, 3), r_belly = span(0, 3, 2, 4), r_spike = span(2, 3, 3, 4), r_wing = span(3, 3, 4, 4);
    const float base_y = 0.13f, R = 0.31f, H = 0.34f;

    // Dome: a quarter ellipse revolved, v from the rim (0) to the top (1).
    region(b, r_dome);
    std::vector<vec2> prof;
    const int rings = 20;
    for (int i = 0; i <= rings; ++i) {
        const float phi = 0.5f * kPi * float(i) / float(rings);
        prof.push_back({R * std::cos(phi), H * std::sin(phi)});
    }
    b.lathe({0, base_y, 0}, {0, 1, 0}, prof, 48, kWhite, 1.0f, 1.0f);

    // Raised plates: a top plate, a ring of six, a lower ring of six offset
    // by half a plate, with dark grooves between them.
    const vec4 base_col = color;
    t.custom(r_dome, [base_col](float u, float v) {
        const float az = u * 2.0f * kPi, lat = v * 0.5f * kPi;
        const float to_v = std::cos(lat) / (0.5f * kPi); // radians of azimuth to v units here
        // Hexagon corners meet the grooves: the top plate dips at the middle
        // ring's grooves (30 deg + k 60), the middle plates dip at the lower
        // ring's grooves (k 60).
        const float top = 0.72f + 0.06f * std::cos(6.0f * az);
        const float mid = 0.36f - 0.07f * std::cos(6.0f * az);
        auto sep = [&](float offset) { // distance to the nearest meridian groove
            float a = std::fmod(az - offset + 100.0f * kPi, kPi / 3.0f);
            return std::min(a, kPi / 3.0f - a) * to_v;
        };
        float dist;
        int plate;
        if (v >= top) {
            dist = v - top;
            plate = 0;
        } else if (v >= mid) {
            dist = std::min({v - mid, top - v, sep(kPi / 6.0f)});
            plate = 1 + int(std::fmod(az + kPi / 6.0f, 2.0f * kPi) / (kPi / 3.0f));
        } else {
            dist = std::min(mid - v, sep(0.0f));
            plate = 7 + int(az / (kPi / 3.0f));
        }
        const float tone = 0.9f + 0.18f * hash01(uint32_t(plate) * 7919u + 3u);
        const float raised = 0.78f + 0.34f * smooth01(dist / 0.07f); // darker near the grooves
        const float light = 0.82f + 0.3f * v;                         // the top catches the light
        vec3 c = vec3(base_col) * tone * raised * light;
        const float groove = 1.0f - smooth01((dist - 0.008f) / 0.01f);
        c = glm::mix(c, vec3(base_col) * 0.32f, groove);
        // A soft highlight on each plate's upper edge.
        const float rim_light = smooth01(dist / 0.02f) * (1.0f - smooth01((dist - 0.02f) / 0.03f));
        c += vec3(0.12f) * rim_light * (0.5f + 0.5f * v);
        return vec4(glm::clamp(c, 0.0f, 1.0f), 1.0f);
    });
    t.noise(r_dome, 0.12f, 14.0f, 71, color);
    t.custom(r_dome, [](float u, float v) { // painterly scratch marks
        const float s = std::sin(u * 220.0f + v * 30.0f) * std::sin(v * 47.0f);
        return vec4(1.0f, 1.0f, 1.0f, s > 0.985f ? 0.25f : 0.0f);
    });

    // Rim: a thick cream tube round the base, segmented, open at the front.
    region(b, r_rim);
    {
        std::vector<vec3> path;
        const int n = 40;
        for (int i = 0; i <= n; ++i) {
            const float th = glm::radians(20.0f) + glm::radians(320.0f) * float(i) / float(n);
            path.push_back({std::sin(th) * R, base_y, std::cos(th) * R});
        }
        b.sweep(path, 0.078f, kWhite, 16, true);
        // u runs round the tube, v along it; the caps are the first and last
        // rings, painted dark as the openings.
        const float cap = 1.0f / float(n + 2);
        t.custom(r_rim, [cap](float u, float v) {
            const float lit = 0.8f + 0.25f * std::sin(u * 2.0f * kPi + 1.2f);
            vec3 c = vec3(0.95f, 0.91f, 0.78f) * lit;
            const float seg = std::abs(std::fmod(v * 14.0f, 1.0f) - 0.5f); // grooves between segments
            c *= 0.72f + 0.28f * smooth01((0.5f - seg) / 0.06f);
            if (v < cap * 1.1f || v > 1.0f - cap * 1.1f) c = vec3(0.12f, 0.08f, 0.06f);
            return vec4(glm::clamp(c, 0.0f, 1.0f), 1.0f);
        });
        t.noise(r_rim, 0.12f, 10.0f, 73, col(240, 230, 200));
    }

    // Underside.
    region(b, r_belly);
    b.lathe({0, base_y, 0}, {0, -1, 0}, {{0.0f, 0.12f}, {0.12f, 0.11f}, {0.24f, 0.07f}, {0.3f, 0.0f}}, 32, kWhite, 1.0f, 1.0f);
    t.gradient_v(r_belly, col(236, 222, 180), col(200, 180, 130));
    t.band_v(r_belly, 0.4f, 0.44f, col(170, 150, 100), 0.02f);

    if (spiked) {
        // Spikes from the plate centres.
        region(b, r_spike);
        auto spike_at = [&](float az, float phi, float len, float w) {
            const vec3 p(R * std::cos(phi) * std::sin(az), base_y + H * std::sin(phi), R * std::cos(phi) * std::cos(az));
            const vec3 n = glm::normalize(vec3(std::cos(phi) * std::sin(az) / R, std::sin(phi) / H, std::cos(phi) * std::cos(az) / R));
            b.tube(p - n * 0.02f, p + n * len, w, 0.004f, kWhite, 14, true);
        };
        spike_at(0.0f, 0.5f * kPi, 0.13f, 0.05f);
        for (int k = 0; k < 6; ++k) spike_at(kPi / 3.0f * float(k), 0.5f * kPi * 0.56f, 0.12f, 0.045f);
        for (int k = 0; k < 6; ++k) spike_at(kPi / 3.0f * float(k) + kPi / 6.0f, 0.5f * kPi * 0.2f, 0.09f, 0.04f);
        t.gradient_v(r_spike, col(255, 252, 238), col(196, 186, 160));
        t.glow(r_spike, {0.3f, 0.3f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.5f), 1.0f);

        // Feathered wings on both sides, swept back and up.
        region(b, r_wing);
        for (float side : {-1.0f, 1.0f}) {
            const vec3 root(side * 0.24f, base_y + 0.2f, -0.04f);
            for (int f = 0; f < 4; ++f) {
                // Feathers fan from up-and-back (f = 0) to out-and-level (f = 3).
                const float a = glm::radians(70.0f - 22.0f * float(f));
                const vec3 dir = glm::normalize(vec3(side * std::cos(a) * 0.8f, std::sin(a), -0.45f));
                const float flen = 0.34f - 0.05f * float(f);
                std::vector<vec3> fp;
                std::vector<vec2> fs;
                for (int k = 0; k <= 6; ++k) {
                    const float s = float(k) / 6.0f;
                    fp.push_back(root + dir * (flen * s) + vec3(0.0f, -0.04f * s * s, -0.03f * s * s) + vec3(side * 0.01f * float(f), 0.0f, 0.0f));
                    fs.push_back({0.012f + 0.05f * std::sin(std::min(s * 1.4f, 1.0f) * kPi * 0.5f) * (1.0f - 0.55f * s * s), 0.012f});
                }
                ribbon(b, fp, fs, kWhite, 10);
            }
        }
        // u: around each feather; the thin edges (u = 0, 0.5) get a grey outline.
        t.custom(r_wing, [](float u, float v) {
            const float edge = std::min({std::abs(u), std::abs(u - 0.5f), std::abs(1.0f - u)});
            vec3 c = glm::mix(vec3(1.0f, 1.0f, 0.98f), vec3(0.82f, 0.84f, 0.88f), v * 0.6f);
            c = glm::mix(c, vec3(0.45f, 0.48f, 0.55f), 1.0f - smooth01(edge / 0.035f));
            return vec4(c, 1.0f);
        });
    }

    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace gk::render
