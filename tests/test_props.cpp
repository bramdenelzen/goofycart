// Breakable obstacles: crates smash and come back, tire stacks topple, bounce
// the kart back and leave tires that spring karts into the air.
#include "sim_fixture.h"

using namespace gk;
using namespace gk::sim;

namespace {

Track::Obstacle obstacle(Track::Breakable kind, vec3 pos, int sides) {
    Track::Obstacle o;
    o.pos = pos;
    o.forward = {0, 0, 1};
    o.right = {-1, 0, 0};
    o.up = {0, 1, 0};
    o.half = kind == Track::Breakable::Tires ? vec3(0.85f, 0.55f, 0.85f) : vec3(1.1f, 0.8f, 1.1f);
    o.sides = sides;
    o.breakable = kind;
    return o;
}

struct PropBench {
    World world;
    std::vector<KartInput> inputs{KartInput{.accelerate = true}};

    explicit PropBench(Track::Obstacle o) : world(make_track(o), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1}) {
        world.add_kart(test::default_params(world.tables(), test::parts()));
        world.tick(std::vector<KartInput>{{}});
        KartState& k = world.karts_mut()[0];
        k.pos = {0, 0, 0};
        k.rot = quat(1, 0, 0, 0);
        k.speed = 25.0f;
        k.sample = world.track().nearest_sample(k.pos);
    }
    static Track make_track(Track::Obstacle o) {
        Track t = test::flat_track();
        t.obstacles.push_back(o);
        return t;
    }
    const KartState& kart() const { return world.karts()[0]; }
    // Drives until `done`, returning the ticks taken (or -1).
    template <class F>
    int run(F&& done, int limit = 600) {
        for (int i = 0; i < limit; ++i) {
            world.tick(inputs);
            if (done()) return i;
        }
        return -1;
    }
};

} // namespace

TEST_CASE("a crate smashes, slows the kart a little and comes back") {
    PropBench b(obstacle(Track::Breakable::Crate, {0, 0, 20}, 4));
    float before = 0.0f;
    bool smashed = false;
    const int t = b.run([&] {
        for (const KartEvent& e : b.world.events())
            if (e.type == KartEventType::Smash) smashed = true;
        if (!smashed) before = b.kart().speed;
        return smashed;
    });
    REQUIRE(t >= 0);
    CHECK(b.world.props()[0].down > 0);
    CHECK(b.kart().speed < before * 0.85f);
    CHECK(b.kart().speed > before * 0.6f); // through it, not stopped
    CHECK(b.kart().pos.z > 18.0f);
    // The kart drives off; the crate is back once the coast is clear.
    b.run([] { return false; }, kCrateRespawnTicks + 10);
    CHECK(b.world.props()[0].down == 0);
}

TEST_CASE("a tire stack topples and bounces the kart back; a loose tire launches it") {
    PropBench b(obstacle(Track::Breakable::Tires, {0, 0, 20}, 8));
    bool knocked = false;
    b.run([&] {
        for (const KartEvent& e : b.world.events()) knocked |= e.type == KartEventType::TireKnock;
        return knocked;
    });
    REQUIRE(knocked);
    CHECK(b.kart().speed < 0.0f); // bounced backwards
    CHECK(b.kart().pos.z < 20.0f);

    // Loose tires roll out in front of the stack's spot; drive over one.
    b.run([] { return false; }, 40);
    const vec3 tire = b.world.loose_tire_pos(0, 1);
    KartState& k = b.world.karts_mut()[0];
    k.pos = tire - vec3(0, 0.3f, 3.0f);
    k.rot = quat(1, 0, 0, 0);
    k.speed = 20.0f;
    k.slide = vec3(0.0f);
    k.grounded = true;
    k.sample = b.world.track().nearest_sample(k.pos);
    bool launched = false;
    b.run([&] {
        for (const KartEvent& e : b.world.events()) launched |= e.type == KartEventType::TireBounce;
        return launched;
    }, 60);
    REQUIRE(launched);
    CHECK_FALSE(b.kart().grounded);
    CHECK(b.kart().air_vel.y > 5.0f);
    CHECK(b.world.props()[0].used != 0); // that tire is kicked away
}

