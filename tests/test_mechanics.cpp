// Track mechanics: grip road (steep banks karts stick to, faster downhill),
// conveyors, wavy road, presses and swinging platforms.
#include "sim_fixture.h"

#include "gk/core/fixed_step.h"
#include "gk/sim/track_def.h"

#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <string>

using namespace gk;
using namespace gk::sim;

namespace {

// An oval with a long straight down one side: 30 m higher at the top end,
// so the straight descends. `features` is spliced into the JSON.
TrackDef oval(const std::string& features) {
    const std::string text = R"({
      "id": "mech", "name": "Mechanics", "half_width": 7,
      "points": [[0, 30, 0], [0, 0, 150], [40, -5, 190], [80, 0, 150], [80, 30, 0], [40, 35, -40]],
      "features": [)" + features + R"(]
    })";
    TrackDef d;
    std::string err;
    REQUIRE_MESSAGE(parse_track_def(text, d, &err), err);
    return d;
}

quat facing(vec3 forward, vec3 up) {
    forward = glm::normalize(forward - up * glm::dot(forward, up));
    const vec3 left = glm::normalize(glm::cross(up, forward));
    return glm::normalize(glm::quat_cast(glm::mat3(left, glm::cross(forward, left), forward)));
}

// Puts kart 0 on the road at `sample`, `lateral` metres right of the middle, facing along it.
void put(World& w, int sample, float lateral, float speed) {
    KartState& k = w.karts_mut()[0];
    const CenterSample& c = w.track().sample(sample);
    k.pos = c.pos + c.right * lateral + c.up * 0.05f;
    k.rot = facing(c.tangent, c.up);
    k.speed = speed;
    k.slide = vec3(0.0f);
    k.air_vel = vec3(0.0f);
    k.grounded = true;
    k.sample = sample;
    k.last_safe_sample = sample;
}

World world_on(Track t) {
    World w(std::move(t), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1});
    w.add_kart(test::default_params(w.tables(), test::parts()));
    w.tick(std::vector<KartInput>{{}});
    return w;
}

int count(const std::vector<KartEvent>& events, KartEventType type) {
    int n = 0;
    for (const auto& e : events) n += e.type == type;
    return n;
}

} // namespace

TEST_CASE("a bank steeper than 35 degrees needs grip road under it") {
    TrackDef d;
    std::string err;
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0],[0,0,100],[50,0,100],[50,0,0]],
        "features":[{"type":"bank","from":[0,20],"to":[0,80],"angle":70}]})", d, &err));
    CHECK(err.find("grip") != std::string::npos);
    CHECK(parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0],[0,0,100],[50,0,100],[50,0,0]],
        "features":[{"type":"grip","from":[0,10],"to":[0,90]},{"type":"bank","from":[0,20],"to":[0,80],"angle":70}]})", d, &err));
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0],[0,0,100],[50,0,100],[50,0,0]],
        "features":[{"type":"grip","from":[0,10],"to":[0,90]},{"type":"bank","from":[0,20],"to":[0,80],"angle":85}]})", d, &err));
    const Track t = build_track(d);
    int grip = 0;
    for (const auto& c : t.center) grip += c.grip;
    CHECK(grip >= 38);
    CHECK(grip <= 42);
}

