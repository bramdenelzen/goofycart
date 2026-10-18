// Deep water: the underwater mode. Entry and exit with their hysteresis, the
// water stats' speed cap, slow falls, and the state riding in snapshots.
#include "sim_fixture.h"

#include "gk/core/fixed_step.h"
#include "gk/net/protocol.h"

#include <algorithm>

using namespace gk;
using namespace gk::sim;

namespace {

// A flat straight with deep water over samples [100, 150), z = 100 .. 200 m.
Track wet_track() {
    Track t = test::flat_track();
    Track::Zone z;
    z.first = 100, z.count = 50;
    z.surface = Surface::Water;
    z.deep = true;
    z.level = 3.0f;
    t.zones.push_back(z);
    return t;
}

struct WaterBench {
    PhysicsTables tables = test::tables();
    PartsCatalog parts = test::parts();
    KartParams params = test::default_params(tables, parts);
    Track track;
    KartState kart;
    std::vector<KartEvent> events;

    explicit WaterBench(float z, Track t = wet_track()) : track(std::move(t)) {
        kart = test::kart_at({0.0f, 0.0f, z});
        kart.sample = track.nearest_sample(kart.pos);
    }
    // Steps one frame; returns whether the mode flipped on it.
    bool step(const KartInput& in) {
        const bool before = kart.underwater;
        KartStepContext ctx{track, tables.constants, params, 0, false, &events};
        kart_step(kart, in, ctx);
        kart_update_progress(kart, track);
        return kart.underwater != before;
    }
    int count(KartEventType type) const {
        return int(std::count_if(events.begin(), events.end(), [&](const KartEvent& e) { return e.type == type; }));
    }
};

KartInput gas() {
    KartInput in;
    in.accelerate = true;
    return in;
}

} // namespace

TEST_CASE("deep water: a kart goes under after a few frames inside, and comes out the same way") {
    WaterBench b(60.0f);
    const int hysteresis = b.tables.constants.water_hysteresis;
    REQUIRE(hysteresis == 6);
    int inside_for = 0, flipped_after = -1;
    for (int f = 0; f < 600 && flipped_after < 0; ++f) {
        const bool in_water = b.track.in_deep_water(b.kart.pos + b.kart.up() * 0.5f, b.kart.sample);
        if (b.step(gas())) flipped_after = inside_for + 1;
        if (in_water) ++inside_for;
    }
    REQUIRE(b.kart.underwater);
    CHECK(flipped_after == hysteresis);
    CHECK(b.count(KartEventType::WaterEnter) == 1);
    CHECK(b.kart.pos.z > 100.0f);
    // Drive on out the other end.
    int outside_for = 0;
    flipped_after = -1;
    for (int f = 0; f < 900 && flipped_after < 0; ++f) {
        const bool in_water = b.track.in_deep_water(b.kart.pos + b.kart.up() * 0.5f, b.kart.sample);
        if (b.step(gas())) flipped_after = outside_for + 1;
        if (!in_water) ++outside_for;
    }
    CHECK_FALSE(b.kart.underwater);
    CHECK(flipped_after == hysteresis);
    CHECK(b.count(KartEventType::WaterExit) == 1);
    CHECK(b.kart.pos.z > 200.0f);
}

TEST_CASE("deep water: a short dip at the edge does not flip the mode") {
    WaterBench b(60.0f);
    // In for fewer frames than the hysteresis, then out again: no change.
    b.kart.pos.z = 100.5f;
    b.kart.sample = b.track.nearest_sample(b.kart.pos);
    for (int f = 0; f < b.tables.constants.water_hysteresis - 1; ++f) b.step(KartInput{});
    CHECK(b.kart.water_frames > 0);
    b.kart.pos.z = 90.0f;
    b.kart.sample = b.track.nearest_sample(b.kart.pos);
    b.step(KartInput{});
    CHECK_FALSE(b.kart.underwater);
    CHECK(b.kart.water_frames == 0);
    CHECK(b.count(KartEventType::WaterEnter) == 0);
}

