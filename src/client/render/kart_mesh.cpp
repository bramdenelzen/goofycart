#include "kart_mesh.h"

#include "textures.h"

#include <algorithm>
#include <cmath>

namespace gk::render {
namespace {

constexpr uint32_t kDark = rgba(40, 40, 48);
constexpr uint32_t kSeat = rgba(60, 60, 70);
constexpr uint32_t kChrome = rgba(210, 212, 220);
constexpr uint32_t kGold = rgba(250, 200, 60);
constexpr uint32_t kBone = rgba(240, 230, 200);
constexpr uint32_t kGlove = rgba(245, 245, 245);
constexpr uint32_t kGlass = rgba(150, 200, 240);

uint32_t shade(uint32_t c, float k) {
    auto ch = [&](int s) { return uint8_t(std::clamp(float((c >> s) & 0xff) * k, 0.0f, 255.0f)); };
    return rgba(ch(0), ch(8), ch(16));
}

// A cross-section of a body: a rounded rectangle in the XY plane at z, `round`
// from 0 (box) to 1 (ellipse), as a ring of points for MeshBuilder::loft.
struct Section {
    float z, y_lo, y_hi, half_w;
    float round = 0.5f;
    float x = 0.0f;
};
constexpr int kRing = 24;

std::vector<vec3> ring_of(const Section& s) {
    std::vector<vec3> r;
    r.reserve(kRing);
    const float hh = std::max(0.5f * (s.y_hi - s.y_lo), 0.002f), cy = 0.5f * (s.y_hi + s.y_lo);
    const float hw = std::max(s.half_w, 0.002f);
    const float p = glm::mix(8.0f, 2.0f, std::clamp(s.round, 0.0f, 1.0f)); // superellipse exponent
    for (int i = 0; i < kRing; ++i) {
        const float t = 2.0f * kPi * float(i) / float(kRing);
        const float c = std::cos(t), sn = std::sin(t);
        const float x = std::copysign(std::pow(std::abs(c), 2.0f / p), c) * hw;
        const float y = std::copysign(std::pow(std::abs(sn), 2.0f / p), sn) * hh;
        r.push_back({s.x + x, cy + y, s.z});
    }
    return r;
}

// Skins a list of sections, rear to front, into one smooth body.
void hull(MeshBuilder& b, const std::vector<Section>& sections, uint32_t color) {
    std::vector<std::vector<vec3>> rings;
    for (const Section& s : sections) rings.push_back(ring_of(s));
    b.loft(rings, color, true);
}

// A cone pointing along `dir` from `base`, smooth.
void cone_dir(MeshBuilder& b, vec3 base, vec3 dir, float radius, float height, uint32_t color) {
    b.tube(base, base + glm::normalize(dir) * height, radius, 0.01f, color, 12, true);
}

// A steering wheel: a rim, three spokes and a hub, facing the driver (-Z, tilted).
void steering_wheel(MeshBuilder& b, vec3 centre, float r, uint32_t rim_color) {
    const vec3 axis = glm::normalize(vec3(0, 0.55f, -1.0f));
    b.torus(centre, axis, r, r * 0.16f, rim_color, 20, 8);
    vec3 u, v;
    u = glm::normalize(glm::cross(vec3(0, 1, 0), axis));
    v = glm::cross(axis, u);
    for (int i = 0; i < 3; ++i) {
        const float a = kPi * 0.5f + 2.0f * kPi * float(i) / 3.0f;
        b.capsule(centre, centre + (u * std::cos(a) + v * std::sin(a)) * r * 0.9f, r * 0.08f, kDark, 8, 2);
    }
    b.sphere(centre, r * 0.22f, kDark, 12, 8);
    b.tube(centre - axis * 0.02f, centre - axis * 0.35f, r * 0.08f, r * 0.08f, kChrome, 10, false);
}

struct Wheel {
    float x, z;
    float scale = 1.0f; // front wheels of some bodies are smaller
};

void wheels(MeshBuilder& b, const std::vector<Wheel>& ws, const sim::PartLook& t) {
    const float hw = t.shape == "thin" ? 0.07f : t.shape == "wide" ? 0.19f : t.shape == "chunky" ? 0.18f : 0.13f;
    const uint32_t tread = hex_rgba(t.color), hub = hex_rgba(t.color2);
    const bool chunky = t.shape == "chunky";
    for (const Wheel& w : ws) {
        const float r = 0.24f * t.size * w.scale;
        const vec3 c(w.x, r, w.z);
        // Tyre: a lathe around X with a rounded shoulder and a recessed rim.
        std::vector<vec2> prof;
        const float rim = r * 0.6f;
        for (float side : {-1.0f, 1.0f}) {
            std::vector<vec2> half{{rim, hw * 0.9f}, {r * 0.82f, hw}, {r * 0.95f, hw * 0.85f}, {r, hw * 0.5f}, {r, 0.0f}};
            if (side < 0) {
                for (auto& p : half) p.y = -p.y;
            } else {
                std::reverse(half.begin(), half.end());
            }
            for (const vec2& p : half) prof.push_back(p);
        }
        // Drop the duplicated centre point.
        prof.erase(prof.begin() + 4);
        b.texture(TexTread, 1.0f);
        b.lathe(c, vec3(1, 0, 0), prof, chunky ? 28 : 24, tread, chunky ? 12.0f : 10.0f, 1.0f);
        b.texture(TexWhite);
        if (chunky) // tread blocks around the shoulder
            for (int i = 0; i < 10; ++i) {
                const float a = 2.0f * kPi * float(i) / 10.0f;
                const vec3 o(0, std::sin(a) * r * 0.97f, std::cos(a) * r * 0.97f);
                b.rounded_box(c + o, {hw * 0.9f, 0.035f, 0.05f}, 0.015f, shade(tread, 0.75f), 10, 6);
            }
        // Hub caps: a dish on each visible side, a nut in the middle, spokes.
        auto cap = [&](float side) {
            const vec3 face = c + vec3(side * hw * 0.9f, 0, 0);
            std::vector<vec2> dish{{0.0f, 0.0f}, {rim * 0.55f, hw * 0.14f}, {rim * 0.95f, hw * 0.06f}, {rim, -hw * 0.1f}};
            if (side < 0)
                for (auto& p : dish) p.y = -p.y;
            else
                std::reverse(dish.begin(), dish.end());
            b.lathe(face, vec3(1, 0, 0), dish, 20, hub);
            b.sphere(face + vec3(side * hw * 0.15f, 0, 0), rim * 0.18f, kChrome, 10, 6);
            for (int i = 0; i < 5; ++i) {
                const float a = 2.0f * kPi * float(i) / 5.0f;
                const vec3 o(0, std::sin(a) * rim * 0.75f, std::cos(a) * rim * 0.75f);
                b.sphere(face + o + vec3(side * hw * 0.08f, 0, 0), rim * 0.08f, kDark, 8, 5);
            }
        };
        const float side = w.x > 0.01f ? 1.0f : w.x < -0.01f ? -1.0f : 0.0f;
        if (side != 0.0f) cap(side);
        else {
            cap(-1.0f);
            cap(1.0f);
        }
    }
}

void hat(MeshBuilder& b, vec3 head, float r, const sim::PartLook& d) {
    const uint32_t c = hex_rgba(d.color2), skin = hex_rgba(d.color);
    const std::string& s = d.shape;
    const vec3 up(0, 1, 0);
    if (s == "cap") {
        // Dome sitting on the head and a curved peak out the front.
        std::vector<vec2> dome{{0.0f, r * 0.95f}, {r * 0.6f, r * 0.9f}, {r * 0.95f, r * 0.7f}, {r * 1.08f, r * 0.4f}, {r * 1.1f, r * 0.25f}};
        std::reverse(dome.begin(), dome.end());
        b.lathe(head, up, dome, 20, c);
        std::vector<std::vector<vec3>> peak;
        for (float t : {0.0f, 0.5f, 1.0f}) {
            std::vector<vec3> ring;
            const float z = r * (1.0f + 0.7f * t), y = r * (0.3f - 0.12f * t), w = r * (1.05f - 0.35f * t);
            for (int i = 0; i < kRing; ++i) {
                const float a = 2.0f * kPi * float(i) / float(kRing);
                ring.push_back(head + vec3(std::cos(a) * w, y + std::sin(a) * r * 0.06f, z - std::abs(std::cos(a)) * r * 0.3f));
            }
            peak.push_back(ring);
        }
        b.loft(peak, c, true);
        b.sphere(head + vec3(0, r * 1.0f, 0), r * 0.1f, c, 8, 5);
    } else if (s == "beanie") {
        std::vector<vec2> dome{{r * 1.12f, r * 0.1f}, {r * 1.1f, r * 0.45f}, {r * 1.0f, r * 0.8f}, {r * 0.7f, r * 1.1f}, {0.0f, r * 1.25f}};
        b.lathe(head, up, dome, 20, c);
        b.torus(head + vec3(0, r * 0.25f, 0), up, r * 1.08f, r * 0.14f, shade(c, 0.85f), 20, 8);
        b.sphere(head + vec3(0, r * 1.35f, 0), r * 0.26f, rgba(250, 250, 250), 12, 8);
    } else if (s == "helmet") {
        b.sphere(head + vec3(0, r * 0.1f, -r * 0.05f), vec3(r * 1.18f, r * 1.18f, r * 1.22f), c, 24, 16);
        // Visor: a dark lens over the eyes.
        b.sphere(head + vec3(0, r * 0.08f, r * 0.42f), vec3(r * 0.85f, r * 0.32f, r * 0.82f), rgba(30, 40, 60), 20, 10);
    } else if (s == "ears") {
        for (float x : {-0.6f, 0.6f}) {
            cone_dir(b, head + vec3(x * r, r * 0.6f, 0), vec3(x * 0.6f, 1, 0), r * 0.36f, r * 0.95f, skin);
            cone_dir(b, head + vec3(x * r * 1.02f, r * 0.62f, r * 0.08f), vec3(x * 0.6f, 1, 0.1f), r * 0.22f, r * 0.7f, rgba(240, 170, 170));
        }
    } else if (s == "bow") {
        for (float x : {-1.0f, 1.0f})
            b.sphere(head + vec3(x * r * 0.42f, r * 0.95f, 0), vec3(r * 0.38f, r * 0.24f, r * 0.16f), c, 16, 10);
        b.sphere(head + vec3(0, r * 0.95f, 0), r * 0.14f, shade(c, 0.8f), 12, 8);
    } else if (s == "bandana") {
        b.torus(head + vec3(0, r * 0.45f, 0), up, r * 0.98f, r * 0.17f, c, 20, 8);
        b.sweep({head + vec3(0, r * 0.4f, -r * 1.05f), head + vec3(r * 0.1f, r * 0.15f, -r * 1.3f), head + vec3(r * 0.2f, -r * 0.2f, -r * 1.35f)},
                {r * 0.14f, r * 0.11f, r * 0.04f}, c, 10);
    } else if (s == "tophat") {
        const uint32_t black = rgba(30, 30, 36);
        b.lathe(head + vec3(0, r * 0.7f, 0), up, {{0.0f, 0.0f}, {r * 1.35f, 0.0f}, {r * 1.4f, r * 0.08f}, {r * 1.3f, r * 0.12f}, {0.0f, r * 0.12f}}, 24, black);
        b.lathe(head + vec3(0, r * 0.8f, 0), up, {{0.0f, 0.0f}, {r * 0.78f, 0.0f}, {r * 0.82f, r * 1.1f}, {0.0f, r * 1.12f}}, 20, black);
        b.torus(head + vec3(0, r * 0.95f, 0), up, r * 0.78f, r * 0.08f, c, 20, 6);
    } else if (s == "crown") {
        b.lathe(head + vec3(0, r * 0.7f, 0), up, {{r * 0.75f, 0.0f}, {r * 0.85f, r * 0.3f}, {r * 0.78f, r * 0.32f}, {r * 0.68f, 0.02f}}, 20, kGold);
        for (int i = 0; i < 5; ++i) {
            const float a = 2.0f * kPi * float(i) / 5.0f;
            const vec3 p = head + vec3(std::cos(a) * r * 0.8f, r * 1.0f, std::sin(a) * r * 0.8f);
            cone_dir(b, p, vec3(std::cos(a) * 0.3f, 1, std::sin(a) * 0.3f), r * 0.16f, r * 0.42f, kGold);
            b.sphere(p + vec3(std::cos(a) * 0.12f, r * 0.44f, std::sin(a) * 0.12f) * 1.0f, r * 0.07f, rgba(230, 60, 80), 8, 5);
        }
    } else if (s == "horns") {
        for (float x : {-1.0f, 1.0f}) {
            std::vector<vec3> path;
            std::vector<float> radii;
            for (int i = 0; i <= 5; ++i) {
                const float t = float(i) / 5.0f;
                path.push_back(head + vec3(x * r * (0.7f + 0.55f * t), r * (0.5f + 0.9f * t - 0.5f * t * t), r * (0.1f + 0.5f * t * t)));
                radii.push_back(r * (0.22f - 0.2f * t));
            }
            b.sweep(path, radii, kBone, 10);
        }
    }
}

// The driver, cut at its joints: a rounded torso, arms to the wheel, a big
// round head with eyes. Every piece is built around its own joint so the
// client can swing it; the joint balls keep the seams closed when it does.
DriverRig build_rig(vec3 seat, const sim::PartLook& d, bool bike, vec3 hands_at) {
    const float s = std::clamp(d.size, 0.6f, 1.5f);
    const uint32_t outfit = hex_rgba(d.color2), skin = hex_rgba(d.color);
    const vec3 hips = seat + vec3(0, 0.05f * s, bike ? 0.05f : 0.0f);
    const vec3 shoulder[2] = {hips + vec3(-0.21f * s, 0.36f * s, 0.0f), hips + vec3(0.21f * s, 0.36f * s, 0.0f)};
    const float r = 0.2f * s;
    const vec3 neck = hips + vec3(0, 0.42f * s, 0.0f);

    DriverRig rig;
    rig.offset[DriverRig::Pelvis] = hips;
    rig.offset[DriverRig::Torso] = vec3(0.0f);
    rig.offset[DriverRig::Head] = neck - hips;

    // Legs: thighs forward from the hips to knees, mostly inside the body.
    // They stay with the pelvis, wedged into the kart.
    {
        MeshBuilder b;
        for (float x : {-1.0f, 1.0f}) {
            const vec3 hip = vec3(x * 0.1f * s, 0.02f * s, 0.05f);
            const vec3 knee = hip + vec3(x * 0.03f, 0.12f * s, 0.42f * s);
            b.capsule(hip, knee, 0.075f * s, shade(outfit, 0.8f), 12, 4);
            b.sphere(knee, 0.08f * s, shade(outfit, 0.8f), 12, 8);
        }
        rig.piece[DriverRig::Pelvis] = b.take();
    }
    // Torso as a loft of ellipses, hips to neck, and the joint balls.
    {
        MeshBuilder b;
        std::vector<std::vector<vec3>> torso;
        struct Slice { float y, w, dpt; };
        for (const Slice& sl : {Slice{0.0f, 0.19f, 0.14f}, Slice{0.12f, 0.17f, 0.12f}, Slice{0.26f, 0.21f, 0.15f}, Slice{0.36f, 0.22f, 0.14f}, Slice{0.42f, 0.14f, 0.1f}}) {
            std::vector<vec3> ring;
            for (int i = 0; i < kRing; ++i) {
                const float a = 2.0f * kPi * float(i) / float(kRing);
                ring.push_back(vec3(std::sin(a) * sl.w * s, sl.y * s, std::cos(a) * sl.dpt * s));
            }
            torso.push_back(ring);
        }
        b.loft(torso, outfit, true);
        for (int i = 0; i < 2; ++i) b.sphere(shoulder[i] - hips, 0.075f * s, outfit, 12, 8);
        b.sphere(neck - hips, 0.07f * s, skin, 12, 8); // caps the neck when the head tips right over
        rig.piece[DriverRig::Torso] = b.take();
    }
    // Arms: upper arm forward and down, forearm to the hands on the wheel.
    for (int i = 0; i < 2; ++i) {
        const float x = i == 0 ? -1.0f : 1.0f;
        const vec3 sh = shoulder[i];
        const vec3 hand = hands_at + vec3(x * 0.16f, 0.0f, 0.0f);
        const vec3 elbow = glm::mix(sh, hand, 0.5f) + vec3(x * 0.06f, -0.08f * s, -0.04f);
        MeshBuilder up;
        up.capsule(vec3(0.0f), elbow - sh, 0.055f * s, outfit, 12, 4);
        rig.piece[DriverRig::UpperL + i] = up.take();
        rig.offset[DriverRig::UpperL + i] = sh - hips;
        MeshBuilder fore;
        fore.sphere(vec3(0.0f), 0.052f * s, outfit, 12, 8); // the elbow ball
        fore.capsule(vec3(0.0f), hand - elbow, 0.05f * s, outfit, 12, 4);
        fore.sphere(hand - elbow, 0.07f * s, kGlove, 12, 8);
        rig.piece[DriverRig::ForeL + i] = fore.take();
        rig.offset[DriverRig::ForeL + i] = elbow - sh;
    }
    // Neck and head: eyes (whites, irises, a highlight), a small nose, a thin
    // dark smile and the hat.
    {
        MeshBuilder b;
        const vec3 head = vec3(0, r * 1.05f, 0.03f); // in the neck's space
        b.tube(vec3(0, -0.02f, 0), head, 0.07f * s, 0.07f * s, skin, 12, false);
        b.sphere(head, vec3(r, r * 1.04f, r * 0.98f), skin, 28, 18);
        for (float x : {-0.42f, 0.42f}) {
            const vec3 e = head + vec3(x * r, r * 0.12f, r * 0.88f);
            b.sphere(e, vec3(r * 0.17f, r * 0.24f, r * 0.08f), rgba(250, 250, 250), 14, 10);
            b.sphere(e + vec3(0, -r * 0.02f, r * 0.05f), vec3(r * 0.1f, r * 0.14f, r * 0.05f), rgba(30, 40, 60), 12, 8);
            b.sphere(e + vec3(r * 0.03f, r * 0.05f, r * 0.09f), r * 0.03f, rgba(255, 255, 255), 8, 5);
        }
        b.sphere(head + vec3(0, -r * 0.12f, r * 0.98f), r * 0.14f, shade(skin, 0.95f), 12, 8);
        b.sweep({head + vec3(-r * 0.22f, -r * 0.38f, r * 0.9f), head + vec3(0, -r * 0.46f, r * 0.95f), head + vec3(r * 0.22f, -r * 0.38f, r * 0.9f)},
                r * 0.025f, rgba(90, 40, 40), 6);
        hat(b, head, r, d);
        rig.piece[DriverRig::Head] = b.take();
    }
    return rig;
}

// The welded driver: the rig with every joint at rest.
void driver(MeshBuilder& b, vec3 seat, const sim::PartLook& d, bool bike, vec3 hands_at) {
    const DriverRig rig = build_rig(seat, d, bike, hands_at);
    mat4 world[DriverRig::PieceCount];
    for (int i = 0; i < DriverRig::PieceCount; ++i) {
        const mat4 parent = kDriverParent[i] < 0 ? mat4(1.0f) : world[kDriverParent[i]];
        world[i] = glm::translate(parent, rig.offset[i]);
        b.append(rig.piece[i], world[i]);
    }
}

} // namespace

KartLook kart_look(const sim::PartsCatalog& parts, const sim::Build& b, uint32_t paint) {
    auto pick = [](const std::vector<sim::Part>& v, int i) { return v.empty() ? sim::PartLook{} : v[size_t(std::clamp(i, 0, int(v.size()) - 1))].look; };
    return {paint, pick(parts.drivers, b.driver), pick(parts.bodies, b.body), pick(parts.tires, b.tires), pick(parts.gliders, b.glider)};
}

MeshData make_kart_mesh(const KartLook& l, KartLayout* layout, unsigned parts) {
    // Faces +Z, about 1.6 m long, wheels touching y = 0.
    MeshBuilder b;
    const uint32_t paint = l.paint, trim = hex_rgba(l.body.color2);
    const std::string& shape = l.body.shape;
    const float lift = std::max(0.0f, (l.tires.size - 1.0f) * 0.24f); // bigger tyres raise the body
    vec3 seat(0, 0.5f, -0.2f);
    vec3 wheel(0, 0.85f, 0.35f); // where the hands go
    bool bike = false;
    std::vector<Wheel> ws{{-0.55f, 0.6f}, {0.55f, 0.6f}, {-0.55f, -0.62f}, {0.55f, -0.62f}};
    MeshBuilder body;

    auto headlights = [&](float z, float y, float x, float r) {
        for (float sx : {-1.0f, 1.0f}) b.sphere({sx * x, y + lift, z}, vec3(r, r, r * 0.5f), rgba(255, 250, 220), 12, 8);
    };
    auto exhaust = [&](vec3 at, vec3 dir, float r, float len) {
        body.tube(at, at + glm::normalize(dir) * len, r * 0.8f, r, kChrome, 12, true);
        body.sphere(at + glm::normalize(dir) * len, r * 0.75f, kDark, 10, 6);
    };

    if (shape == "pipe") {
        // A bare frame: a central pod, side rails and a rear engine block.
        hull(body, {{-0.8f, 0.22f, 0.4f, 0.2f, 0.9f}, {-0.4f, 0.18f, 0.42f, 0.3f, 0.8f}, {0.2f, 0.18f, 0.4f, 0.3f, 0.8f}, {0.7f, 0.2f, 0.36f, 0.22f, 0.9f}, {0.95f, 0.24f, 0.3f, 0.1f, 1.0f}}, paint);
        for (float x : {-0.42f, 0.42f}) body.sweep({{x, 0.32f, -0.85f}, {x, 0.32f, 0.6f}, {x * 0.6f, 0.34f, 0.95f}, {x * 0.15f, 0.34f, 1.0f}}, 0.04f, trim, 10);
        body.sweep({{-0.42f, 0.32f, -0.85f}, {0.42f, 0.32f, -0.85f}}, 0.04f, trim, 10);
        body.rounded_box({0, 0.38f, -0.78f}, {0.2f, 0.12f, 0.1f}, 0.04f, kDark);
        exhaust({0.22f, 0.4f, -0.8f}, {0.3f, 0.2f, -1.0f}, 0.04f, 0.25f);
        body.rounded_box({0, 0.52f, -0.42f}, {0.22f, 0.14f, 0.05f}, 0.04f, kSeat);
        seat = {0, 0.42f, -0.2f};
        wheel = {0, 0.72f, 0.4f};
    } else if (shape == "buggy") {
        hull(body, {{-0.9f, 0.3f, 0.6f, 0.4f, 0.4f}, {-0.6f, 0.26f, 0.72f, 0.5f, 0.3f}, {-0.2f, 0.26f, 0.68f, 0.52f, 0.3f}, {0.3f, 0.28f, 0.66f, 0.5f, 0.3f}, {0.7f, 0.3f, 0.7f, 0.46f, 0.35f}, {0.92f, 0.34f, 0.6f, 0.3f, 0.6f}}, paint);
        for (float x : {-0.42f, 0.42f}) {
            body.sweep({{x, 0.62f, 0.3f}, {x * 0.9f, 1.3f, 0.0f}, {x * 0.85f, 1.45f, -0.2f}, {x * 0.9f, 1.3f, -0.45f}, {x, 0.62f, -0.65f}}, 0.035f, trim, 10);
        }
        body.sweep({{-0.36f, 1.45f, -0.2f}, {0.36f, 1.45f, -0.2f}}, 0.035f, trim, 10);
        body.sweep({{-0.5f, 0.42f, 0.98f}, {0.5f, 0.42f, 0.98f}}, 0.04f, trim, 10);
        body.rounded_box({0, 0.75f, -0.42f}, {0.24f, 0.16f, 0.06f}, 0.05f, kSeat);
        headlights(0.93f, 0.6f, 0.26f, 0.07f);
        seat = {0, 0.62f, -0.2f};
        wheel = {0, 1.0f, 0.3f};
        for (Wheel& w : ws) w.x *= 1.1f;
    } else if (shape == "bike" || shape == "scooter" || shape == "chopper") {
        bike = true;
        if (shape == "bike") {
            ws = {{0, 0.62f}, {0, -0.58f}};
            // Tank and tail in one body, a fairing at the front.
            hull(body, {{-0.55f, 0.5f, 0.66f, 0.12f, 0.9f}, {-0.3f, 0.46f, 0.72f, 0.16f, 0.8f}, {0.0f, 0.42f, 0.78f, 0.17f, 0.7f}, {0.3f, 0.44f, 0.8f, 0.15f, 0.8f}, {0.55f, 0.5f, 0.7f, 0.1f, 1.0f}}, paint);
            body.sphere({0, 0.86f, 0.5f}, vec3(0.16f, 0.1f, 0.05f), kGlass, 14, 8);
            for (float x : {-0.08f, 0.08f}) body.sweep({{x, 0.26f, 0.62f}, {x, 0.7f, 0.5f}}, 0.03f, kChrome, 10);
            body.sweep({{-0.32f, 0.8f, 0.4f}, {-0.1f, 0.78f, 0.42f}, {0.1f, 0.78f, 0.42f}, {0.32f, 0.8f, 0.4f}}, 0.022f, trim, 8);
            body.rounded_box({0, 0.74f, -0.2f}, {0.14f, 0.04f, 0.22f}, 0.03f, kSeat);
            exhaust({0.14f, 0.32f, -0.1f}, {0, 0.1f, -1.0f}, 0.04f, 0.6f);
            // Fenders over both wheels.
            for (const Wheel& w : ws) {
                std::vector<vec3> path;
                for (int i = 0; i <= 6; ++i) {
                    const float a = kPi * (0.2f + 0.6f * float(i) / 6.0f);
                    path.push_back({0, 0.27f + std::sin(a) * 0.3f, w.z + std::cos(a) * 0.3f});
                }
                body.sweep(path, 0.05f, shade(paint, 0.9f), 8);
            }
            seat = {0, 0.64f, -0.15f};
            wheel = {0, 0.82f, 0.4f};
        } else if (shape == "scooter") {
            ws = {{0, 0.62f, 0.75f}, {0, -0.55f, 0.75f}};
            hull(body, {{-0.62f, 0.3f, 0.5f, 0.2f, 0.7f}, {-0.35f, 0.26f, 0.56f, 0.22f, 0.6f}, {0.0f, 0.24f, 0.32f, 0.2f, 0.5f}, {0.4f, 0.24f, 0.32f, 0.2f, 0.5f}, {0.6f, 0.26f, 0.5f, 0.16f, 0.8f}}, paint);
            body.sweep({{0, 0.3f, 0.6f}, {0, 0.95f, 0.5f}}, 0.04f, trim, 10);
            body.sphere({0, 0.7f, 0.66f}, vec3(0.16f, 0.22f, 0.08f), paint, 14, 8);
            body.sweep({{-0.3f, 0.95f, 0.48f}, {-0.1f, 0.96f, 0.5f}, {0.1f, 0.96f, 0.5f}, {0.3f, 0.95f, 0.48f}}, 0.022f, trim, 8);
            body.rounded_box({0, 0.6f, -0.35f}, {0.17f, 0.05f, 0.2f}, 0.04f, kSeat);
            seat = {0, 0.57f, -0.35f};
            wheel = {0, 0.97f, 0.45f};
        } else {
            ws = {{0, 0.95f, 0.9f}, {0, -0.6f, 1.15f}};
            hull(body, {{-0.55f, 0.36f, 0.56f, 0.14f, 0.8f}, {-0.25f, 0.34f, 0.62f, 0.17f, 0.7f}, {0.05f, 0.44f, 0.74f, 0.19f, 0.7f}, {0.3f, 0.48f, 0.72f, 0.16f, 0.8f}, {0.45f, 0.5f, 0.6f, 0.1f, 1.0f}}, paint);
            for (float x : {-0.08f, 0.08f}) body.sweep({{x, 0.24f, 0.95f}, {x, 0.95f, 0.45f}}, 0.03f, kChrome, 10);
            body.sweep({{-0.42f, 1.0f, 0.4f}, {-0.2f, 0.96f, 0.46f}, {0.2f, 0.96f, 0.46f}, {0.42f, 1.0f, 0.4f}}, 0.024f, trim, 8);
            exhaust({0.18f, 0.3f, -0.1f}, {0.05f, 0.05f, -1.0f}, 0.045f, 0.55f);
            exhaust({-0.18f, 0.3f, -0.1f}, {-0.05f, 0.05f, -1.0f}, 0.045f, 0.55f);
            body.rounded_box({0, 0.62f, -0.32f}, {0.15f, 0.04f, 0.2f}, 0.03f, kSeat);
            body.sphere({0, 0.72f, 0.52f}, vec3(0.1f, 0.1f, 0.06f), rgba(255, 250, 220), 12, 8);
            seat = {0, 0.52f, -0.3f};
            wheel = {0, 0.98f, 0.42f};
        }
    } else if (shape == "racer") {
        ws = {{-0.55f, 0.75f}, {0.55f, 0.75f}, {-0.58f, -0.75f, 1.1f}, {0.58f, -0.75f, 1.1f}};
        // Narrow nose cone, cockpit, side pods, wings.
        hull(body, {{-0.95f, 0.26f, 0.5f, 0.2f, 0.7f}, {-0.6f, 0.22f, 0.62f, 0.26f, 0.6f}, {-0.25f, 0.2f, 0.56f, 0.3f, 0.6f}, {0.2f, 0.2f, 0.46f, 0.26f, 0.7f}, {0.8f, 0.22f, 0.38f, 0.16f, 0.9f}, {1.35f, 0.26f, 0.32f, 0.06f, 1.0f}}, paint);
        for (float x : {-1.0f, 1.0f})
            hull(body, {{-0.7f, 0.2f, 0.42f, 0.16f, 0.6f, x * 0.42f}, {-0.3f, 0.2f, 0.44f, 0.2f, 0.5f, x * 0.44f}, {0.3f, 0.2f, 0.4f, 0.16f, 0.6f, x * 0.4f}, {0.55f, 0.22f, 0.3f, 0.08f, 1.0f, x * 0.3f}}, paint);
        body.rounded_box({0, 0.74f, -0.98f}, {0.6f, 0.03f, 0.15f}, 0.025f, trim);
        for (float x : {-0.3f, 0.3f}) body.rounded_box({x, 0.62f, -0.95f}, {0.02f, 0.12f, 0.1f}, 0.015f, kDark);
        body.rounded_box({0, 0.3f, 1.05f}, {0.65f, 0.025f, 0.12f}, 0.02f, trim);
        for (float x : {-0.62f, 0.62f}) body.rounded_box({x, 0.36f, 1.05f}, {0.02f, 0.07f, 0.12f}, 0.015f, trim);
        body.sphere({0, 0.6f, -0.45f}, vec3(0.2f, 0.12f, 0.12f), shade(paint, 0.85f), 14, 8); // airbox behind the driver
        exhaust({0.12f, 0.36f, -1.0f}, {0, 0.1f, -1.0f}, 0.035f, 0.18f);
        exhaust({-0.12f, 0.36f, -1.0f}, {0, 0.1f, -1.0f}, 0.035f, 0.18f);
        seat = {0, 0.36f, -0.25f};
        wheel = {0, 0.66f, 0.3f};
    } else if (shape == "tub") {
        hull(body, {{-0.72f, 0.26f, 0.66f, 0.42f, 0.9f}, {-0.45f, 0.24f, 0.7f, 0.5f, 0.7f}, {0.0f, 0.24f, 0.7f, 0.52f, 0.7f}, {0.45f, 0.24f, 0.7f, 0.5f, 0.7f}, {0.72f, 0.26f, 0.66f, 0.42f, 0.9f}}, paint);
        // Rolled rim.
        std::vector<vec3> rim;
        for (int i = 0; i <= 32; ++i) {
            const float a = 2.0f * kPi * float(i) / 32.0f;
            rim.push_back({std::cos(a) * 0.5f, 0.7f, std::sin(a) * 0.72f});
        }
        body.sweep(rim, 0.045f, trim, 10, false);
        // Tap and feet.
        body.sweep({{0, 0.72f, -0.6f}, {0, 0.95f, -0.62f}, {0, 0.98f, -0.45f}, {0, 0.9f, -0.4f}}, 0.035f, kChrome, 10);
        for (float x : {-0.4f, 0.4f})
            for (float z : {-0.5f, 0.5f}) body.sphere({x, 0.22f, z}, vec3(0.09f, 0.06f, 0.09f), trim, 12, 8);
        seat = {0, 0.55f, -0.15f};
        wheel = {0, 0.82f, 0.38f};
    } else if (shape == "trike") {
        ws = {{0, 0.75f}, {-0.58f, -0.55f}, {0.58f, -0.55f}};
        hull(body, {{-0.75f, 0.28f, 0.54f, 0.36f, 0.6f}, {-0.45f, 0.26f, 0.6f, 0.42f, 0.5f}, {-0.1f, 0.26f, 0.56f, 0.4f, 0.5f}, {0.3f, 0.28f, 0.5f, 0.26f, 0.7f}, {0.65f, 0.3f, 0.46f, 0.1f, 1.0f}}, paint);
        body.sweep({{0, 0.24f, 0.75f}, {0, 0.8f, 0.45f}}, 0.035f, trim, 10);
        body.sweep({{-0.32f, 0.82f, 0.42f}, {-0.12f, 0.8f, 0.45f}, {0.12f, 0.8f, 0.45f}, {0.32f, 0.82f, 0.42f}}, 0.022f, trim, 8);
        body.rounded_box({0, 0.68f, -0.4f}, {0.22f, 0.12f, 0.06f}, 0.04f, kSeat);
        headlights(0.6f, 0.44f, 0.0f, 0.07f);
        seat = {0, 0.55f, -0.3f};
        wheel = {0, 0.82f, 0.4f};
    } else if (shape == "truck") {
        ws = {{-0.6f, 0.65f}, {0.6f, 0.65f}, {-0.6f, -0.6f}, {0.6f, -0.6f}};
        // Chassis and bonnet in one hull, a cab box, a bed with sides.
        hull(body, {{-0.92f, 0.3f, 0.6f, 0.52f, 0.3f}, {-0.3f, 0.3f, 0.6f, 0.55f, 0.3f}, {0.15f, 0.3f, 0.62f, 0.55f, 0.3f}, {0.35f, 0.3f, 0.86f, 0.52f, 0.3f}, {0.8f, 0.32f, 0.84f, 0.5f, 0.35f}, {0.92f, 0.36f, 0.76f, 0.46f, 0.5f}}, paint);
        body.rounded_box({0, 0.86f, 0.85f}, {0.5f, 0.1f, 0.05f}, 0.03f, kChrome); // grille
        headlights(0.93f, 0.7f, 0.36f, 0.08f);
        body.rounded_box({0, 0.74f, -0.74f}, {0.55f, 0.14f, 0.05f}, 0.04f, trim);
        for (float x : {-0.52f, 0.52f}) body.rounded_box({x, 0.74f, -0.3f}, {0.04f, 0.14f, 0.45f}, 0.03f, trim);
        body.rounded_box({0, 0.72f, -0.15f}, {0.26f, 0.12f, 0.06f}, 0.04f, kSeat);
        exhaust({0.58f, 0.5f, -0.2f}, {0, 1, -0.1f}, 0.05f, 0.7f);
        seat = {0, 0.6f, -0.1f};
        wheel = {0, 0.98f, 0.42f};
    } else { // the standard kart
        hull(body, {{-0.9f, 0.28f, 0.46f, 0.3f, 0.6f}, {-0.7f, 0.22f, 0.62f, 0.46f, 0.45f}, {-0.3f, 0.2f, 0.54f, 0.48f, 0.4f}, {0.2f, 0.2f, 0.5f, 0.46f, 0.4f}, {0.6f, 0.22f, 0.44f, 0.4f, 0.5f}, {0.95f, 0.26f, 0.38f, 0.3f, 0.7f}, {1.1f, 0.3f, 0.34f, 0.16f, 1.0f}}, paint);
        body.rounded_box({0, 0.64f, -0.48f}, {0.3f, 0.16f, 0.08f}, 0.05f, kSeat);  // seat back
        body.rounded_box({0, 0.6f, -0.82f}, {0.5f, 0.04f, 0.12f}, 0.03f, trim);    // rear bumper
        body.rounded_box({0, 0.3f, 1.08f}, {0.36f, 0.05f, 0.08f}, 0.03f, trim);    // front bumper
        headlights(1.1f, 0.4f, 0.14f, 0.05f);
        exhaust({0.2f, 0.3f, -0.9f}, {0.1f, 0.15f, -1.0f}, 0.045f, 0.22f);
        exhaust({-0.2f, 0.3f, -0.9f}, {-0.1f, 0.15f, -1.0f}, 0.045f, 0.22f);
        body.rounded_box({0, 0.52f, -0.85f}, {0.16f, 0.1f, 0.08f}, 0.03f, kDark); // engine
        seat = {0, 0.4f, -0.25f};
        wheel = {0, 0.78f, 0.35f};
    }
    if (layout) {
        layout->wheels.clear();
        for (const Wheel& w : ws) {
            const float r = 0.24f * l.tires.size * w.scale;
            layout->wheels.push_back({vec3(w.x, r, w.z), r});
        }
        layout->seat = seat + vec3(0, lift, 0);
        layout->hands = wheel + vec3(0, lift, 0);
        layout->bike = bike;
        layout->lift = lift;
    }
    if (!(parts & KartBody)) body = {};
    // The steering wheel belongs to the kart, not the driver: a bike has
    // handlebars in its shape instead.
    if ((parts & KartBody) && !bike) steering_wheel(body, wheel, 0.17f, trim);
    // A bike's hands go on the handlebar ends, everyone else's just behind the wheel.
    if (parts & KartDriver) driver(body, seat, l.driver, bike, bike ? wheel : wheel + vec3(0, 0.02f, -0.03f));
    b.append(body.data(), glm::translate(mat4(1.0f), vec3(0, lift, 0)));
    if (parts & KartWheels) wheels(b, ws, l.tires);
    return b.take();
}

DriverRig make_driver_rig(const KartLook& l, const KartLayout& layout) {
    // The seat and hands `make_kart_mesh` gave the welded driver: the layout
    // already carries the tyre lift, and a bike's hands sit on the handlebar
    // ends rather than just behind a steering wheel.
    const vec3 hands = layout.bike ? layout.hands : layout.hands + vec3(0, 0.02f, -0.03f);
    return build_rig(layout.seat, l.driver, layout.bike, hands);
}

MeshData make_glider_mesh(const sim::PartLook& g) {
    MeshBuilder b;
    const uint32_t c = hex_rgba(g.color), c2 = hex_rgba(g.color2), frame = rgba(60, 60, 70);
    auto two_sided = [&](vec3 p0, vec3 p1, vec3 p2, vec3 p3, uint32_t col) {
        b.quad(p0, p1, p2, p3, col);
        b.quad(p3, p2, p1, p0, col);
    };
    if (g.shape == "parafoil") {
        // Arched canopy in five panels on lines.
        const int n = 5;
        for (int i = 0; i < n; ++i) {
            const float a0 = kPi * (0.15f + 0.7f * float(i) / n), a1 = kPi * (0.15f + 0.7f * float(i + 1) / n);
            const vec3 p0(std::cos(a0) * 1.5f, 1.3f + std::sin(a0) * 0.9f, 0), p1(std::cos(a1) * 1.5f, 1.3f + std::sin(a1) * 0.9f, 0);
            two_sided(p0 + vec3(0, 0, -0.5f), p1 + vec3(0, 0, -0.5f), p1 + vec3(0, 0, 0.5f), p0 + vec3(0, 0, 0.5f), i % 2 ? c2 : c);
        }
        for (float x : {-1.0f, 1.0f}) b.capsule({x * 0.3f, 1.0f, 0}, {x * 1.3f, 1.75f, 0}, 0.012f, frame, 6, 2);
    } else if (g.shape == "kite") {
        two_sided({0, 1.7f, 0.9f}, {-1.1f, 1.8f, 0.0f}, {0, 1.9f, -0.9f}, {1.1f, 1.8f, 0.0f}, c);
        b.capsule({0, 1.72f, 0.9f}, {0, 1.88f, -0.9f}, 0.02f, c2, 8, 2);
        b.capsule({-1.1f, 1.8f, 0.0f}, {1.1f, 1.8f, 0.0f}, 0.02f, c2, 8, 2);
        for (float x : {-0.3f, 0.3f}) b.capsule({x, 1.0f, -0.1f}, {x, 1.8f, 0.0f}, 0.012f, frame, 6, 2);
    } else if (g.shape == "umbrella") {
        std::vector<vec2> canopy;
        for (int i = 0; i <= 6; ++i) {
            const float t = float(i) / 6.0f;
            canopy.push_back({1.3f * (1.0f - t), 1.75f + 0.5f * std::sin(t * kPi * 0.5f)});
        }
        b.lathe({0, 0, 0}, {0, 1, 0}, canopy, 24, c);
        b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 1.73f}, {1.3f, 1.74f}}, 24, c2); // underside
        b.capsule({0, 0.9f, -0.15f}, {0, 2.3f, -0.15f}, 0.025f, frame, 8, 2);
        b.sphere({0, 2.3f, -0.15f}, 0.05f, c2, 10, 6);
    } else if (g.shape == "leaf") {
        const vec3 tip(0, 1.85f, 1.1f), tail(0, 1.8f, -1.0f);
        two_sided(tip, {-1.0f, 1.75f, 0.2f}, {-0.8f, 1.72f, -0.5f}, tail, c);
        two_sided(tip, tail, {0.8f, 1.72f, -0.5f}, {1.0f, 1.75f, 0.2f}, c);
        b.capsule(tail + vec3(0, 0.02f, -0.2f), tip + vec3(0, 0.02f, 0), 0.025f, c2, 8, 2);
        for (float x : {-0.35f, 0.35f}) b.capsule({x, 1.05f, 0}, {x, 1.75f, 0}, 0.025f, c2, 8, 2);
    } else { // wing
        two_sided({-1.4f, 1.75f, -0.4f}, {1.4f, 1.75f, -0.4f}, {0.5f, 1.85f, 0.5f}, {-0.5f, 1.85f, 0.5f}, c);
        for (float x : {-0.35f, 0.35f}) b.capsule({x, 1.05f, 0.0f}, {x, 1.75f, 0.0f}, 0.03f, c2, 8, 2);
    }
    return b.take();
}

} // namespace gk::render