TEST_CASE("grip road: hops off a 70-degree bank come back to it") {
    World w = world_on(build_track(oval(R"({"type":"grip","from":[0,10],"to":[0,140]},
                                        {"type":"bank","from":[0,20],"to":[0,130],"angle":70})")));
    const Track& t = w.track();
    const int start = t.nearest_sample(vec3(0, 0, 0)) + 25; // 50 m in: the bank is at its full 70 degrees
    REQUIRE(t.grip_at(start));
    const CenterSample& c0 = t.sample(start);
    CHECK(c0.up.y == doctest::Approx(std::cos(glm::radians(70.0f))).epsilon(0.05));
    put(w, start, -3.0f, 18.0f); // on the high side of the bank
    int landed = 0, airborne = 0, worst_lean = 0;
    for (int f = 0; f < 60 * 4; ++f) {
        KartInput in{.accelerate = true};
        in.drift = f % 60 == 10; // a hop every second
        w.tick(std::vector<KartInput>{in});
        const KartState& k = w.karts()[0];
        REQUIRE(k.respawn_frames == 0);
        if (k.grounded) {
            ++landed;
            const CenterSample& c = t.sample(k.sample);
            if (glm::dot(k.up(), c.up) < 0.9f) ++worst_lean;
        } else {
            ++airborne;
        }
    }
    CHECK(airborne > 20);  // the hops happened
    CHECK(landed > 160);   // and it was back on the road most of the time
    CHECK(worst_lean == 0); // standing on the bank, not on the world's up
    const KartState& k = w.karts()[0];
    const CenterSample& c = t.sample(k.sample);
    CHECK(std::abs(glm::dot(k.pos - c.pos, c.right)) < c.half_width); // still on the road
}

TEST_CASE("grip road: a descent is faster, and the speed carries past the end") {
    auto run = [](const std::string& features) {
        World w = world_on(build_track(oval(features)));
        const int start = w.track().nearest_sample(vec3(0, 30, 0)) + 5;
        put(w, start, 0.0f, 20.0f);
        float top = 0.0f;
        for (int f = 0; f < 60 * 5; ++f) {
            w.tick(std::vector<KartInput>{KartInput{.accelerate = true}});
            top = std::max(top, w.karts()[0].speed);
        }
        return std::pair(top, w.karts()[0].speed);
    };
    const auto [plain_top, plain_now] = run("");
    const auto [grip_top, grip_now] = run(R"({"type":"grip","from":[0,0],"to":[0,100]})");
    CHECK(grip_top > plain_top * 1.05f);
    // 5 s in the kart is past the grip stretch (100 m at 20+ m/s) and still
    // carrying some of it.
    CHECK(grip_now > plain_now * 1.02f);
}

TEST_CASE("a conveyor carries karts on the ground, a current only under water") {
    for (const bool dry : {true, false}) {
        Track t = test::flat_track();
        Track::Current cu;
        cu.first = 0, cu.count = 1000;
        cu.push = 5.0f;
        cu.dry = dry;
        t.currents.push_back(cu);
        World w = world_on(std::move(t));
        put(w, 50, 0.0f, 0.0f);
        const float z0 = w.karts()[0].pos.z;
        for (int f = 0; f < 60; ++f) w.tick(std::vector<KartInput>{{}});
        const float moved = w.karts()[0].pos.z - z0;
        if (dry) CHECK(moved == doctest::Approx(5.0f).epsilon(0.1));
        else CHECK(std::abs(moved) < 0.2f);
    }
}

TEST_CASE("a wavy stretch lifts the floor with the tick, and karts ride it") {
    Track t = test::flat_track();
    Track::Wave wv;
    wv.first = 50, wv.count = 21; // z = 0 .. 40
    wv.u0 = 100.0f, wv.length = 40.0f;
    wv.amplitude = 0.6f, wv.wavelength = 12.0f, wv.period = 120;
    t.waves.push_back(wv);
    // The raycast sees the wave: a crest and a trough half a period apart, flat road outside it.
    t.mover_time = 0.0;
    float lo = 1e9f, hi = -1e9f;
    for (int tick = 0; tick < 120; tick += 10) {
        t.mover_time = double(tick);
        const auto hit = t.raycast(vec3(0, 3, 20), vec3(0, -1, 0), 6.0f);
        REQUIRE(hit);
        lo = std::min(lo, hit->point.y), hi = std::max(hi, hit->point.y);
    }
    CHECK(hi - lo > 1.0f);
    CHECK(hi < 0.65f);
    CHECK(lo > -0.65f);
    const auto flat = t.raycast(vec3(0, 3, -30), vec3(0, -1, 0), 6.0f);
    REQUIRE(flat);
    CHECK(flat->point.y == doctest::Approx(0.0f).epsilon(0.01));
    // A kart parked in the stretch bobs with it and never leaves the road.
    World w = world_on(std::move(t));
    put(w, 60, 0.0f, 0.0f);
    float klo = 1e9f, khi = -1e9f;
    for (int f = 0; f < 240; ++f) {
        w.tick(std::vector<KartInput>{{}});
        const KartState& k = w.karts()[0];
        REQUIRE(k.respawn_frames == 0);
        klo = std::min(klo, k.pos.y), khi = std::max(khi, k.pos.y);
    }
    CHECK(khi - klo > 0.8f);
    // Driven through at speed it gets to the far side.
    put(w, 45, 0.0f, 18.0f);
    for (int f = 0; f < 240; ++f) {
        w.tick(std::vector<KartInput>{KartInput{.accelerate = true}});
        REQUIRE(w.karts()[0].respawn_frames == 0);
    }
    CHECK(w.karts()[0].pos.z > 50.0f);
}

TEST_CASE("a press squashes the kart under it and is driven under while up") {
    Track t = test::flat_track();
    Track::Mover m;
    m.kind = Track::Mover::Kind::Press;
    m.base = {0, 3.0f, 20};
    m.travel = {0, -3.0f, 0};
    m.forward = {0, 0, 1}, m.right = {-1, 0, 0}, m.up = {0, 1, 0};
    m.half = {3.0f, 1.2f, 2.0f};
    m.period = 240;
    t.movers.push_back(m);
    World w = world_on(std::move(t));
    // Phase 0..0.5 of the period it rests up: a kart drives under it.
    put(w, 55, 0.0f, 15.0f); // z = 10
    for (int f = 0; f < 60; ++f) w.tick(std::vector<KartInput>{KartInput{.accelerate = true}});
    CHECK(w.karts()[0].pos.z > 24.0f);
    CHECK(w.karts()[0].tumble_frames == 0);
    // Parked under it as it comes down: squashed.
    put(w, 60, 0.0f, 0.0f); // z = 20
    bool squashed = false;
    for (int f = 0; f < 240 && !squashed; ++f) {
        w.tick(std::vector<KartInput>{{}});
        squashed = count(w.events(), KartEventType::Squash) > 0;
        if (squashed) CHECK(w.karts()[0].tumble_frames > 0);
    }
    CHECK(squashed);
}

TEST_CASE("a swinging platform sways under its pivot and carries its rider") {
    Track t = test::flat_track();
    Track::Mover m;
    m.kind = Track::Mover::Kind::Swing;
    m.base = {0, 0.0f, 20};
    m.forward = {0, 0, 1}, m.right = {-1, 0, 0}, m.up = {0, 1, 0};
    m.half = {3.0f, 0.4f, 6.0f};
    m.arm = 6.0f;
    m.angle = glm::radians(25.0f);
    m.period = 240;
    m.ramp = 2.5f;
    t.movers.push_back(m);
    // Quarter of a period in it hangs 25 degrees to one side: out and up.
    const vec3 rest = t.mover_pos(0, 0.0), out = t.mover_pos(0, 60.0);
    CHECK(std::abs(out.x - rest.x) == doctest::Approx(6.0f * std::sin(glm::radians(25.0f))).epsilon(0.02));
    CHECK(out.y - rest.y == doctest::Approx(6.0f * (1.0f - std::cos(glm::radians(25.0f)))).epsilon(0.02));
    CHECK(t.mover_frame(0, 60.0).up.y == doctest::Approx(std::cos(glm::radians(25.0f))).epsilon(0.01));
    World w = world_on(std::move(t));
    KartState& k = w.karts_mut()[0];
    k.pos = w.track().mover_pos(0, double(w.tick_count())) + vec3(0, 0.8f, 0);
    k.speed = 0.0f, k.slide = vec3(0.0f), k.grounded = true;
    k.sample = w.track().nearest_sample(k.pos);
    for (int i = 0; i < 3; ++i) w.tick(std::vector<KartInput>{{}});
    const vec3 start = w.karts()[0].pos, m0 = w.track().mover_pos(0, double(w.tick_count()));
    for (int i = 0; i < 40; ++i) w.tick(std::vector<KartInput>{{}});
    const vec3 moved = w.karts()[0].pos - start, m_moved = w.track().mover_pos(0, double(w.tick_count())) - m0;
    CHECK(std::abs(m_moved.x) > 0.8f);
    CHECK(moved.x == doctest::Approx(m_moved.x).epsilon(0.2));
    CHECK(w.karts()[0].pos.y > 0.6f);
}

TEST_CASE("presses, swings, conveyors and waves parse and build from track data") {
    TrackDef d;
    std::string err;
    const bool ok = parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0],[0,0,200],[50,0,200],[50,0,0]],
        "features":[{"type":"mover","kind":"press","at":[0,40],"drop":3.5,"period":3},
                    {"type":"mover","kind":"swing","at":[0,80],"arm":5,"angle":20,"height_above":0.5},
                    {"type":"conveyor","from":[0,100],"to":[0,140],"push":6},
                    {"type":"wave","from":[0,150],"to":[0,190],"amplitude":0.5,"wavelength":10,"period":1.5}]})", d, &err);
    REQUIRE_MESSAGE(ok, err);
    const Track t = build_track(d);
    REQUIRE(t.movers.size() == 2);
    CHECK(t.movers[0].kind == Track::Mover::Kind::Press);
    CHECK(t.movers[0].travel.y == doctest::Approx(-3.5f));
    CHECK(t.movers[0].base.y == doctest::Approx(3.5f));
    CHECK(t.movers[1].kind == Track::Mover::Kind::Swing);
    CHECK(t.movers[1].arm == doctest::Approx(5.0f));
    REQUIRE(t.currents.size() == 1);
    CHECK(t.currents[0].dry);
    REQUIRE(t.waves.size() == 1);
    CHECK(t.waves[0].period == 90);
    CHECK(t.waves[0].count == 21);
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0],[0,0,200],[50,0,200],[50,0,0]],
        "features":[{"type":"wave","from":[0,150],"to":[0,190],"amplitude":3}]})", d, &err));
}