TEST_CASE("deep water: no speed lost going in or out") {
    WaterBench b(80.0f);
    b.params.top_speed_water = b.params.top_speed; // so only the transition could change the speed
    float before = 0.0f;
    bool flipped = false;
    for (int f = 0; f < 600 && !flipped; ++f) {
        before = b.kart.speed;
        flipped = b.step(gas());
    }
    REQUIRE(flipped);
    CHECK(b.kart.speed >= before - 0.01f);
    flipped = false;
    for (int f = 0; f < 900 && !flipped; ++f) {
        before = b.kart.speed;
        flipped = b.step(gas());
    }
    REQUIRE(flipped);
    CHECK(b.kart.speed >= before - 0.01f);
}

TEST_CASE("deep water: the water stats cap the speed, and the floor still counts") {
    WaterBench b(110.0f);
    b.params.top_speed_water = b.params.top_speed * 0.8f;
    b.kart.underwater = true;
    for (int f = 0; f < 240; ++f) {
        b.step(gas());
        b.kart.pos.z = 110.0f; // stay in the water: run on the spot
        b.kart.sample = b.track.nearest_sample(b.kart.pos);
    }
    REQUIRE(b.kart.underwater);
    CHECK(b.kart.speed == doctest::Approx(b.params.top_speed_water).epsilon(0.02));
    // Out of the water the full top speed comes back.
    WaterBench dry(-50.0f);
    for (int f = 0; f < 300; ++f) dry.step(gas());
    CHECK_FALSE(dry.kart.underwater);
    CHECK(dry.kart.speed == doctest::Approx(dry.params.top_speed).epsilon(0.02));
    // An off-road floor under the water still slows the kart.
    Track grass = test::flat_track(Surface::OffroadMedium, -50.0f, 50.0f);
    grass.zones = wet_track().zones;
    WaterBench slow(110.0f, std::move(grass));
    slow.params.top_speed_water = slow.params.top_speed * 0.8f;
    slow.kart.underwater = true;
    for (int f = 0; f < 240; ++f) {
        slow.step(gas());
        slow.kart.pos.z = 110.0f;
        slow.kart.sample = slow.track.nearest_sample(slow.kart.pos);
    }
    CHECK(slow.kart.speed < b.kart.speed * 0.9f);
}

TEST_CASE("deep water: the glider folds away on the way in, and falls are slow") {
    WaterBench b(110.0f);
    b.kart.pos.y = 1.5f;
    b.kart.grounded = false;
    b.kart.gliding = true;
    b.kart.air_vel = {0.0f, 0.0f, 20.0f};
    for (int f = 0; f < 6; ++f) b.step(KartInput{});
    REQUIRE(b.kart.underwater);
    CHECK_FALSE(b.kart.gliding);
    CHECK(b.count(KartEventType::GliderClose) == 1);
    // Sinking: never faster than the cap.
    b.kart.pos.y = 2.4f;
    b.kart.air_vel = {0.0f, 0.0f, 0.0f};
    float fastest = 0.0f;
    for (int f = 0; f < 30 && !b.kart.grounded; ++f) {
        b.step(KartInput{});
        if (!b.kart.grounded) fastest = std::max(fastest, -b.kart.air_vel.y);
    }
    CHECK(fastest <= b.tables.constants.water_terminal_fall + 1e-3f);
    CHECK(fastest > 0.0f);
}

TEST_CASE("deep water: the mode rides in snapshots") {
    KartState k;
    k.underwater = true;
    k.water_frames = 4;
    net::SnapshotMsg m;
    m.karts.push_back({k, KartInput{}});
    net::SnapshotMsg back;
    REQUIRE(net::decode(net::encode(m), back));
    REQUIRE(back.karts.size() == 1);
    CHECK(back.karts[0].state.underwater);
    CHECK(back.karts[0].state.water_frames == 4);
}