TEST_CASE("a star smashes through without slowing") {
    PropBench b(obstacle(Track::Breakable::Crate, {0, 0, 20}, 4));
    b.world.karts_mut()[0].star_frames = 600;
    b.world.karts_mut()[0].speed = 25.0f;
    float before = 25.0f;
    bool smashed = false;
    b.run([&] {
        for (const KartEvent& e : b.world.events()) smashed |= e.type == KartEventType::Smash;
        if (!smashed) before = b.kart().speed;
        return smashed;
    });
    REQUIRE(smashed);
    CHECK(b.kart().speed >= before * 0.98f);
}

namespace {
World mover_world(Track::Mover m) {
    Track t = test::flat_track();
    t.movers.push_back(m);
    World w(std::move(t), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1});
    w.add_kart(test::default_params(w.tables(), test::parts()));
    w.tick(std::vector<KartInput>{{}});
    return w;
}
Track::Mover platform(vec3 base, vec3 travel) {
    Track::Mover m;
    m.kind = Track::Mover::Kind::Platform;
    m.base = base;
    m.forward = {0, 0, 1};
    m.right = {-1, 0, 0};
    m.up = {0, 1, 0};
    m.half = {3.0f, 0.4f, 6.0f};
    m.ramp = 2.5f;
    m.travel = travel;
    m.period = 240;
    return m;
}
} // namespace

TEST_CASE("karts drive up a platform's ramp and ride it as it moves") {
    {   // A still platform: up the ramp, along the top, down the far ramp.
        World ws = mover_world(platform({0, 0, 20}, {0, 0, 0}));
        KartState& k = ws.karts_mut()[0];
        k.pos = {0, 0, 5};
        k.rot = quat(1, 0, 0, 0);
        k.speed = 12.0f;
        k.sample = ws.track().nearest_sample(k.pos);
        std::vector<KartInput> go{KartInput{.accelerate = true}};
        float top = 0.0f;
        for (int i = 0; i < 150; ++i) {
            ws.tick(go);
            top = std::max(top, ws.karts()[0].pos.y);
        }
        CHECK(top > 0.6f);
        CHECK(ws.karts()[0].pos.z > 26.0f); // over it, not stopped by it
    }
    World w = mover_world(platform({0, 0, 20}, {4, 0, 0}));
    KartState& k = w.karts_mut()[0];
    k.pos = {0, 0, 5};
    k.rot = quat(1, 0, 0, 0);
    k.speed = 12.0f;
    k.sample = w.track().nearest_sample(k.pos);
    std::vector<KartInput> in{KartInput{.accelerate = true}};
    // Park on top: it carries the kart across the road.
    KartState& p = w.karts_mut()[0];
    const vec3 at = w.track().mover_pos(0, double(w.tick_count()));
    p.pos = at + vec3(0, 0.8f, 0);
    p.speed = 0.0f;
    p.slide = vec3(0.0f);
    p.grounded = true;
    std::vector<KartInput> idle{KartInput{}};
    for (int i = 0; i < 3; ++i) w.tick(idle);
    const vec3 start = w.karts()[0].pos, m0 = w.track().mover_pos(0, double(w.tick_count()));
    for (int i = 0; i < 40; ++i) w.tick(idle);
    const vec3 moved = w.karts()[0].pos - start, m_moved = w.track().mover_pos(0, double(w.tick_count())) - m0;
    CHECK(std::abs(m_moved.x) > 1.0f);
    CHECK(moved.x == doctest::Approx(m_moved.x).epsilon(0.15));
    CHECK(w.karts()[0].pos.y > 0.6f); // still on top
}

TEST_CASE("a moving block is solid") {
    Track::Mover m = platform({0, 0, 20}, {0, 0, 0});
    m.kind = Track::Mover::Kind::Block;
    m.half = {1.5f, 1.2f, 1.5f};
    m.ramp = 0.0f;
    World w = mover_world(m);
    KartState& k = w.karts_mut()[0];
    k.pos = {0, 0, 5};
    k.rot = quat(1, 0, 0, 0);
    k.speed = 20.0f;
    k.sample = w.track().nearest_sample(k.pos);
    std::vector<KartInput> in{KartInput{.accelerate = true}};
    for (int i = 0; i < 90; ++i) w.tick(in);
    CHECK(w.karts()[0].pos.z < 20.0f - 1.5f); // stopped at its face, not through it
}