TEST_CASE("a star or bullet drives over lava on the road, but the lava below still takes it") {
    // A lava zone across the road at samples 100..120, and lava floor (not a
    // zone) beyond x = 20.
    Track t = test::flat_track(Surface::OutOfBounds, 20.0f, 2000.0f);
    Track::Zone z;
    z.first = 100, z.count = 20;
    z.surface = Surface::OutOfBounds;
    t.zones.push_back(z);
    World w = world_on(std::move(t));
    auto drive = [&](int frames) {
        bool lost = false;
        for (int f = 0; f < frames && !lost; ++f) {
            w.tick(std::vector<KartInput>{KartInput{.accelerate = true}});
            lost = w.karts()[0].respawn_frames > 0;
        }
        return lost;
    };
    put(w, 90, 0.0f, 20.0f);
    CHECK(drive(90));
    for (const bool bullet : {false, true}) {
        CAPTURE(bullet);
        put(w, 90, 0.0f, 20.0f);
        KartState& k = w.karts_mut()[0];
        k.respawn_frames = 0, k.invincible_frames = 0;
        (bullet ? k.bullet_frames : k.star_frames) = 300;
        CHECK_FALSE(drive(180));
        CHECK(w.karts()[0].sample > 120);
        // Off the road onto the lava floor: lost all the same.
        put(w, 300, -24.0f, 5.0f);
        w.karts_mut()[0].star_frames = 300;
        CHECK(drive(30));
        w.karts_mut()[0] = KartState{};
        put(w, 10, 0.0f, 0.0f);
        for (int f = 0; f < 120; ++f) w.tick(std::vector<KartInput>{{}});
    }
}

