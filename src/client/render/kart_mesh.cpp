#include "kart_mesh.h"

#include <algorithm>
#include <cmath>

namespace gk::render {
namespace {

constexpr uint32_t kDark = rgba(40, 40, 48);
constexpr uint32_t kSeat = rgba(60, 60, 70);
constexpr uint32_t kGold = rgba(250, 200, 60);
constexpr uint32_t kBone = rgba(240, 230, 200);

uint32_t shade(uint32_t c, float k) {
    auto ch = [&](int s) { return uint8_t(std::clamp(float((c >> s) & 0xff) * k, 0.0f, 255.0f)); };
    return rgba(ch(0), ch(8), ch(16));
}

// A cone pointing along `dir` from `base`.
void cone_dir(MeshBuilder& b, vec3 base, vec3 dir, float radius, float height, int sides, uint32_t color) {
    MeshBuilder c;
    c.cone({0, 0, 0}, radius, height, sides, color);
    const vec3 up(0, 1, 0), d = glm::normalize(dir);
    mat4 m = glm::translate(mat4(1.0f), base);
    const vec3 axis = glm::cross(up, d);
    if (glm::length(axis) > 1e-4f) m = glm::rotate(m, std::acos(std::clamp(glm::dot(up, d), -1.0f, 1.0f)), glm::normalize(axis));
    b.append(c.data(), m);
}

// A thin box between two points, for frames, forks and roll cages.
void bar(MeshBuilder& b, vec3 a, vec3 c, float r, uint32_t color) {
    MeshBuilder t;
    const float len = glm::length(c - a);
    t.box({0, len * 0.5f, 0}, {r, len * 0.5f, r}, color);
    const vec3 up(0, 1, 0), d = (c - a) / std::max(len, 1e-4f);
    mat4 m = glm::translate(mat4(1.0f), a);
    const vec3 axis = glm::cross(up, d);
    if (glm::length(axis) > 1e-4f) m = glm::rotate(m, std::acos(std::clamp(glm::dot(up, d), -1.0f, 1.0f)), glm::normalize(axis));
    else if (d.y < 0) m = glm::rotate(m, kPi, vec3(1, 0, 0));
    b.append(t.data(), m);
}

struct Wheel {
    float x, z;
    float scale = 1.0f; // front wheels of some bodies are smaller
};

void wheels(MeshBuilder& b, const std::vector<Wheel>& ws, const sim::PartLook& t) {
    const float hw = t.shape == "thin" ? 0.06f : t.shape == "wide" ? 0.17f : t.shape == "chunky" ? 0.16f : 0.12f;
    const int sides = t.shape == "chunky" ? 8 : 6;
    const uint32_t tread = hex_rgba(t.color), hub = hex_rgba(t.color2);
    for (const Wheel& w : ws) {
        const float r = 0.24f * t.size * w.scale;
        const vec3 c(w.x, r, w.z);
        b.cylinder_x(c, r, hw, sides, tread);
        const float side = w.x > 0.01f ? 1.0f : w.x < -0.01f ? -1.0f : 0.0f;
        if (side != 0.0f) b.box(c + vec3(side * (hw + 0.005f), 0, 0), {0.01f, r * 0.35f, r * 0.35f}, hub);
        else for (float s : {-1.0f, 1.0f}) b.box(c + vec3(s * (hw + 0.005f), 0, 0), {0.01f, r * 0.35f, r * 0.35f}, hub);
        if (t.shape == "chunky") // tread blocks
            for (int i = 0; i < 4; ++i) {
                const float a = float(i) * kPi * 0.5f + 0.4f;
                b.box(c + vec3(0, std::sin(a) * r, std::cos(a) * r), {hw + 0.01f, 0.04f, 0.04f}, shade(tread, 0.8f));
            }
    }
}

void hat(MeshBuilder& b, vec3 head, float r, const sim::PartLook& d) {
    const uint32_t c = hex_rgba(d.color2), skin = hex_rgba(d.color);
    const std::string& s = d.shape;
    if (s == "cap") {
        b.box(head + vec3(0, r * 0.75f, 0), {r * 1.05f, r * 0.3f, r * 1.05f}, c);
        b.box(head + vec3(0, r * 0.55f, r * 1.05f), {r * 1.05f, r * 0.08f, r * 0.4f}, c);
    } else if (s == "beanie") {
        b.box(head + vec3(0, r * 0.7f, 0), {r * 1.05f, r * 0.42f, r * 1.05f}, c);
        b.icosphere(head + vec3(0, r * 1.25f, 0), r * 0.25f, rgba(250, 250, 250));
    } else if (s == "helmet") {
        b.icosphere(head + vec3(0, r * 0.1f, -r * 0.05f), r * 1.18f, c);
        b.box(head + vec3(0, r * 0.05f, r * 1.0f), {r * 0.8f, r * 0.3f, r * 0.15f}, rgba(30, 40, 60));
    } else if (s == "ears") {
        for (float x : {-0.6f, 0.6f}) cone_dir(b, head + vec3(x * r, r * 0.6f, 0), vec3(x * 0.6f, 1, 0), r * 0.35f, r * 0.9f, 4, skin);
    } else if (s == "bow") {
        b.box(head + vec3(-r * 0.35f, r * 0.95f, 0), {r * 0.32f, r * 0.2f, r * 0.12f}, c);
        b.box(head + vec3(r * 0.35f, r * 0.95f, 0), {r * 0.32f, r * 0.2f, r * 0.12f}, c);
        b.box(head + vec3(0, r * 0.95f, 0), {r * 0.1f, r * 0.12f, r * 0.12f}, shade(c, 0.8f));
    } else if (s == "bandana") {
        b.box(head + vec3(0, r * 0.45f, 0), {r * 1.06f, r * 0.18f, r * 1.06f}, c);
        b.box(head + vec3(0, r * 0.35f, -r * 1.15f), {r * 0.15f, r * 0.25f, r * 0.12f}, c);
    } else if (s == "tophat") {
        b.box(head + vec3(0, r * 0.75f, 0), {r * 1.3f, r * 0.06f, r * 1.3f}, rgba(30, 30, 36));
        b.box(head + vec3(0, r * 1.3f, 0), {r * 0.75f, r * 0.55f, r * 0.75f}, rgba(30, 30, 36));
        b.box(head + vec3(0, r * 0.9f, 0), {r * 0.77f, r * 0.1f, r * 0.77f}, c);
    } else if (s == "crown") {
        b.box(head + vec3(0, r * 0.85f, 0), {r * 0.8f, r * 0.15f, r * 0.8f}, kGold);
        for (float x : {-0.6f, 0.6f})
            for (float z : {-0.6f, 0.6f}) b.cone(head + vec3(x * r, r, z * r), r * 0.18f, r * 0.4f, 4, kGold);
    } else if (s == "horns") {
        for (float x : {-1.0f, 1.0f}) cone_dir(b, head + vec3(x * r * 0.7f, r * 0.5f, 0), vec3(x, 0.8f, 0.2f), r * 0.22f, r * 0.9f, 5, kBone);
    }
}

void driver(MeshBuilder& b, vec3 seat, const sim::PartLook& d, bool bike) {
    const float s = std::clamp(d.size, 0.6f, 1.5f);
    const uint32_t outfit = hex_rgba(d.color2), skin = hex_rgba(d.color);
    const vec3 torso = seat + vec3(0, 0.17f * s, bike ? 0.05f : 0.0f);
    b.box(torso, vec3(0.2f, 0.18f, 0.16f) * s, outfit);
    for (float x : {-1.0f, 1.0f}) bar(b, torso + vec3(x * 0.17f * s, 0.08f * s, 0.05f), seat + vec3(x * 0.2f, 0.12f, 0.55f), 0.045f * s, outfit);
    const float r = 0.2f * s;
    const vec3 head = torso + vec3(0, 0.18f * s + r * 0.95f, 0.05f);
    b.icosphere(head, r, skin);
    for (float x : {-0.4f, 0.4f}) b.box(head + vec3(x * r, r * 0.15f, r * 0.92f), {r * 0.1f, r * 0.15f, r * 0.05f}, rgba(20, 20, 30));
    hat(b, head, r, d);
}

} // namespace

KartLook kart_look(const sim::PartsCatalog& parts, const sim::Build& b, uint32_t paint) {
    auto pick = [](const std::vector<sim::Part>& v, int i) { return v.empty() ? sim::PartLook{} : v[size_t(std::clamp(i, 0, int(v.size()) - 1))].look; };
    return {paint, pick(parts.drivers, b.driver), pick(parts.bodies, b.body), pick(parts.tires, b.tires), pick(parts.gliders, b.glider)};
}

MeshData make_kart_mesh(const KartLook& l) {
    // Faces +Z, about 1.6 m long, wheels touching y = 0.
    MeshBuilder b;
    const uint32_t paint = l.paint, trim = hex_rgba(l.body.color2);
    const std::string& shape = l.body.shape;
    const float lift = std::max(0.0f, (l.tires.size - 1.0f) * 0.24f); // bigger tyres raise the body
    vec3 seat(0, 0.5f, -0.2f);
    bool bike = false;
    std::vector<Wheel> ws{{-0.55f, 0.6f}, {0.55f, 0.6f}, {-0.55f, -0.62f}, {0.55f, -0.62f}};
    MeshBuilder body;

    if (shape == "pipe") {
        for (float x : {-0.4f, 0.4f}) body.box({x, 0.32f, 0}, {0.04f, 0.04f, 0.8f}, trim);
        body.box({0, 0.32f, 0.8f}, {0.44f, 0.04f, 0.04f}, trim);
        body.box({0, 0.3f, 0.1f}, {0.32f, 0.03f, 0.55f}, paint);
        body.box({0, 0.3f, 0.75f}, {0.25f, 0.06f, 0.1f}, paint);
        body.box({0, 0.6f, -0.45f}, {0.25f, 0.15f, 0.06f}, kSeat);
        body.box({0, 0.3f, -0.8f}, {0.18f, 0.1f, 0.1f}, kDark);
        seat = {0, 0.42f, -0.2f};
    } else if (shape == "buggy") {
        body.box({0, 0.48f, -0.05f}, {0.48f, 0.16f, 0.8f}, paint);
        body.box({0, 0.62f, 0.65f}, {0.4f, 0.06f, 0.2f}, shade(paint, 0.8f));
        for (float x : {-0.42f, 0.42f}) {
            bar(body, {x, 0.62f, 0.25f}, {x * 0.85f, 1.45f, -0.05f}, 0.035f, trim);
            bar(body, {x, 0.62f, -0.6f}, {x * 0.85f, 1.45f, -0.35f}, 0.035f, trim);
            bar(body, {x * 0.85f, 1.45f, -0.05f}, {x * 0.85f, 1.45f, -0.35f}, 0.035f, trim);
        }
        bar(body, {-0.36f, 1.45f, -0.35f}, {0.36f, 1.45f, -0.35f}, 0.035f, trim);
        seat = {0, 0.62f, -0.2f};
        for (Wheel& w : ws) w.x *= 1.1f;
    } else if (shape == "bike" || shape == "scooter" || shape == "chopper") {
        bike = true;
        if (shape == "bike") {
            ws = {{0, 0.62f}, {0, -0.58f}};
            body.box({0, 0.52f, 0.0f}, {0.16f, 0.16f, 0.5f}, paint);
            body.quad({-0.16f, 0.68f, 0.5f}, {0.16f, 0.68f, 0.5f}, {0.1f, 0.5f, 0.9f}, {-0.1f, 0.5f, 0.9f}, paint);
            body.box({0, 0.82f, 0.45f}, {0.12f, 0.1f, 0.03f}, rgba(140, 200, 240));
            body.box({0, 0.78f, 0.38f}, {0.32f, 0.02f, 0.02f}, trim);
            seat = {0, 0.62f, -0.15f};
        } else if (shape == "scooter") {
            ws = {{0, 0.62f, 0.75f}, {0, -0.55f, 0.75f}};
            body.box({0, 0.3f, 0.0f}, {0.2f, 0.05f, 0.55f}, paint);
            body.box({0, 0.45f, -0.4f}, {0.22f, 0.15f, 0.2f}, paint);
            bar(body, {0, 0.3f, 0.6f}, {0, 0.95f, 0.5f}, 0.04f, trim);
            body.box({0, 0.95f, 0.5f}, {0.3f, 0.02f, 0.02f}, trim);
            seat = {0, 0.55f, -0.35f};
        } else {
            ws = {{0, 0.95f, 0.9f}, {0, -0.6f, 1.15f}};
            body.box({0, 0.45f, -0.25f}, {0.16f, 0.12f, 0.4f}, paint);
            body.box({0, 0.6f, 0.15f}, {0.18f, 0.12f, 0.2f}, paint); // tank
            for (float x : {-0.08f, 0.08f}) bar(body, {x, 0.24f, 0.95f}, {x, 0.95f, 0.45f}, 0.03f, trim);
            body.box({0, 0.98f, 0.45f}, {0.4f, 0.02f, 0.02f}, trim);
            body.box({0.18f, 0.3f, -0.5f}, {0.04f, 0.04f, 0.35f}, rgba(200, 200, 210)); // exhaust
            seat = {0, 0.5f, -0.3f};
        }
    } else if (shape == "racer") {
        ws = {{-0.55f, 0.75f}, {0.55f, 0.75f}, {-0.58f, -0.75f, 1.1f}, {0.58f, -0.75f, 1.1f}};
        body.box({0, 0.3f, -0.05f}, {0.38f, 0.09f, 0.95f}, paint);
        body.quad({-0.2f, 0.3f, 1.35f}, {0.2f, 0.3f, 1.35f}, {0.38f, 0.39f, 0.9f}, {-0.38f, 0.39f, 0.9f}, paint);
        body.tri({0.38f, 0.21f, 0.9f}, {0.38f, 0.39f, 0.9f}, {0.2f, 0.3f, 1.35f}, paint);
        body.tri({-0.38f, 0.21f, 0.9f}, {-0.2f, 0.3f, 1.35f}, {-0.38f, 0.39f, 0.9f}, paint);
        for (float x : {-0.25f, 0.25f}) body.box({x, 0.55f, -0.95f}, {0.02f, 0.16f, 0.04f}, kDark);
        body.box({0, 0.72f, -0.98f}, {0.6f, 0.03f, 0.15f}, trim);
        body.box({0, 0.32f, 0.95f}, {0.6f, 0.02f, 0.1f}, trim);
        seat = {0, 0.38f, -0.25f};
    } else if (shape == "tub") {
        body.box({0, 0.45f, 0}, {0.5f, 0.22f, 0.7f}, paint);
        for (float x : {-0.5f, 0.5f}) body.box({x, 0.69f, 0}, {0.04f, 0.03f, 0.72f}, trim);
        for (float z : {-0.7f, 0.7f}) body.box({0, 0.69f, z}, {0.52f, 0.03f, 0.04f}, trim);
        body.box({0, 0.4f, -0.78f}, {0.12f, 0.12f, 0.06f}, kDark);
        seat = {0, 0.55f, -0.15f};
    } else if (shape == "trike") {
        ws = {{0, 0.75f}, {-0.58f, -0.55f}, {0.58f, -0.55f}};
        body.box({0, 0.4f, -0.35f}, {0.42f, 0.12f, 0.35f}, paint);
        body.quad({-0.42f, 0.52f, 0.0f}, {0.42f, 0.52f, 0.0f}, {0.08f, 0.45f, 0.65f}, {-0.08f, 0.45f, 0.65f}, paint);
        body.quad({-0.08f, 0.32f, 0.65f}, {0.08f, 0.32f, 0.65f}, {0.42f, 0.28f, 0.0f}, {-0.42f, 0.28f, 0.0f}, shade(paint, 0.7f));
        bar(body, {0, 0.24f, 0.75f}, {0, 0.8f, 0.45f}, 0.03f, trim);
        body.box({0, 0.8f, 0.45f}, {0.32f, 0.02f, 0.02f}, trim);
        seat = {0, 0.55f, -0.3f};
    } else if (shape == "truck") {
        ws = {{-0.6f, 0.65f}, {0.6f, 0.65f}, {-0.6f, -0.6f}, {0.6f, -0.6f}};
        body.box({0, 0.45f, 0}, {0.55f, 0.15f, 0.9f}, paint);
        body.box({0, 0.72f, 0.55f}, {0.5f, 0.15f, 0.3f}, paint); // bonnet
        body.box({0, 0.62f, 0.86f}, {0.5f, 0.12f, 0.03f}, rgba(200, 200, 210)); // grille
        body.box({0, 0.75f, -0.75f}, {0.55f, 0.15f, 0.04f}, trim);
        for (float x : {-0.53f, 0.53f}) body.box({x, 0.75f, -0.3f}, {0.03f, 0.15f, 0.45f}, trim);
        seat = {0, 0.6f, -0.1f};
    } else { // the standard kart
        body.box({0, 0.35f, -0.05f}, {0.45f, 0.12f, 0.75f}, paint);
        body.quad({-0.30f, 0.30f, 1.05f}, {0.30f, 0.30f, 1.05f}, {0.45f, 0.47f, 0.70f}, {-0.45f, 0.47f, 0.70f}, paint);
        body.tri({0.45f, 0.23f, 0.70f}, {0.45f, 0.47f, 0.70f}, {0.30f, 0.30f, 1.05f}, paint);
        body.tri({-0.45f, 0.23f, 0.70f}, {-0.30f, 0.30f, 1.05f}, {-0.45f, 0.47f, 0.70f}, paint);
        body.quad({-0.30f, 0.23f, 1.05f}, {0.30f, 0.23f, 1.05f}, {0.30f, 0.30f, 1.05f}, {-0.30f, 0.30f, 1.05f}, paint);
        body.box({0, 0.62f, -0.45f}, {0.30f, 0.16f, 0.10f}, kSeat);
        body.box({0, 0.58f, -0.80f}, {0.50f, 0.04f, 0.12f}, trim);
        body.box({0, 0.45f, 0.55f}, {0.05f, 0.12f, 0.05f}, kDark);
        body.box({0, 0.25f, -0.85f}, {0.20f, 0.10f, 0.10f}, kDark);
    }
    driver(body, seat, l.driver, bike);
    b.append(body.data(), glm::translate(mat4(1.0f), vec3(0, lift, 0)));
    wheels(b, ws, l.tires);
    return b.take();
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
        for (float x : {-1.0f, 1.0f}) bar(b, {x * 0.3f, 1.0f, 0}, {x * 1.3f, 1.75f, 0}, 0.01f, frame);
    } else if (g.shape == "kite") {
        two_sided({0, 1.7f, 0.9f}, {-1.1f, 1.8f, 0.0f}, {0, 1.9f, -0.9f}, {1.1f, 1.8f, 0.0f}, c);
        bar(b, {0, 1.72f, 0.9f}, {0, 1.88f, -0.9f}, 0.02f, c2);
        bar(b, {-1.1f, 1.8f, 0.0f}, {1.1f, 1.8f, 0.0f}, 0.02f, c2);
        for (float x : {-0.3f, 0.3f}) bar(b, {x, 1.0f, -0.1f}, {x, 1.8f, 0.0f}, 0.012f, frame);
    } else if (g.shape == "umbrella") {
        MeshBuilder canopy;
        canopy.cone({0, 0, 0}, 1.3f, 0.45f, 8, c);
        b.append(canopy.data(), glm::translate(mat4(1.0f), vec3(0, 1.75f, 0)));
        canopy = {};
        canopy.cone({0, 0, 0}, 1.3f, -0.05f, 8, c2); // underside
        b.append(canopy.data(), glm::translate(mat4(1.0f), vec3(0, 1.74f, 0)));
        bar(b, {0, 0.9f, -0.15f}, {0, 2.2f, -0.15f}, 0.025f, frame);
    } else if (g.shape == "leaf") {
        const vec3 tip(0, 1.85f, 1.1f), tail(0, 1.8f, -1.0f);
        two_sided(tip, {-1.0f, 1.75f, 0.2f}, {-0.8f, 1.72f, -0.5f}, tail, c);
        two_sided(tip, tail, {0.8f, 1.72f, -0.5f}, {1.0f, 1.75f, 0.2f}, c);
        bar(b, tail + vec3(0, 0.02f, -0.2f), tip + vec3(0, 0.02f, 0), 0.025f, c2);
        for (float x : {-0.35f, 0.35f}) bar(b, {x, 1.05f, 0}, {x, 1.75f, 0}, 0.025f, c2);
    } else { // wing
        two_sided({-1.4f, 1.75f, -0.4f}, {1.4f, 1.75f, -0.4f}, {0.5f, 1.85f, 0.5f}, {-0.5f, 1.85f, 0.5f}, c);
        for (float x : {-0.35f, 0.35f}) b.box({x, 1.4f, 0.0f}, {0.03f, 0.35f, 0.03f}, c2);
    }
    return b.take();
}

} // namespace gk::render
