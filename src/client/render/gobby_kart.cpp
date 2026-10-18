// The Gobby Kart standard kart and its tyres, painted in the style of the
// game's other models: a red racer with white stripes, a padded bucket seat,
// gold steering wheel, chrome exhausts and a striped rear wing.
#include "gobby_assets.h"
#include "gobby_util.h"

namespace gk::render {
namespace {

using namespace gobby;

// A cross-section of a body: a rounded rectangle in the XY plane at z,
// `round` from 0 (box) to 1 (ellipse).
struct Section {
    float z, y_lo, y_hi, half_w;
    float round = 0.5f;
    float x = 0.0f;
};
constexpr int kRing = 20;

std::vector<vec3> ring_of(const Section& s) {
    std::vector<vec3> r;
    const float hh = std::max(0.5f * (s.y_hi - s.y_lo), 0.002f), cy = 0.5f * (s.y_hi + s.y_lo);
    const float hw = std::max(s.half_w, 0.002f);
    const float p = glm::mix(8.0f, 2.0f, std::clamp(s.round, 0.0f, 1.0f)); // superellipse exponent
    for (int i = 0; i < kRing; ++i) {
        const float t = 2.0f * kPi * float(i) / float(kRing);
        const float c = std::cos(t), sn = std::sin(t);
        r.push_back({s.x + std::copysign(std::pow(std::abs(c), 2.0f / p), c) * hw, cy + std::copysign(std::pow(std::abs(sn), 2.0f / p), sn) * hh, s.z});
    }
    return r;
}

// Skins sections, rear to front, into one smooth shell. Texture u runs
// around the section (0 = +x side, 0.25 = top, 0.5 = -x, 0.75 = bottom) and
// v from the rear cap to the front cap.
void hull(MeshBuilder& b, const std::vector<Section>& sections) {
    std::vector<std::vector<vec3>> rings;
    for (const Section& s : sections) rings.push_back(ring_of(s));
    b.loft(rings, kWhite, true);
}

uint32_t hex_rgba_local(uint32_t rgb) { return rgba(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

vec4 paint_of(uint32_t rgba8) {
    return vec4(float(rgba8 & 0xff), float((rgba8 >> 8) & 0xff), float((rgba8 >> 16) & 0xff), 255.0f) / 255.0f;
}

// Car paint on a lofted shell: a glossy top, a darker underside.
void paint_shell(Canvas& t, const UvRect& r, vec4 paint) {
    t.fill(r, paint);
    t.band_u(r, 0.08f, 0.42f, vec4(1.0f, 1.0f, 1.0f, 0.12f), 0.12f);  // top catches the light
    t.band_u(r, 0.55f, 0.95f, vec4(0.0f, 0.0f, 0.04f, 0.35f), 0.15f); // underside in shadow
    t.band_u(r, 0.22f, 0.28f, vec4(1.0f, 1.0f, 1.0f, 0.12f), 0.03f);  // a spine highlight
}

void paint_chrome(Canvas& t, const UvRect& r) {
    // Sky above, a dark horizon line, the ground reflected below.
    t.gradient_v(r, col(245, 248, 255), col(170, 176, 190));
    t.band_v(r, 0.45f, 0.6f, col(70, 74, 88), 0.05f);
    t.band_v(r, 0.6f, 1.0f, col(120, 118, 115), 0.1f);
    t.band_v(r, 0.15f, 0.25f, vec4(1.0f, 1.0f, 1.0f, 0.6f), 0.05f);
}

void paint_gold(Canvas& t, const UvRect& r) {
    t.gradient_v(r, col(255, 225, 110), col(175, 120, 25));
    t.band_v(r, 0.25f, 0.35f, vec4(1.0f, 1.0f, 0.85f, 0.6f), 0.05f);
    t.band_v(r, 0.6f, 0.7f, col(130, 85, 20, 0.6f), 0.05f);
}

// Padded upholstery: navy with tuck lines and soft highlights.
void paint_seat(Canvas& t, const UvRect& r, int rows, bool seams) {
    const vec4 navy = col(42, 48, 70);
    t.fill(r, navy);
    t.noise(r, 0.12f, 9.0f, 5, navy);
    for (int i = 1; i < rows; ++i) {
        const float v = float(i) / float(rows);
        t.band_v(r, v - 0.012f, v + 0.012f, col(18, 20, 32), 0.006f);
        t.band_v(r, v + 0.015f, v + 0.06f, vec4(1.0f, 1.0f, 1.0f, 0.08f), 0.02f);
    }
    if (seams)
        for (float u : {0.19f, 0.31f}) t.band_u(r, u - 0.008f, u + 0.008f, col(18, 20, 32), 0.004f);
    t.glow(r, {0.25f, 0.3f}, 0.3f, vec4(0.6f, 0.65f, 0.85f, 0.2f), 1.0f);
}

// Builds `part` in its own builder and adds it rotated about `pivot`.
template <class F> void rotated(MeshBuilder& b, const UvRect& r, vec3 pivot, vec3 axis, float degrees, F&& part) {
    MeshBuilder sub;
    region(sub, r);
    part(sub);
    const mat4 m = glm::translate(mat4(1.0f), pivot) * glm::rotate(mat4(1.0f), glm::radians(degrees), axis) * glm::translate(mat4(1.0f), -pivot);
    b.append(sub.data(), m);
}

} // namespace

PaintedModel make_gobby_kart(uint32_t paint_rgba8) {
    PaintedModel m;
    m.texture = Canvas(1024, col(128, 128, 128));
    MeshBuilder b;
    Canvas& t = m.texture;
    const vec4 paint = paint_of(paint_rgba8);
    const float lum = 0.3f * paint.r + 0.59f * paint.g + 0.11f * paint.b;
    const vec4 stripe = lum > 0.72f ? col(30, 40, 80) : col(250, 250, 248);

    // Texture regions on an 8 x 8 grid of the 1024 px canvas.
    const UvRect r_hood = span(0, 0, 4, 4, 8), r_tub = span(4, 0, 8, 2, 8), r_deck = span(4, 2, 8, 3, 8);
    const UvRect r_pod = span(4, 3, 6, 4, 8), r_bumper = span(6, 3, 8, 4, 8);
    const UvRect r_seat_back = span(0, 4, 2, 6, 8), r_cushion = span(2, 4, 4, 5, 8);
    const UvRect r_dark = span(2, 5, 3, 6, 8), r_grille = span(3, 5, 4, 6, 8);
    const UvRect r_chrome = span(4, 4, 6, 5, 8), r_gold = span(6, 4, 7, 5, 8), r_glass = span(7, 4, 8, 5, 8);
    const UvRect r_amber = span(4, 5, 5, 6, 8), r_tail = span(5, 5, 6, 6, 8), r_dash = span(6, 5, 8, 6, 8);
    const UvRect r_wing = span(0, 6, 4, 7, 8), r_plate = span(4, 6, 5, 7, 8), r_engine = span(5, 6, 8, 8, 8);
    const UvRect r_floor = span(0, 7, 2, 8, 8), r_pipe_end = span(2, 7, 3, 8, 8), r_spring = span(3, 7, 4, 8, 8);

    // ---- bodywork (faces +Z, wheels at about x = 0.62, z = +-0.66)
    region(b, r_tub); // chassis: a low shell under everything
    hull(b, {{-1.02f, 0.2f, 0.36f, 0.3f, 0.5f}, {-0.9f, 0.17f, 0.38f, 0.4f, 0.4f}, {-0.3f, 0.16f, 0.36f, 0.43f, 0.35f}, {0.4f, 0.16f, 0.36f, 0.42f, 0.35f},
             {0.95f, 0.18f, 0.36f, 0.38f, 0.45f}, {1.04f, 0.22f, 0.34f, 0.3f, 0.6f}});
    paint_shell(t, r_tub, paint);

    region(b, r_hood); // the hood: high and rounded, sloping to the nose
    hull(b, {{0.18f, 0.3f, 0.64f, 0.38f, 0.3f}, {0.3f, 0.28f, 0.68f, 0.41f, 0.32f}, {0.6f, 0.27f, 0.66f, 0.43f, 0.35f}, {0.85f, 0.26f, 0.6f, 0.42f, 0.4f},
             {1.0f, 0.27f, 0.53f, 0.4f, 0.48f}, {1.07f, 0.31f, 0.47f, 0.33f, 0.65f}});
    paint_shell(t, r_hood, paint);
    t.band_u(r_hood, 0.193f, 0.232f, stripe, 0.004f); // twin racing stripes over the top
    t.band_u(r_hood, 0.268f, 0.307f, stripe, 0.004f);
    t.glow(r_hood, {0.28f, 0.55f}, 0.12f, vec4(1.0f, 1.0f, 1.0f, 0.25f), 1.0f);
    t.band_v(r_hood, 0.0f, 0.18f, vec4(0.0f, 0.0f, 0.0f, 0.25f), 0.12f); // the cockpit side of the hood

    region(b, r_tub); // side sills along the cockpit
    for (float x : {-0.39f, 0.39f}) b.rounded_box({x, 0.44f, -0.12f}, {0.065f, 0.13f, 0.42f}, 0.06f, kWhite, 16, 10);

    region(b, r_deck); // rear deck behind the seat, over the engine bay
    hull(b, {{-1.04f, 0.26f, 0.5f, 0.32f, 0.5f}, {-0.94f, 0.22f, 0.56f, 0.41f, 0.4f}, {-0.62f, 0.2f, 0.56f, 0.43f, 0.35f}, {-0.5f, 0.24f, 0.52f, 0.4f, 0.45f}});
    paint_shell(t, r_deck, paint);
    {   // stripes over the deck top, stopping short of the rear cap
        const UvRect top{r_deck.x0, r_deck.y0 + 0.2f * r_deck.h(), r_deck.x1, r_deck.y1};
        t.band_u(top, 0.193f, 0.232f, stripe, 0.004f);
        t.band_u(top, 0.268f, 0.307f, stripe, 0.004f);
    }

    region(b, r_pod); // side pods behind the front wheels, with a white belt
    for (float x : {-0.47f, 0.47f})
        hull(b, {{-0.52f, 0.22f, 0.46f, 0.07f, 0.5f, x}, {-0.46f, 0.2f, 0.5f, 0.11f, 0.45f, x}, {-0.12f, 0.2f, 0.5f, 0.11f, 0.45f, x},
                 {-0.04f, 0.22f, 0.46f, 0.07f, 0.55f, x}});
    paint_shell(t, r_pod, paint);
    t.band_v(r_pod, 0.36f, 0.64f, stripe, 0.01f);

    region(b, r_floor); // floor pan
    b.rounded_box({0, 0.17f, 0}, {0.37f, 0.03f, 0.92f}, 0.025f, kWhite, 16, 8);
    t.gradient_v(r_floor, col(50, 52, 60), col(25, 26, 30));

    // ---- front: bumper, grille, lights
    region(b, r_bumper);
    b.rounded_box({0, 0.31f, 1.1f}, {0.45f, 0.085f, 0.075f}, 0.06f, kWhite, 20, 12);
    for (float x : {-0.42f, 0.42f}) b.rounded_box({x, 0.31f, 1.04f}, {0.06f, 0.09f, 0.11f}, 0.05f, kWhite, 12, 10);
    shade_round(t, r_bumper, paint, 1.0f);
    region(b, r_chrome);
    b.rounded_box({0, 0.215f, 1.11f}, {0.3f, 0.025f, 0.05f}, 0.02f, kWhite, 14, 8);
    paint_chrome(t, r_chrome);
    region(b, r_grille);
    b.rounded_box({0, 0.43f, 1.055f}, {0.22f, 0.07f, 0.035f}, 0.03f, kWhite, 14, 10);
    t.fill(r_grille, col(26, 28, 34));
    for (int i = 0; i < 6; ++i) {
        const float v = 0.18f + 0.13f * float(i);
        t.band_v(r_grille, v, v + 0.05f, col(120, 125, 140), 0.01f);
    }
    for (float x : {-0.29f, 0.29f}) { // round headlights in chrome cups on the fender corners
        region(b, r_chrome);
        b.lathe({x, 0.47f, 0.96f}, {0, 0, 1}, {{0.0f, 0.0f}, {0.07f, 0.0f}, {0.088f, 0.07f}, {0.09f, 0.11f}, {0.076f, 0.12f}}, 20, kWhite, 1.0f, 1.0f);
        region(b, r_glass);
        b.sphere({x, 0.47f, 1.075f}, vec3(0.074f, 0.074f, 0.03f), kWhite, 16, 10);
        region(b, r_amber);
        b.rounded_box({x * 1.33f, 0.3f, 1.16f}, {0.045f, 0.025f, 0.02f}, 0.012f, kWhite, 10, 6);
    }
    t.gradient_v(r_glass, col(255, 255, 250), col(225, 232, 240));
    t.glow(r_glass, {0.35f, 0.3f}, 0.22f, vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.6f);
    t.vignette(r_glass, 0.15f, 0.2f);
    t.gradient_v(r_amber, col(255, 190, 80), col(240, 120, 20));
    t.glow(r_amber, {0.4f, 0.3f}, 0.3f, vec4(1.0f, 1.0f, 0.8f, 0.6f), 1.0f);

    // Suspension: arms and coil springs out to the wheels.
    region(b, r_dark);
    for (float side : {-1.0f, 1.0f})
        for (float z : {0.66f, -0.66f}) {
            b.capsule({side * 0.36f, 0.26f, z}, {side * 0.56f, 0.26f, z}, 0.028f, kWhite, 8, 3);
            b.capsule({side * 0.36f, 0.22f, z + 0.08f}, {side * 0.56f, 0.24f, z + 0.02f}, 0.02f, kWhite, 8, 3);
        }
    t.gradient_v(r_dark, col(70, 72, 82), col(28, 30, 36));
    region(b, r_spring);
    for (float side : {-1.0f, 1.0f}) {
        std::vector<vec3> coil;
        for (int i = 0; i <= 36; ++i) {
            const float a = float(i) * 0.6f;
            coil.push_back({side * 0.43f + std::cos(a) * 0.04f, 0.28f + float(i) * 0.006f, 0.6f + std::sin(a) * 0.04f});
        }
        b.sweep(coil, 0.011f, kWhite, 6, true);
        b.tube({side * 0.43f, 0.26f, 0.6f}, {side * 0.43f, 0.52f, 0.6f}, 0.02f, 0.02f, kWhite, 8, true);
    }
    t.gradient_v(r_spring, col(70, 110, 220), col(30, 50, 130));

    // ---- cockpit
    // Dashboard with a chrome-rimmed gauge cluster facing the driver.
    region(b, r_dark);
    b.rounded_box({0, 0.62f, 0.25f}, {0.34f, 0.06f, 0.07f}, 0.05f, kWhite, 16, 10);
    rotated(b, r_chrome, {0, 0.7f, 0.25f}, {1, 0, 0}, -25.0f, [](MeshBuilder& s) { s.rounded_box({0, 0.72f, 0.25f}, {0.19f, 0.085f, 0.03f}, 0.03f, kWhite, 14, 10); });
    rotated(b, r_dash, {0, 0.7f, 0.25f}, {1, 0, 0}, -25.0f, [](MeshBuilder& s) { s.box_painted({0, 0.72f, 0.215f}, {0.165f, 0.065f, 0.004f}, kWhite); });
    t.fill(r_dash, col(22, 24, 30));
    for (float u : {0.3f, 0.7f}) {
        t.ellipse(r_dash, {u, 0.5f}, {0.17f, 0.36f}, col(225, 228, 235), 0.02f);
        t.ellipse(r_dash, {u, 0.5f}, {0.14f, 0.3f}, col(18, 20, 26), 0.02f);
        for (int k = 0; k < 7; ++k) { // tick marks
            const float a = kPi * (0.85f + 1.3f * float(k) / 6.0f);
            t.ellipse(r_dash, {u + std::cos(a) * 0.11f, 0.5f + std::sin(a) * 0.24f}, {0.008f, 0.02f}, k > 4 ? col(240, 60, 50) : col(230, 230, 230), 0.006f);
        }
        t.jagged(r_dash, {u, 0.5f}, {u + 0.07f, 0.34f}, 0.018f, col(250, 70, 50), 3, 0.0f);
        t.ellipse(r_dash, {u, 0.5f}, {0.025f, 0.05f}, col(200, 200, 205), 0.01f);
    }
    t.ellipse(r_dash, {0.5f, 0.25f}, {0.025f, 0.06f}, col(80, 230, 90), 0.01f);
    t.ellipse(r_dash, {0.5f, 0.75f}, {0.025f, 0.06f}, col(240, 70, 60), 0.01f);

    // Gold steering wheel on a chrome column; the driver's hands go at (0, 0.8, 0.32).
    const vec3 wheel_c(0, 0.78f, 0.35f);
    const vec3 wheel_axis = glm::normalize(vec3(0, 0.55f, -1.0f));
    region(b, r_gold);
    b.torus(wheel_c, wheel_axis, 0.155f, 0.026f, kWhite, 28, 10);
    {
        const vec3 u = glm::normalize(glm::cross(vec3(0, 1, 0), wheel_axis)), v = glm::cross(wheel_axis, u);
        for (int i = 0; i < 3; ++i) {
            const float a = kPi * 0.5f + 2.0f * kPi * float(i) / 3.0f + kPi;
            b.capsule(wheel_c, wheel_c + (u * std::cos(a) + v * std::sin(a)) * 0.14f, 0.018f, kWhite, 8, 3);
        }
        b.sphere(wheel_c - wheel_axis * 0.005f, vec3(0.045f), kWhite, 14, 10);
    }
    paint_gold(t, r_gold);
    region(b, r_chrome);
    b.tube(wheel_c + wheel_axis * 0.02f, wheel_c + wheel_axis * 0.3f, 0.022f, 0.026f, kWhite, 10, true);

    // Padded bucket seat, leaning back a little.
    rotated(b, r_seat_back, {0, 0.38f, -0.45f}, {1, 0, 0}, -12.0f, [](MeshBuilder& s) {
        s.rounded_box({0, 0.72f, -0.47f}, {0.22f, 0.3f, 0.06f}, 0.06f, kWhite, 24, 16);
    });
    paint_seat(t, r_seat_back, 5, true);
    rotated(b, r_cushion, {0, 0.38f, -0.45f}, {1, 0, 0}, -12.0f, [](MeshBuilder& s) {
        for (float x : {-0.24f, 0.24f}) s.rounded_box({x, 0.66f, -0.45f}, {0.055f, 0.3f, 0.09f}, 0.05f, kWhite, 14, 12); // bolsters
        s.rounded_box({0, 1.02f, -0.46f}, {0.2f, 0.05f, 0.07f}, 0.045f, kWhite, 14, 10);                                 // headrest roll
    });
    region(b, r_cushion);
    b.rounded_box({0, 0.36f, -0.22f}, {0.23f, 0.06f, 0.21f}, 0.055f, kWhite, 20, 12);
    for (float x : {-0.23f, 0.23f}) b.rounded_box({x, 0.4f, -0.22f}, {0.045f, 0.06f, 0.2f}, 0.04f, kWhite, 12, 8);
    paint_seat(t, r_cushion, 3, false);

    // ---- rear: engine, exhausts, wing, lights
    region(b, r_engine);
    b.rounded_box({0, 0.62f, -0.8f}, {0.2f, 0.12f, 0.15f}, 0.04f, kWhite, 18, 12);
    for (float x : {-0.12f, 0.12f}) b.rounded_box({x, 0.77f, -0.8f}, {0.06f, 0.04f, 0.12f}, 0.025f, kWhite, 12, 8);
    t.gradient_v(r_engine, col(110, 112, 122), col(40, 42, 50));
    for (int i = 0; i < 10; ++i) t.band_v(r_engine, 0.1f + 0.08f * float(i), 0.13f + 0.08f * float(i), col(25, 26, 32), 0.01f);
    region(b, r_bumper);
    b.sphere({0, 0.84f, -0.74f}, vec3(0.06f, 0.035f, 0.06f), kWhite, 12, 8); // red filler cap
    for (float side : {-1.0f, 1.0f}) { // twin chrome exhausts with big cans
        const vec3 a(side * 0.16f, 0.6f, -0.88f), c(side * 0.26f, 0.7f, -1.02f), d = glm::normalize(vec3(side * 0.25f, 0.55f, -1.0f));
        region(b, r_chrome);
        b.sweep({a, a + vec3(side * 0.04f, -0.02f, -0.06f), c}, 0.042f, kWhite, 12, true);
        b.lathe(c, d, {{0.0f, 0.0f}, {0.06f, 0.0f}, {0.085f, 0.05f}, {0.09f, 0.3f}, {0.075f, 0.34f}}, 18, kWhite, 1.0f, 1.0f);
        region(b, r_pipe_end);
        b.lathe(c, d, {{0.075f, 0.34f}, {0.058f, 0.33f}, {0.05f, 0.29f}, {0.0f, 0.29f}}, 18, kWhite, 1.0f, 1.0f);
    }
    t.gradient_v(r_pipe_end, col(60, 50, 45), col(15, 12, 12));
    region(b, r_dark); // wing struts
    for (float x : {-0.2f, 0.2f}) b.tube({x, 0.5f, -0.92f}, {x, 1.0f, -1.0f}, 0.03f, 0.025f, kWhite, 10, true);
    region(b, r_wing);
    b.box_painted({0, 1.03f, -1.0f}, {0.5f, 0.025f, 0.13f}, kWhite);
    t.fill(r_wing, paint);
    t.gradient_v(r_wing, vec4(1.0f, 1.0f, 1.0f, 0.15f), vec4(0.0f, 0.0f, 0.0f, 0.2f));
    t.band_u(r_wing, 0.36f, 0.45f, stripe, 0.004f);
    t.band_u(r_wing, 0.55f, 0.64f, stripe, 0.004f);
    region(b, r_plate);
    for (float x : {-0.52f, 0.52f}) b.rounded_box({x, 1.02f, -1.0f}, {0.022f, 0.08f, 0.16f}, 0.018f, kWhite, 10, 8);
    shade_round(t, r_plate, paint, 1.0f);
    region(b, r_tail);
    for (float x : {-0.28f, 0.28f}) b.rounded_box({x, 0.42f, -1.03f}, {0.07f, 0.035f, 0.025f}, 0.018f, kWhite, 10, 8);
    t.gradient_v(r_tail, col(255, 90, 80), col(170, 20, 20));
    t.glow(r_tail, {0.4f, 0.3f}, 0.3f, vec4(1.0f, 0.8f, 0.8f, 0.6f), 1.0f);

    m.mesh = b.take();
    m.finish();
    return m;
}

PaintedModel make_gobby_tire(const sim::PartLook& look) {
    // A wheel of radius 0.5 on the X axis, hub facing +X; the game scales it
    // to each wheel's radius and turns the left ones to face out.
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const std::string& shape = look.shape;
    const bool chunky = shape == "chunky", thin = shape == "thin", wide = shape == "wide";
    const float r = 0.5f, hw = thin ? 0.13f : wide ? 0.34f : chunky ? 0.32f : 0.24f;
    const float rim = thin ? 0.34f : chunky ? 0.28f : 0.31f;
    // Treaded tyres are near-black rubber with a hint of the part's colour;
    // slicks keep their colour.
    vec4 rubber = paint_of(hex_rgba_local(look.color));
    if (shape == "normal" || shape == "chunky") rubber = glm::mix(col(26, 26, 30), rubber, 0.35f);
    const vec4 hub = paint_of(hex_rgba_local(look.color2));
    const float block = chunky ? 0.05f : shape == "normal" ? 0.018f : 0.0f;
    const float body_r = r - block;

    const UvRect r_tread = span(0, 0, 4, 2), r_rim = span(0, 2, 2, 4), r_cap = span(2, 2, 3, 3), r_block = span(3, 2, 4, 3), r_inner = span(2, 3, 4, 4);
    // Tyre: a rounded torus-like band from the inner bead round to the outer.
    region(b, r_tread);
    b.lathe({0, 0, 0}, {1, 0, 0},
            {{rim, -hw * 0.82f}, {body_r * 0.84f, -hw}, {body_r * 0.97f, -hw * 0.86f}, {body_r, -hw * 0.55f}, {body_r, hw * 0.55f}, {body_r * 0.97f, hw * 0.86f},
             {body_r * 0.84f, hw}, {rim, hw * 0.82f}},
            36, kWhite, 1.0f, 1.0f);
    // v: 0 inner bead .. 1 outer bead; the tread is the middle third.
    t.fill(r_tread, rubber);
    t.band_v(r_tread, 0.0f, 0.3f, shade(rubber, 0.8f), 0.08f);
    t.band_v(r_tread, 0.7f, 1.0f, shade(rubber, 0.8f), 0.08f);
    t.band_v(r_tread, 0.4f, 0.6f, vec4(1.0f, 1.0f, 1.0f, 0.06f), 0.05f);
    if (shape == "normal" || chunky) { // grooves across the tread
        UvRect tread{r_tread.x0, r_tread.y0 + 0.3f * r_tread.h(), r_tread.x1, r_tread.y0 + 0.7f * r_tread.h()};
        t.stripes_u(tread, chunky ? 36 : 48, vec4(0.0f, 0.0f, 0.0f, 0.0f), vec4(0.0f, 0.0f, 0.0f, 0.35f), 0.35f);
    } else if (thin || wide) { // slick: a pinstripe on the shoulder
        t.band_v(r_tread, 0.18f, 0.21f, shade(hub, 1.0f), 0.005f);
        t.band_v(r_tread, 0.79f, 0.82f, shade(hub, 1.0f), 0.005f);
    }
    t.noise(r_tread, 0.15f, 8.0f, 41, rubber);
    t.band_v(r_tread, 0.88f, 0.94f, vec4(1.0f, 1.0f, 1.0f, 0.12f), 0.02f); // lettering ring on the outer wall

    // Knobbly blocks round the tread, in two staggered rows.
    if (block > 0.0f) {
        const int n = chunky ? 14 : 18;
        for (int row = 0; row < (chunky ? 2 : 1); ++row)
            for (int i = 0; i < n; ++i) {
                const float a = 2.0f * kPi * (float(i) + (row ? 0.5f : 0.0f)) / float(n);
                const float x = chunky ? (row ? hw * 0.42f : -hw * 0.42f) : 0.0f;
                const float bw = chunky ? hw * 0.5f : hw * 0.8f;
                MeshBuilder blk;
                region(blk, r_block);
                blk.box_painted({x, body_r + block * 0.45f, 0.0f}, {bw, block * 0.75f, chunky ? 0.07f : 0.03f}, kWhite);
                b.append(blk.data(), glm::rotate(mat4(1.0f), a, vec3(1, 0, 0)));
            }
        t.gradient_v(r_block, shade(rubber, 1.25f), shade(rubber, 0.7f));
    }

    // Rim: a dished wheel with five spokes on the outside, a plain dish inside.
    region(b, r_rim);
    b.lathe({0, 0, 0}, {1, 0, 0}, {{rim, hw * 0.82f}, {rim * 0.97f, hw * 0.62f}, {rim * 0.75f, hw * 0.58f}, {rim * 0.32f, hw * 0.66f}, {0.0f, hw * 0.7f}}, 30, kWhite, 1.0f, 1.0f);
    // v: 0 at the lip .. 1 at the centre. Lip bright, spokes with dark gaps.
    t.fill(r_rim, hub);
    t.band_v(r_rim, 0.0f, 0.18f, shade(hub, 1.3f), 0.03f);
    t.band_v(r_rim, 0.18f, 0.24f, shade(hub, 0.6f), 0.02f);
    {
        UvRect spokes{r_rim.x0, r_rim.y0 + 0.3f * r_rim.h(), r_rim.x1, r_rim.y0 + 0.72f * r_rim.h()};
        for (int i = 0; i < 5; ++i) {
            const float u = (float(i) + 0.5f) / 5.0f;
            t.band_u(spokes, u - 0.065f, u + 0.065f, col(20, 20, 24), 0.02f);
        }
    }
    t.glow(r_rim, {0.25f, 0.5f}, 0.4f, vec4(1.0f, 1.0f, 1.0f, 0.25f), 1.0f);
    region(b, r_inner);
    b.lathe({0, 0, 0}, {-1, 0, 0}, {{rim, hw * 0.82f}, {rim * 0.9f, hw * 0.6f}, {0.0f, hw * 0.6f}}, 24, kWhite, 1.0f, 1.0f);
    t.fill(r_inner, shade(hub, 0.55f));
    // Hub cap with a centre nut.
    region(b, r_cap);
    b.lathe({hw * 0.66f, 0, 0}, {1, 0, 0}, {{0.11f, 0.0f}, {0.1f, 0.05f}, {0.06f, 0.08f}, {0.0f, 0.09f}}, 20, kWhite, 1.0f, 1.0f);
    t.gradient_v(r_cap, shade(hub, 1.3f), shade(hub, 0.8f));
    t.glow(r_cap, {0.3f, 0.6f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.5f), 1.0f);

    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace gk::render