TEST_CASE("a press doesn't squash a kart with a star") {
    Track t = test::flat_track();
    Track::Mover m;
    m.kind = Track::Mover::Kind::Press;
    m.base = {0, 3.0f, 20};
    m.travel = {0, -3.0f, 0};
    m.forward = {0, 0, 1}, m.right = {-1, 0, 0}, m.up = {0, 1, 0};
    m.half = {3.0f, 1.2f, 2.0f};
    m.period = 240;
    t.movers.push_back(m);
    World w = world_on(std::move(t));
    put(w, 60, 0.0f, 0.0f); // z = 20, under it
    w.karts_mut()[0].star_frames = 400;
    for (int f = 0; f < 240; ++f) {
        w.tick(std::vector<KartInput>{{}});
        CHECK(w.karts()[0].tumble_frames == 0);
    }
}

namespace {

// A straight road over one smooth hill `height` m high and about 2 x `width`
// m long, peaking at z = 200 (sample 150).
Track hill_track(float height, float width) {
    Track t;
    t.name = "hill";
    auto y = [&](float z) { const float u = (z - 200.0f) / width; return height * std::exp(-u * u); };
    auto dy = [&](float z) { const float u = (z - 200.0f) / width; return -2.0f * u / width * height * std::exp(-u * u); };
    for (int i = 0; i <= 1000; ++i) {
        const float z = float(i) - 100.0f;
        t.vertices.push_back({-10.0f, y(z), z});
        t.vertices.push_back({10.0f, y(z), z});
        if (i == 0) continue;
        const uint32_t a = uint32_t(2 * (i - 1)), b = uint32_t(2 * i);
        t.triangles.push_back({a, b, b + 1, Surface::Road});
        t.triangles.push_back({a, b + 1, a + 1, Surface::Road});
    }
    for (int i = 0; i < 450; ++i) {
        const float z = float(i) * 2.0f - 100.0f;
        CenterSample c;
        c.pos = {0, y(z), z};
        c.tangent = glm::normalize(vec3(0, dy(z), 1));
        c.right = {-1, 0, 0};
        c.up = glm::normalize(vec3(0, 1, -dy(z)));
        c.s = c.u = float(i) * 2.0f;
        t.center.push_back(c);
    }
    t.checkpoints = {0};
    t.length = 900.0f;
    t.kill_y = -30.0f;
    t.finalize();
    return t;
}

// Drives kart 0 from sample 100 over the hill; returns the most frames it spent in the air at once.
int over_the_hill(World& w, float speed, bool gas) {
    put(w, 100, 0.0f, speed);
    int run = 0, longest = 0;
    for (int f = 0; f < 60 * 8 && w.karts()[0].pos.z < 320.0f; ++f) {
        w.tick(std::vector<KartInput>{KartInput{.accelerate = gas}});
        run = w.karts()[0].grounded ? 0 : run + 1;
        longest = std::max(longest, run);
        REQUIRE(w.karts()[0].respawn_frames == 0);
    }
    return longest;
}

} // namespace

