#include "item_icons.h"

#include "paint.h"

#include "gk/sim/items.h"

#include <cmath>

namespace gk::render {
namespace {

vec4 col(int r, int g, int b, float a = 1.0f) { return vec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
const vec4 kInk = col(25, 20, 30);

// Each painter draws in a cell's UV rect: (0,0) top left .. (1,1) bottom right.
// A shape with a dark outline: the shape a little larger in ink, then the fill.
void blob(Canvas& t, const UvRect& r, vec2 c, vec2 rad, vec4 top, vec4 bottom) {
    t.ellipse(r, c, rad + vec2(0.045f), kInk, 0.01f);
    t.custom(r, [&](float u, float v) {
        const vec2 d = (vec2(u, v) - c) / rad;
        if (glm::dot(d, d) > 1.0f) return vec4(0.0f);
        const float k = std::clamp((v - (c.y - rad.y)) / (2.0f * rad.y), 0.0f, 1.0f);
        return glm::mix(top, bottom, k);
    });
    t.ellipse(r, c + vec2(-0.35f * rad.x, -0.45f * rad.y), rad * vec2(0.32f, 0.2f), vec4(1.0f, 1.0f, 1.0f, 0.55f), 0.03f); // shine
}

void shell(Canvas& t, const UvRect& r, vec2 c, float s, vec4 dome, bool spikes = false) {
    t.ellipse(r, c + vec2(0, 0.14f * s), vec2(0.4f, 0.16f) * s + vec2(0.04f), kInk, 0.01f);
    t.ellipse(r, c + vec2(0, 0.14f * s), vec2(0.4f, 0.16f) * s, col(250, 245, 230), 0.01f); // rim
    blob(t, r, c - vec2(0, 0.04f * s), vec2(0.36f, 0.3f) * s, dome * 1.15f, dome * 0.7f);
    for (int i = -1; i <= 1; ++i) // hex plates
        t.ellipse(r, c + vec2(0.15f * float(i) * s, -0.06f * s), vec2(0.07f, 0.06f) * s, vec4(1.0f, 1.0f, 1.0f, 0.35f), 0.02f);
    if (spikes)
        for (int i = -2; i <= 2; ++i) {
            const vec2 p = c + vec2(0.13f * float(i) * s, -0.3f * s + 0.04f * std::abs(float(i)) * s);
            t.star(r, p, 0.07f * s, col(250, 250, 250));
        }
}

void banana(Canvas& t, const UvRect& r, vec2 c, float s) {
    t.custom(r, [&](float u, float v) {
        const vec2 p = (vec2(u, v) - c) / s;
        const float d = glm::length(p - vec2(0.18f, -0.25f)); // a crescent: a ring arc
        const float ang = std::atan2(p.y + 0.25f, p.x - 0.18f);
        if (ang < 1.2f || ang > 3.6f) return vec4(0.0f);
        if (d > 0.46f && d < 0.66f) return kInk;
        if (d > 0.49f && d < 0.63f) return vec4(0.0f);
        return vec4(0.0f);
    });
    t.custom(r, [&](float u, float v) {
        const vec2 p = (vec2(u, v) - c) / s;
        const float d = glm::length(p - vec2(0.18f, -0.25f));
        const float ang = std::atan2(p.y + 0.25f, p.x - 0.18f);
        if (ang < 1.25f || ang > 3.55f || d < 0.49f || d > 0.63f) return vec4(0.0f);
        return glm::mix(col(255, 236, 90), col(230, 170, 30), (d - 0.49f) / 0.14f);
    });
    t.ellipse(r, c + vec2(-0.42f, -0.18f) * s, vec2(0.05f) * s, col(110, 70, 30), 0.01f);
}

void mushroom(Canvas& t, const UvRect& r, vec2 c, float s, vec4 cap) {
    t.rounded(r, {c.x - 0.17f * s, c.y - 0.02f * s, c.x + 0.17f * s, c.y + 0.36f * s}, 0.08f * s, kInk);
    t.rounded(r, {c.x - 0.13f * s, c.y, c.x + 0.13f * s, c.y + 0.32f * s}, 0.06f * s, col(250, 236, 205));
    t.ellipse(r, c + vec2(-0.05f, 0.15f) * s, vec2(0.025f, 0.05f) * s, kInk, 0.005f); // eyes
    t.ellipse(r, c + vec2(0.05f, 0.15f) * s, vec2(0.025f, 0.05f) * s, kInk, 0.005f);
    t.custom(r, [&](float u, float v) { // dome cap with a flat bottom
        const vec2 p = (vec2(u, v) - c) / s;
        if (p.y > 0.02f) return vec4(0.0f);
        const float d = (p.x * p.x) / (0.42f * 0.42f) + (p.y * p.y) / (0.36f * 0.36f);
        if (d > 1.0f) return vec4(0.0f);
        if (d > 0.82f || p.y > -0.02f) return kInk;
        return glm::mix(cap * 1.1f, cap * 0.75f, std::clamp((p.y + 0.36f) / 0.36f, 0.0f, 1.0f));
    });
    for (vec2 o : {vec2(-0.2f, -0.12f), vec2(0.18f, -0.14f), vec2(0.0f, -0.26f)})
        t.ellipse(r, c + o * s, vec2(0.08f, 0.065f) * s, col(255, 255, 255), 0.01f);
}

void star_icon(Canvas& t, const UvRect& r, vec2 c, float s) {
    t.star(r, c, 0.46f * s, kInk);
    t.star(r, c, 0.4f * s, col(255, 215, 40));
    t.star(r, c - vec2(0.02f, 0.04f) * s, 0.26f * s, col(255, 240, 120));
    t.ellipse(r, c + vec2(-0.07f, -0.02f) * s, vec2(0.025f, 0.06f) * s, kInk, 0.005f);
    t.ellipse(r, c + vec2(0.07f, -0.02f) * s, vec2(0.025f, 0.06f) * s, kInk, 0.005f);
}

void bolt(Canvas& t, const UvRect& r, vec2 c, float s) {
    const vec2 pts[6] = {{0.1f, -0.42f}, {-0.2f, 0.06f}, {0.0f, 0.06f}, {-0.12f, 0.44f}, {0.22f, -0.08f}, {0.02f, -0.08f}};
    t.custom(r, [&](float u, float v) {
        const vec2 p = (vec2(u, v) - c) / s;
        bool in = false;
        for (int i = 0, j = 5; i < 6; j = i++) {
            const vec2 a = pts[i], b = pts[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
        return in ? glm::mix(col(255, 250, 150), col(255, 200, 30), (p.y + 0.42f) / 0.86f) : vec4(0.0f);
    });
}

void bomb(Canvas& t, const UvRect& r, vec2 c, float s) {
    blob(t, r, c + vec2(0, 0.06f) * s, vec2(0.34f) * s, col(70, 70, 85), col(20, 20, 28));
    t.rounded(r, {c.x - 0.08f * s, c.y - 0.36f * s, c.x + 0.08f * s, c.y - 0.24f * s}, 0.02f * s, col(150, 150, 160));
    t.jagged(r, c + vec2(0.0f, -0.36f) * s, c + vec2(0.14f, -0.46f) * s, 0.02f, col(120, 90, 50), 3);
    t.star(r, c + vec2(0.16f, -0.46f) * s, 0.08f * s, col(255, 190, 40));
    t.ellipse(r, c + vec2(-0.1f, 0.0f) * s, vec2(0.04f, 0.07f) * s, col(250, 250, 250), 0.005f);
    t.ellipse(r, c + vec2(0.1f, 0.0f) * s, vec2(0.04f, 0.07f) * s, col(250, 250, 250), 0.005f);
}

void ink(Canvas& t, const UvRect& r, vec2 c, float s) {
    blob(t, r, c - vec2(0, 0.08f) * s, vec2(0.28f, 0.3f) * s, col(250, 250, 250), col(200, 200, 215));
    for (int i = 0; i < 4; ++i) {
        const float x = (-0.18f + 0.12f * float(i)) * s;
        t.rounded(r, {c.x + x - 0.035f * s, c.y + 0.15f * s, c.x + x + 0.035f * s, c.y + 0.42f * s}, 0.03f * s, col(200, 200, 215));
    }
    t.ellipse(r, c + vec2(-0.09f, -0.06f) * s, vec2(0.05f, 0.08f) * s, kInk, 0.005f);
    t.ellipse(r, c + vec2(0.09f, -0.06f) * s, vec2(0.05f, 0.08f) * s, kInk, 0.005f);
}

void coin(Canvas& t, const UvRect& r, vec2 c, float s) {
    blob(t, r, c, vec2(0.3f, 0.38f) * s, col(255, 230, 90), col(220, 150, 20));
    t.rounded(r, {c.x - 0.05f * s, c.y - 0.2f * s, c.x + 0.05f * s, c.y + 0.2f * s}, 0.04f * s, col(200, 130, 10));
}

} // namespace

std::vector<uint8_t> paint_item_icons(int size) {
    Canvas t(size, vec4(0.0f));
    using sim::Item;
    for (int i = 1; i < int(Item::Count); ++i) {
        const int idx = i - 1;
        const float cs = 1.0f / float(kIconGrid);
        const UvRect r{float(idx % kIconGrid) * cs, float(idx / kIconGrid) * cs, float(idx % kIconGrid + 1) * cs, float(idx / kIconGrid + 1) * cs};
        const vec2 c(0.5f, 0.52f);
        switch (Item(i)) {
        case Item::Banana: banana(t, r, c, 0.85f); break;
        case Item::TripleBanana:
            banana(t, r, c + vec2(-0.2f, 0.12f), 0.5f), banana(t, r, c + vec2(0.2f, 0.12f), 0.5f), banana(t, r, c + vec2(0.0f, -0.15f), 0.5f);
            break;
        case Item::GreenShell: shell(t, r, c, 1.0f, col(60, 190, 70)); break;
        case Item::TripleGreen:
            for (vec2 o : {vec2(-0.2f, 0.14f), vec2(0.2f, 0.14f), vec2(0.0f, -0.14f)}) shell(t, r, c + o, 0.55f, col(60, 190, 70));
            break;
        case Item::RedShell: shell(t, r, c, 1.0f, col(225, 50, 45)); break;
        case Item::TripleRed:
            for (vec2 o : {vec2(-0.2f, 0.14f), vec2(0.2f, 0.14f), vec2(0.0f, -0.14f)}) shell(t, r, c + o, 0.55f, col(225, 50, 45));
            break;
        case Item::SpikedShell: shell(t, r, c + vec2(0, 0.05f), 1.0f, col(60, 100, 230), true); break;
        case Item::Bomb: bomb(t, r, c, 1.0f); break;
        case Item::Mushroom: mushroom(t, r, c, 1.0f, col(230, 50, 45)); break;
        case Item::TripleMushroom:
            for (vec2 o : {vec2(-0.2f, 0.14f), vec2(0.2f, 0.14f), vec2(0.0f, -0.12f)}) mushroom(t, r, c + o, 0.55f, col(230, 50, 45));
            break;
        case Item::GoldenMushroom: mushroom(t, r, c, 1.0f, col(250, 195, 40)); break;
        case Item::Bullet:
            blob(t, r, c, vec2(0.38f, 0.26f), col(80, 80, 95), col(25, 25, 35));
            t.ellipse(r, c + vec2(0.12f, -0.02f), vec2(0.05f, 0.08f), col(250, 250, 250), 0.005f);
            t.rounded(r, {c.x - 0.42f, c.y - 0.14f, c.x - 0.3f, c.y + 0.14f}, 0.02f, col(240, 240, 240));
            break;
        case Item::Ink: ink(t, r, c, 1.0f); break;
        case Item::Lightning: bolt(t, r, c, 1.0f); break;
        case Item::Star: star_icon(t, r, c, 1.0f); break;
        case Item::Fireball:
            blob(t, r, c + vec2(0, 0.06f), vec2(0.3f), col(255, 230, 90), col(240, 90, 20));
            t.star(r, c + vec2(0, -0.14f), 0.2f, col(255, 150, 40));
            break;
        case Item::Boomerang: {
            t.custom(r, [&](float u, float v) {
                const vec2 p = vec2(u, v) - c;
                const bool arm1 = std::abs(p.y + 0.0f) < 0.1f && p.x > -0.36f && p.x < 0.1f;
                const bool arm2 = std::abs(p.x - 0.0f) < 0.1f && p.y > -0.36f && p.y < 0.1f;
                const bool edge = (std::abs(p.y) < 0.13f && p.x > -0.39f && p.x < 0.13f) || (std::abs(p.x) < 0.13f && p.y > -0.39f && p.y < 0.13f);
                return arm1 || arm2 ? col(80, 150, 240) : edge ? kInk : vec4(0.0f);
            });
            break;
        }
        case Item::Chomper:
            blob(t, r, c, vec2(0.36f, 0.32f), col(240, 60, 50), col(170, 25, 25));
            t.rounded(r, {c.x - 0.3f, c.y - 0.02f, c.x + 0.3f, c.y + 0.08f}, 0.03f, col(250, 250, 250));
            t.spots(r, 5, 0.025f, col(255, 255, 255), 11);
            break;
        case Item::Horn:
            blob(t, r, c + vec2(0.1f, 0.0f), vec2(0.28f, 0.3f), col(250, 210, 80), col(200, 140, 30));
            t.rounded(r, {c.x - 0.38f, c.y - 0.06f, c.x - 0.05f, c.y + 0.06f}, 0.03f, col(200, 150, 40));
            break;
        case Item::LuckyEight:
            t.ellipse(r, c, vec2(0.44f), kInk, 0.01f);
            t.ellipse(r, c, vec2(0.4f), col(170, 90, 220), 0.01f);
            for (int k = 0; k < 8; ++k) {
                const float a = float(k) / 8.0f * 6.2832f;
                t.ellipse(r, c + vec2(std::cos(a), std::sin(a)) * 0.26f, vec2(0.07f), k % 2 ? col(255, 210, 60) : col(90, 200, 90), 0.01f);
            }
            break;
        case Item::Coin: coin(t, r, c, 1.0f); break;
        case Item::Ghost:
            blob(t, r, c, vec2(0.32f, 0.36f), col(250, 250, 255), col(200, 200, 225));
            t.ellipse(r, c + vec2(-0.1f, -0.05f), vec2(0.05f, 0.08f), kInk, 0.005f);
            t.ellipse(r, c + vec2(0.1f, -0.05f), vec2(0.05f, 0.08f), kInk, 0.005f);
            t.ellipse(r, c + vec2(0.0f, 0.12f), vec2(0.1f, 0.06f), col(230, 70, 90), 0.01f);
            break;
        default: break;
        }
    }
    return t.rgba();
}

} // namespace gk::render
