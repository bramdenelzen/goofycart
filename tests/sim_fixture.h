#pragma once
#include "gk/sim/data.h"
#include "gk/sim/world.h"

#include <doctest/doctest.h>

#include <cmath>

namespace test {

inline gk::sim::PhysicsTables tables() {
    gk::sim::PhysicsTables t;
    std::string err;
    REQUIRE_MESSAGE(t.load(gk::sim::data_path("physics.json"), &err), err);
    return t;
}

inline gk::sim::PartsCatalog parts() {
    gk::sim::PartsCatalog c;
    std::string err;
    REQUIRE_MESSAGE(c.load(gk::sim::data_path("parts.json"), &err), err);
    return c;
}

inline gk::sim::KartParams default_params(const gk::sim::PhysicsTables& t, const gk::sim::PartsCatalog& c,
                                          gk::sim::Build b = {1, 0, 0, 0},
                                          gk::sim::EngineClass cc = gk::sim::EngineClass::cc150) {
    return t.params(c.levels(b), c.body(b), cc);
}

// A world with one kart that is already racing (countdown skipped).
inline gk::sim::World racing_world(int karts = 1) {
    auto t = tables();
    auto c = parts();
    gk::sim::World w(gk::sim::make_greybox_track(), t, gk::sim::RaceConfig{.countdown_frames = 1});
    for (int i = 0; i < karts; ++i) w.add_kart(default_params(t, c));
    gk::sim::KartInput none;
    w.tick(std::span(&none, 1));
    return w;
}

} // namespace test

namespace test {

// Flat test ground: road everywhere, plus an optional stripe of another
// surface for x in [stripe_x0, stripe_x1]. A straight centre line runs along +Z.
inline gk::sim::Track flat_track(gk::sim::Surface stripe = gk::sim::Surface::Road, float stripe_x0 = 0, float stripe_x1 = 0) {
    using namespace gk::sim;
    Track t;
    t.name = "flat";
    const float e = 2000.0f;
    auto add_quad = [&](float x0, float x1, Surface s) {
        const uint32_t b = uint32_t(t.vertices.size());
        t.vertices.push_back({x0, 0, -e});
        t.vertices.push_back({x0, 0, e});
        t.vertices.push_back({x1, 0, e});
        t.vertices.push_back({x1, 0, -e});
        // Faces up when x1 > x0.
        t.triangles.push_back({b, b + 1, b + 2, s});
        t.triangles.push_back({b, b + 2, b + 3, s});
    };
    if (stripe_x1 > stripe_x0) {
        add_quad(stripe_x1, e, Surface::Road);
        add_quad(stripe_x0, stripe_x1, stripe);
        add_quad(-e, stripe_x0, Surface::Road);
    } else {
        add_quad(-e, e, Surface::Road);
    }
    for (int i = 0; i < 1000; ++i) {
        CenterSample c;
        c.pos = {0, 0, float(i) * 2.0f - 100.0f};
        c.tangent = {0, 0, 1};
        c.right = {-1, 0, 0};
        c.up = {0, 1, 0};
        c.s = float(i) * 2.0f;
        t.center.push_back(c);
    }
    t.checkpoints = {0};
    t.length = 2000.0f;
    t.finalize();
    return t;
}

// A flat stadium loop: two straights joined by tight half circles, with
// grass beyond the road edges. For CPU driving tests that need real corners.
inline gk::sim::Track stadium_track(float radius = 22.0f, float straight = 90.0f, float half_width = 7.0f) {
    using namespace gk;
    using namespace gk::sim;
    Track t;
    t.name = "stadium";
    const float pi = 3.14159265f;
    const float total = 2.0f * straight + 2.0f * pi * radius;
    const int n = int(total / 2.0f);
    auto at = [&](float s, vec3& pos, vec3& tan) {
        s = std::fmod(s, total);
        if (s < straight) { pos = {radius, 0, s}; tan = {0, 0, 1}; return; }
        s -= straight;
        if (s < pi * radius) {
            const float a = s / radius; // left turn round (0, 0, straight)
            pos = {radius * std::cos(a), 0, straight + radius * std::sin(a)};
            tan = {-std::sin(a), 0, std::cos(a)};
            return;
        }
        s -= pi * radius;
        if (s < straight) { pos = {-radius, 0, straight - s}; tan = {0, 0, -1}; return; }
        s -= straight;
        const float a = s / radius;
        pos = {-radius * std::cos(a), 0, -radius * std::sin(a)};
        tan = {std::sin(a), 0, -std::cos(a)};
    };
    for (int i = 0; i < n; ++i) {
        CenterSample c;
        c.s = float(i) * total / float(n);
        at(c.s, c.pos, c.tangent);
        c.right = glm::normalize(glm::cross(c.tangent, vec3(0, 1, 0)));
        c.up = {0, 1, 0};
        c.half_width = half_width;
        t.center.push_back(c);
        for (float lat : {-(half_width + 8.0f), -half_width, half_width, half_width + 8.0f})
            t.vertices.push_back(c.pos + c.right * lat);
    }
    auto V = [&](int i, int col) { return uint32_t((i % n) * 4 + col); };
    for (int i = 0; i < n; ++i) {
        const Surface cols[3] = {Surface::OffroadMedium, Surface::Road, Surface::OffroadMedium};
        for (int col = 0; col < 3; ++col) {
            // Wound so cross(b - a, c - a) points up.
            t.triangles.push_back({V(i, col), V(i, col + 1), V(i + 1, col + 1), cols[col]});
            t.triangles.push_back({V(i, col), V(i + 1, col + 1), V(i + 1, col), cols[col]});
        }
    }
    t.length = total;
    for (int i = 0; i < n; i += 15) t.checkpoints.push_back(i);
    t.kill_y = -20.0f;
    t.finalize();
    return t;
}

inline gk::sim::KartState kart_at(gk::vec3 pos) {
    gk::sim::KartState s;
    s.pos = pos;
    return s;
}

} // namespace test