TEST_CASE("over a crest a fast kart flies a moment, a slow one stays planted") {
    World w = world_on(hill_track(4.0f, 16.0f));
    const float top = w.params(0).top_speed;
    const int fast = over_the_hill(w, top, true);
    MESSAGE("at ", top, " m/s: ", fast, " frames in the air");
    CHECK(fast >= 4);
    CHECK(fast <= 40); // a moment, not a jump
    CHECK(w.karts()[0].grounded);
    CHECK(over_the_hill(w, 10.0f, false) == 0);
    // A gentle rise doesn't throw it even flat out.
    World g = world_on(hill_track(2.0f, 30.0f));
    CHECK(over_the_hill(g, top, true) == 0);
}

TEST_CASE("flat out on flat road the kart never leaves it") {
    World w = world_on(test::flat_track());
    put(w, 10, 0.0f, 0.0f);
    for (int f = 0; f < 60 * 6; ++f) {
        w.tick(std::vector<KartInput>{KartInput{.accelerate = true}});
        CHECK(w.karts()[0].grounded);
    }
}

namespace {

// A straight road that runs flat, then bends smoothly into a climb of
// `degrees` from about z = 100 on.
Track slope_track(float degrees) {
    Track t;
    t.name = "slope";
    const float rise = std::tan(glm::radians(degrees)), L = 10.0f;
    auto y = [&](float z) { return rise * L * std::log1p(std::exp((z - 100.0f) / L)); };
    auto dy = [&](float z) { return rise / (1.0f + std::exp(-(z - 100.0f) / L)); };
    for (int i = 0; i <= 500; ++i) {
        const float z = float(i) * 2.0f - 100.0f;
        t.vertices.push_back({-10.0f, y(z), z});
        t.vertices.push_back({10.0f, y(z), z});
        if (i == 0) continue;
        const uint32_t a = uint32_t(2 * (i - 1)), b = uint32_t(2 * i);
        t.triangles.push_back({a, b, b + 1, Surface::Road});
        t.triangles.push_back({a, b + 1, a + 1, Surface::Road});
    }
    for (int i = 0; i < 450; ++i) {
        const float z = float(i) * 2.0f - 100.0f;
        CenterSample c;
        c.pos = {0, y(z), z};
        c.tangent = glm::normalize(vec3(0, dy(z), 1));
        c.right = {-1, 0, 0};
        c.up = glm::normalize(vec3(0, 1, -dy(z)));
        c.half_width = 10.0f;
        c.s = c.u = float(i) * 2.0f;
        t.center.push_back(c);
    }
    t.checkpoints = {0};
    t.length = 900.0f;
    t.kill_y = -30.0f;
    t.finalize();
    return t;
}

// The oval with its straight banked at 80 degrees on grip road.
Track bank80() {
    return build_track(oval(R"({"type":"grip","from":[0,10],"to":[0,140]},
                              {"type":"bank","from":[0,20],"to":[0,130],"angle":80})"));
}

// The first sample of the bank at its full angle.
int bank_start(const Track& t) {
    int i = t.nearest_sample(vec3(0, 0, 0));
    while (t.sample(i).up.y > std::cos(glm::radians(78.0f))) ++i;
    return i;
}

World world_of(Track t, int karts) {
    World w(std::move(t), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1});
    for (int i = 0; i < karts; ++i) w.add_kart(test::default_params(w.tables(), test::parts()));
    w.tick(std::vector<KartInput>(size_t(karts)));
    return w;
}

// Puts kart `i` at rest on the road at `sample`.
void park(World& w, int i, int sample, float lateral) {
    KartState& k = w.karts_mut()[size_t(i)];
    const CenterSample& c = w.track().sample(sample);
    k.pos = c.pos + c.right * lateral + c.up * 0.05f;
    k.rot = facing(c.tangent, c.up);
    k.speed = 0.0f;
    k.slide = k.air_vel = vec3(0.0f);
    k.grounded = true;
    k.sample = k.last_safe_sample = sample;
}

// An item just above the road at `sample`, `lateral` m right of the middle,
// moving along the road at `speed`.
ItemEntity item_on(const Track& t, EntityType type, int sample, float lateral, float speed) {
    const CenterSample& c = t.sample(sample);
    ItemEntity e;
    e.type = type;
    e.owner = 0;
    e.sample = int16_t(sample);
    e.pos = c.pos + c.right * lateral + c.up * 0.3f;
    e.vel = c.tangent * speed;
    e.age = 100; // long out of its thrower's hands
    e.life = int16_t(type == EntityType::Fireball ? 150 : type == EntityType::GreenShell ? 600 : 0);
    return e;
}

// Where `p` is against the road beside it: height along the road's up, and across it.
struct Against {
    float height, across, half_width;
    vec3 tangent;
};
Against against(const Track& t, vec3 p) {
    const CenterSample& c = t.sample(t.nearest_sample(p));
    return {glm::dot(p - c.pos, c.up), glm::dot(p - c.pos, c.right), c.half_width, c.tangent};
}

// Runs one item alone for `frames` and checks it stays on the road the
// whole way. Returns it as it ends up.
ItemEntity ride(World& w, const ItemEntity& start, int frames) {
    w.entities_mut() = {start};
    for (int f = 0; f < frames; ++f) {
        w.tick(std::vector<KartInput>(w.karts().size()));
        REQUIRE(w.entities().size() == 1);
        const ItemEntity& e = w.entities()[0];
        const Against a = against(w.track(), e.pos);
        CAPTURE(f);
        CHECK(a.height > -0.3f);
        CHECK(a.height < 1.5f);
        CHECK(std::abs(a.across) < a.half_width);
    }
    return w.entities()[0];
}

} // namespace

