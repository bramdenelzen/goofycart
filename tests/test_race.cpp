#include "sim_fixture.h"

using namespace gk;
using namespace gk::sim;

namespace {
int count(const std::vector<KartEvent>& events, KartEventType type) {
    int n = 0;
    for (const auto& e : events) n += e.type == type;
    return n;
}
} // namespace

TEST_CASE("greybox track has every surface class and a gap") {
    const Track t = make_greybox_track();
    bool seen[int(Surface::Count)] = {};
    for (const auto& tri : t.triangles) seen[int(tri.surface)] = true;
    for (Surface s : {Surface::Road, Surface::OffroadLight, Surface::OffroadMedium, Surface::OffroadHeavy, Surface::Sand,
                      Surface::Ice, Surface::BoostPad, Surface::GlideRamp, Surface::TrickRamp, Surface::Wall,
                      Surface::OutOfBounds}) {
        CAPTURE(surface_name(s));
        CHECK(seen[int(s)]);
    }
    int gaps = 0;
    for (const auto& c : t.center) gaps += !c.has_floor;
    CHECK(gaps > 10);
    CHECK(t.checkpoints.size() > 10);
    CHECK(t.length > 700.0f);
}

TEST_CASE("autopilot drives a full lap, gliding over the chasm") {
    World w = test::racing_world();
    std::vector<KartEvent> all;
    int frames = 0;
    while (w.karts()[0].lap < 2 && frames < 60 * 120) {
        KartInput in = autopilot(w.karts()[0], w.track());
        w.tick(std::span(&in, 1));
        all.insert(all.end(), w.events().begin(), w.events().end());
        ++frames;
    }
    CHECK(w.karts()[0].lap == 2);
    CHECK(count(all, KartEventType::GliderOpen) >= 1);
    CHECK(count(all, KartEventType::Respawn) == 0);
    CHECK(count(all, KartEventType::Lap) == 1);
    MESSAGE("lap time: " << frames / 60.0 << " s");
}

TEST_CASE("circling at the start line does not count laps") {
    World w = test::racing_world();
    KartState& k = w.karts_mut()[0];
    const Track& t = w.track();
    // Jump back and forth across the line without visiting the checkpoints.
    for (int i = 0; i < 6; ++i) {
        k.pos = t.center[size_t(i % 2 == 0 ? 3 : t.center.size() - 3)].pos;
        kart_update_progress(k, t);
    }
    CHECK(k.lap == 1);
}

TEST_CASE("falling into the chasm respawns the kart on the road before the glide ramp") {
    World w = test::racing_world();
    KartState& k = w.karts_mut()[0];
    const Track& t = w.track();
    int gap = 0;
    while (t.center[size_t(gap)].has_floor) ++gap;
    k.pos = t.center[size_t(gap + 5)].pos + vec3(0, 2, 0);
    k.grounded = false;
    k.air_vel = vec3(0.0f);
    std::vector<KartEvent> all;
    KartInput none;
    for (int i = 0; i < 60 * 6; ++i) {
        w.tick(std::span(&none, 1));
        all.insert(all.end(), w.events().begin(), w.events().end());
    }
    CHECK(count(all, KartEventType::Respawn) == 1);
    CHECK(k.respawn_frames == 0);
    CHECK(t.center[size_t(k.sample)].respawn_ok);
    CHECK(t.center[size_t(k.sample)].has_floor);
    CHECK(k.grounded);
}

TEST_CASE("rocket start: pressing in the window boosts, holding from the start burns out") {
    auto run = [](int press_frames_before_go) {
        auto t = test::tables();
        auto c = test::parts();
        World w(make_greybox_track(), t, RaceConfig{.countdown_frames = 180});
        w.add_kart(test::default_params(t, c));
        std::vector<KartEvent> all;
        for (int f = 0; f < 182; ++f) {
            KartInput in;
            in.accelerate = (180 - f) <= press_frames_before_go;
            w.tick(std::span(&in, 1));
            all.insert(all.end(), w.events().begin(), w.events().end());
        }
        return all;
    };
    auto good = run(80);
    CHECK(count(good, KartEventType::Boost) == 1);
    auto early = run(180);
    CHECK(count(early, KartEventType::Burnout) == 1);
    auto late = run(10);
    CHECK(count(late, KartEventType::Boost) == 0);
    CHECK(count(late, KartEventType::Burnout) == 0);
}

TEST_CASE("a trick off the ramp lip gives a landing boost") {
    World w = test::racing_world();
    std::vector<KartEvent> all;
    bool pressed = false;
    for (int f = 0; f < 60 * 20 && !pressed; ++f) {
        const KartState& k = w.karts()[0];
        KartInput in = autopilot(k, w.track());
        if (!k.grounded && k.trick_window > 0 && !k.gliding) {
            in.drift = true;
            pressed = true;
        }
        w.tick(std::span(&in, 1));
        all.insert(all.end(), w.events().begin(), w.events().end());
    }
    REQUIRE(pressed);
    for (int f = 0; f < 120; ++f) {
        KartInput in = autopilot(w.karts()[0], w.track());
        w.tick(std::span(&in, 1));
        all.insert(all.end(), w.events().begin(), w.events().end());
    }
    CHECK(count(all, KartEventType::Trick) == 1);
    bool trick_boost = false;
    for (const auto& e : all) trick_boost |= e.type == KartEventType::Boost && e.value == int(BoostKind::Trick);
    CHECK(trick_boost);
}

TEST_CASE("kart bumps push the lighter kart further") {
    auto t = test::tables();
    auto c = test::parts();
    World w(test::flat_track(), t, RaceConfig{.countdown_frames = 1});
    w.add_kart(test::default_params(t, c, {0, 1, 1, 1})); // light
    w.add_kart(test::default_params(t, c, {3, 2, 3, 0})); // heavy
    auto& ks = w.karts_mut();
    ks[0].pos = {0, 0, 0};
    ks[1].pos = {-1.2f, 0, 0};
    ks[0].rot = ks[1].rot = quat(1, 0, 0, 0);
    ks[0].slide = vec3(-5, 0, 0); // light moves towards heavy
    ks[1].slide = vec3(5, 0, 0);
    KartInput none[2];
    w.tick(std::span(none, 2));
    CHECK(glm::length(ks[0].slide) > glm::length(ks[1].slide));
    CHECK(ks[0].slide.x > 0.0f); // light bounced back
}

TEST_CASE("karts pull away uphill from the grid after a burnout") {
    auto t = test::tables();
    auto c = test::parts();
    World w(make_greybox_track(), t); // full countdown, accelerate held throughout
    for (int i = 0; i < 12; ++i) w.add_kart(test::default_params(t, c));
    std::vector<KartInput> in(12);
    for (int f = 0; f < 60 * 8; ++f) {
        for (int i = 0; i < 12; ++i) in[size_t(i)] = autopilot(w.karts()[size_t(i)], w.track());
        w.tick(in);
    }
    for (const auto& k : w.karts()) CHECK(k.speed > 10.0f);
}
