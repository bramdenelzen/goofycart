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
    t.gradient_v(r, col(255, 255, 255), col(232, 230, 238));
    t.glow(r, {0.25f, 0.3f}, 0.32f, vec4(1.0f, 1.0f, 1.0f, 0.35f), 1.0f);
    t.noise(r, 0.05f, 30.0f, seed, col(222, 220, 230));
    t.band_v(r, 0.88f, 1.0f, col(196, 192, 210, 0.3f), 0.1f);
}

} // namespace

PaintedModel make_meowie() {
    PaintedModel m;
    MeshBuilder b;
    Canvas& t = m.texture;
    const float s = 0.8f; // a small character
    // Texture: the head and body each take a square; the right-hand strip
    // holds the limbs, the pink of the ears and nose, and the cheek blush.
    const UvRect r_head = span(0, 0, 2, 2), r_body = span(0, 2, 2, 4);
    const UvRect r_pink = span(2, 0, 3, 1), r_limb = span(3, 0, 4, 2), r_pad = span(2, 1, 3, 2);

    // ---- body: a plump ball sitting on the seat, a round tail puff behind
    const vec3 body(0.0f, 0.2f * s, 0.0f);
    region(b, r_body);
    b.sphere(body, vec3(0.27f, 0.24f, 0.25f) * s, kWhite, 40, 26);
    b.sphere(body + vec3(0.0f, 0.0f, -0.26f) * s, 0.09f * s, kWhite, 18, 12);
    // Feet: chunky round paws out in front, with pink pads underneath.
    region(b, r_limb);
    for (float x : {-1.0f, 1.0f}) b.sphere(body + vec3(x * 0.15f, -0.13f, 0.22f) * s, vec3(0.12f, 0.1f, 0.14f) * s, kWhite, 22, 14);
    region(b, r_pad);
    for (float x : {-1.0f, 1.0f}) b.sphere(body + vec3(x * 0.15f, -0.2f, 0.27f) * s, vec3(0.07f, 0.03f, 0.07f) * s, kWhite, 12, 8);
    // Arms: stubby, held out towards the wheel, with round paws.
    region(b, r_limb);
    for (float x : {-1.0f, 1.0f}) {
        const vec3 sh = body + vec3(x * 0.2f, 0.1f, 0.08f) * s, paw = body + vec3(x * 0.3f, 0.2f, 0.42f) * s;
        b.capsule(sh, paw, 0.075f * s, kWhite, 18, 6);
        b.sphere(paw, 0.09f * s, kWhite, 18, 12);
    }

    // ---- head: bigger than the body, a little forward, the face at +Z
    const vec3 head(0.0f, 0.62f * s, 0.05f * s);
    const vec3 hr = vec3(0.35f, 0.3f, 0.32f) * s;
    region(b, r_head);
    b.sphere(head, hr, kWhite, 56, 36);
    // Fluffy cheeks either side of the muzzle.
    region(b, r_limb);
    for (float x : {-1.0f, 1.0f}) b.sphere(head + vec3(x * 0.2f, -0.1f, 0.22f) * s, vec3(0.11f, 0.08f, 0.09f) * s, kWhite, 18, 12);
    // Ears: pointed triangles on top, tipped outward, pink on the inside.
    for (float x : {-1.0f, 1.0f}) {
        const vec3 base = head + vec3(x * 0.19f, 0.22f, -0.02f) * s;
        const vec3 up = glm::normalize(vec3(x * 0.35f, 1.0f, 0.0f));
        region(b, r_limb);
        b.lathe(base, up, {{0.0f, -0.02f * s}, {0.11f * s, 0.0f}, {0.08f * s, 0.07f * s}, {0.035f * s, 0.14f * s}, {0.0f, 0.17f * s}}, 14, kWhite, 1.0f, 1.0f);
        region(b, r_pink);
        b.lathe(base + vec3(0, 0, 0.035f * s), up, {{0.0f, 0.0f}, {0.07f * s, 0.005f * s}, {0.05f * s, 0.06f * s}, {0.0f, 0.125f * s}}, 12, kWhite, 1.0f, 1.0f);
    }
    // A tiny pink nose.
    region(b, r_pink);
    b.sphere(head + vec3(0.0f, -0.04f, 0.31f) * s, vec3(0.03f, 0.022f, 0.02f) * s, kWhite, 12, 8);

    // ---- paint
    paint_fur(t, r_head, 3);
    paint_fur(t, r_body, 5);
    paint_fur(t, r_limb, 7);
    t.gradient_v(r_pink, col(255, 190, 212), col(246, 150, 186));
    t.glow(r_pink, {0.4f, 0.35f}, 0.3f, vec4(1.0f, 1.0f, 1.0f, 0.3f), 1.0f);
    t.gradient_v(r_pad, col(250, 170, 195), col(235, 130, 165));
    // The face, centred on u = 0.25 of the head square (the sphere's +Z).
    const vec4 brown = col(58, 34, 32);
    for (float x : {-1.0f, 1.0f}) {
        const vec2 eye(0.25f + x * 0.115f, 0.49f);
        t.ellipse(r_head, eye, {0.064f, 0.086f}, col(30, 16, 16), 0.012f);         // a soft rim
        t.ellipse(r_head, eye + vec2(0.0f, 0.008f), {0.056f, 0.076f}, brown, 0.012f); // the big dark eye
        t.ellipse(r_head, eye + vec2(0.0f, 0.03f), {0.035f, 0.035f}, col(80, 48, 44, 0.6f), 0.02f); // a warm lower glow
        t.ellipse(r_head, eye + vec2(-0.018f, -0.03f), {0.018f, 0.024f}, col(255, 255, 255, 0.95f), 0.008f); // the big glint
        t.ellipse(r_head, eye + vec2(0.022f, 0.028f), {0.009f, 0.011f}, col(255, 255, 255, 0.8f), 0.006f);   // a small one
        // Cheek blush, low and outside the eye.
        t.ellipse(r_head, eye + vec2(x * 0.07f, 0.1f), {0.045f, 0.028f}, col(255, 150, 175, 0.45f), 0.03f);
        // Three short whisker dashes under the eye, fanning out.
        for (int i = 0; i < 3; ++i) {
            const float k = float(i) - 1.0f;
            const vec2 a = eye + vec2(x * (0.085f + k * 0.03f), 0.112f + std::abs(k) * 0.02f);
            const vec2 d(x * 0.02f, -0.012f * (0.5f + k));
            t.jagged(r_head, a - d, a + d, 0.011f, brown, 1, 0.0f);
        }
    }
    // A tiny w-shaped mouth under the nose.
    t.mouth(r_head, {0.237f, 0.59f}, 0.03f, 0.018f, 0.011f, brown);
    t.mouth(r_head, {0.263f, 0.59f}, 0.03f, 0.018f, 0.011f, brown);

    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace gk::render