TEST_CASE("shells, fireballs, bananas and bombs ride and rest on an 80-degree grip bank") {
    const Track probe = bank80();
    const int start = bank_start(probe);
    REQUIRE(probe.grip_at(start));
    REQUIRE(probe.grip_at(start + 40));
    for (const EntityType type : {EntityType::GreenShell, EntityType::Fireball, EntityType::Banana, EntityType::Bomb}) {
        CAPTURE(int(type));
        World w = world_of(bank80(), 1);
        park(w, 0, start + 120, 0.0f); // well out of the way
        const bool rolls = type == EntityType::GreenShell || type == EntityType::Fireball;
        // Shells and fireballs come onto the bank from the level road before it.
        const ItemEntity e0 = item_on(w.track(), type, rolls ? start - 20 : start, -3.0f, rolls ? 24.0f : 0.0f);
        const ItemEntity e = ride(w, e0, rolls ? 140 : 60); // a fireball burns out at 150
        if (rolls) CHECK(w.track().sample(w.track().nearest_sample(e.pos)).up.y < 0.25f); // out on the bank
        CHECK(e.grounded);
        const Against a = against(w.track(), e.pos);
        if (rolls) {
            CHECK(glm::dot(e.vel, a.tangent) > 20.0f); // still going along the bank, not slowed by it
        } else {
            CHECK(glm::length(e.pos - e0.pos) < 1.0f); // stays where it was dropped
            CHECK(std::abs(a.height) < 0.6f);           // lying on the bank
        }
    }
}

