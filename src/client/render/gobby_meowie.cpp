// Meowie: a small white plush cat after Albert's photo. A round fluffy body
// under a bigger round head, pink inner ears, big dark oval eyes, a tiny
// w-shaped mouth, three whisker dashes under each eye, stubby arms held
// out and chunky round feet. Built in the seat's space: origin at the
// driver's hips, +Z towards the steering wheel.
#include "gobby_assets.h"
#include "gobby_util.h"

namespace gk::render {
namespace {

using namespace gobby;

// White plush: soft top light, a faint grey underside and a little fur grain.
void paint_fur(Canvas& t, const UvRect& r, uint32_t seed) {
    t.gradient_v(r, col(255, 255, 255), col(226, 226, 232));
    t.glow(r, {0.25f, 0.3f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.3f), 1.0f);
    t.noise(r, 0.07f, 26.0f, seed, col(215, 215, 225));
    t.band_v(r, 0.9f, 1.0f, col(190, 190, 205, 0.35f), 0.08f);
}

} // namespace

PaintedModel make_meowie() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const float s = 0.8f; // a small character
    // Texture: the head and body each take a square, the limbs and the pink
    // of the ears the right-hand strip.
    const UvRect r_head = span(0, 0, 2, 2), r_body = span(0, 2, 2, 4);
    const UvRect r_pink = span(2, 0, 3, 1), r_limb = span(3, 0, 4, 2);

    // ---- body: a plump ball sitting on the seat
    const vec3 body(0.0f, 0.2f * s, 0.0f);
    region(b, r_body);
    b.sphere(body, vec3(0.27f, 0.24f, 0.25f) * s, kWhite, 28, 18);
    // Feet: chunky round paws out in front.
    region(b, r_limb);
    for (float x : {-1.0f, 1.0f}) b.sphere(body + vec3(x * 0.15f, -0.13f, 0.22f) * s, vec3(0.12f, 0.1f, 0.14f) * s, kWhite, 16, 10);
    // Arms: stubby, held out towards the wheel, with round paws.
    for (float x : {-1.0f, 1.0f}) {
        const vec3 sh = body + vec3(x * 0.2f, 0.1f, 0.08f) * s, paw = body + vec3(x * 0.3f, 0.2f, 0.42f) * s;
        b.capsule(sh, paw, 0.075f * s, kWhite, 14, 5);
        b.sphere(paw, 0.085f * s, kWhite, 14, 10);
    }

    // ---- head: bigger than the body, a little forward, the face at +Z
    const vec3 head(0.0f, 0.62f * s, 0.05f * s);
    const vec3 hr = vec3(0.34f, 0.3f, 0.32f) * s;
    region(b, r_head);
    b.sphere(head, hr, kWhite, 36, 22);
    // Ears: pointed triangles on top, tipped outward, pink on the inside.
    for (float x : {-1.0f, 1.0f}) {
        const vec3 base = head + vec3(x * 0.19f, 0.22f, -0.02f) * s;
        const vec3 up = glm::normalize(vec3(x * 0.35f, 1.0f, 0.0f));
        region(b, r_limb);
        b.lathe(base, up, {{0.0f, -0.02f * s}, {0.11f * s, 0.0f}, {0.08f * s, 0.07f * s}, {0.035f * s, 0.14f * s}, {0.0f, 0.17f * s}}, 10, kWhite, 1.0f, 1.0f);
        region(b, r_pink);
        b.lathe(base + vec3(0, 0, 0.035f * s), up, {{0.0f, 0.0f}, {0.065f * s, 0.005f * s}, {0.045f * s, 0.06f * s}, {0.0f, 0.12f * s}}, 8, kWhite, 1.0f, 1.0f);
    }

    // ---- paint
    paint_fur(t, r_head, 3);
    paint_fur(t, r_body, 5);
    paint_fur(t, r_limb, 7);
    t.gradient_v(r_pink, col(255, 190, 210), col(245, 150, 185));
    t.glow(r_pink, {0.4f, 0.35f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.3f), 1.0f);
    // The face, centred on u = 0.25 of the head square (the sphere's +Z).
    const vec4 brown = col(60, 36, 34);
    for (float x : {-1.0f, 1.0f}) {
        const vec2 eye(0.25f + x * 0.115f, 0.5f);
        t.ellipse(r_head, eye, {0.058f, 0.078f}, brown, 0.015f);
        t.ellipse(r_head, eye + vec2(-0.015f, -0.025f), {0.014f, 0.018f}, col(255, 255, 255, 0.9f), 0.01f); // a glint
        // Three short whisker dashes under the eye, fanning out.
        for (int i = 0; i < 3; ++i) {
            const float k = float(i) - 1.0f;
            const vec2 a = eye + vec2(x * (0.09f + k * 0.03f), 0.11f + std::abs(k) * 0.02f);
            const vec2 d(x * 0.022f, -0.012f * (0.5f + k));
            t.jagged(r_head, a - d, a + d, 0.012f, brown, 1, 0.0f);
        }
    }
    // A tiny w-shaped mouth: two little dips.
    t.mouth(r_head, {0.237f, 0.585f}, 0.03f, 0.018f, 0.011f, brown);
    t.mouth(r_head, {0.263f, 0.585f}, 0.03f, 0.018f, 0.011f, brown);

    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace gk::render