TEST_CASE("a red shell catches its target on an 80-degree grip bank and up a steep slope") {
    {
        World w = world_of(bank80(), 2);
        const int start = bank_start(w.track());
        park(w, 0, start + 120, 0.0f);
        park(w, 1, start + 30, 3.0f); // 60 m on, low on the bank
        ItemEntity e = item_on(w.track(), EntityType::RedShell, start - 20, -3.0f, 25.0f); // from the level road before it
        e.target = 1;
        w.entities_mut() = {e};
        bool hit = false;
        for (int f = 0; f < 180 && !hit; ++f) {
            w.tick(std::vector<KartInput>(2));
            for (const auto& ev : w.events()) hit |= ev.type == KartEventType::Hit && ev.kart == 1;
            if (!hit && !w.entities().empty()) {
                const Against a = against(w.track(), w.entities()[0].pos);
                CHECK(a.height > -0.3f);
                CHECK(a.height < 1.5f);
            }
        }
        CHECK(hit);
    }
    {
        World w = world_of(slope_track(40.0f), 2);
        const int start = w.track().nearest_sample(vec3(0, 0, 90));
        park(w, 0, start + 200, 0.0f);
        park(w, 1, start + 30, -4.0f); // up the slope, off to the side
        float way = 0.0f; // along the road to it
        for (int i = start; i < start + 30; ++i) way += glm::length(w.track().sample(i + 1).pos - w.track().sample(i).pos);
        ItemEntity e = item_on(w.track(), EntityType::RedShell, start, 4.0f, 25.0f);
        e.target = 1;
        w.entities_mut() = {e};
        int hit_at = -1;
        for (int f = 0; f < 240 && hit_at < 0; ++f) {
            w.tick(std::vector<KartInput>(2));
            for (const auto& ev : w.events())
                if (ev.type == KartEventType::Hit && ev.kart == 1) hit_at = f;
        }
        CHECK(hit_at >= 0);
        CHECK(float(hit_at) < way / w.item_tuning().red_speed * 60.0f * 1.15f); // at shell speed, not slowed by the climb
    }
}

TEST_CASE("a green shell climbs a steep slope at its speed, a banana rests on it") {
    World w = world_of(slope_track(40.0f), 1);
    const int start = w.track().nearest_sample(vec3(0, 0, 150));
    REQUIRE(w.track().sample(start).up.y < 0.8f);
    park(w, 0, start + 200, 0.0f);
    const ItemEntity shell = ride(w, item_on(w.track(), EntityType::GreenShell, start, 0.0f, 30.0f), 40);
    CHECK(shell.grounded);
    CHECK(glm::dot(shell.vel, against(w.track(), shell.pos).tangent) > 27.0f);
    const ItemEntity banana0 = item_on(w.track(), EntityType::Banana, start, 2.0f, 0.0f);
    const ItemEntity banana = ride(w, banana0, 60);
    CHECK(banana.grounded);
    CHECK(glm::length(banana.pos - banana0.pos) < 1.0f);
}

TEST_CASE("a spiked shell flies along the road's up over an 80-degree grip bank") {
    World w = world_of(bank80(), 2);
    const int start = bank_start(w.track());
    park(w, 0, start - 5, 0.0f);
    park(w, 1, start + 120, 0.0f); // the leader, far ahead
    w.karts_mut()[1].lap = 2;
    ItemEntity e = item_on(w.track(), EntityType::SpikedShell, start, 0.0f, 0.0f);
    e.pos = w.track().sample(start).pos + w.track().sample(start).up * 3.0f;
    w.entities_mut() = {e};
    int checked = 0;
    for (int f = 0; f < 50; ++f) {
        w.tick(std::vector<KartInput>(2));
        REQUIRE(w.entities().size() == 1);
        const ItemEntity& s = w.entities()[0];
        const CenterSample& c = w.track().sample(w.track().nearest_sample(s.pos));
        if (!w.track().grip_at(w.track().nearest_sample(s.pos)) || c.up.y > 0.3f) continue; // on the full bank only
        ++checked;
        CAPTURE(f);
        const Against a = against(w.track(), s.pos);
        CHECK(a.height > 1.5f);
        CHECK(std::abs(a.across) < a.half_width);
    }
    CHECK(checked > 5);
}
