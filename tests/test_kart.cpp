#include "sim_fixture.h"

#include "gk/core/fixed_step.h"

#include <cmath>

using namespace gk;
using namespace gk::sim;

namespace {

struct Bench {
    PhysicsTables tables = test::tables();
    PartsCatalog parts = test::parts();
    KartParams params = test::default_params(tables, parts);
    Track track;
    KartState kart;
    std::vector<KartEvent> events;

    explicit Bench(Track t, vec3 start = {0, 0, 0}) : track(std::move(t)), kart(test::kart_at(start)) {}

    void step(const KartInput& in, int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            KartStepContext ctx{track, tables.constants, params, 0, false, &events};
            kart_step(kart, in, ctx);
        }
    }
    int count(KartEventType type) const {
        int n = 0;
        for (const auto& e : events) n += e.type == type;
        return n;
    }
};

KartInput gas(float steer = 0.0f, bool drift = false) {
    KartInput in;
    in.accelerate = true;
    in.steer = steer;
    in.drift = drift;
    return in;
}

} // namespace

TEST_CASE("stat points sum to Levels clamped to 0..20") {
    auto parts = test::parts();
    const StatArray lv = parts.levels({3, 2, 3, 0}); // Brute, Mud Buggy, Monster, Wing
    CHECK(lv[int(Stat::Weight)] == 18);
    CHECK(lv[int(Stat::Acceleration)] == 4);
    CHECK(menu_value(0) == doctest::Approx(0.75f));
    CHECK(menu_value(20) == doctest::Approx(5.75f));
    for (int s = 0; s < kStatCount; ++s) {
        CHECK(lv[size_t(s)] >= 0);
        CHECK(lv[size_t(s)] <= kMaxLevel);
    }
}

TEST_CASE("datamined acceleration curve: Drive_AccLmtFwd at Level 1, 10, 20") {
    auto t = test::tables();
    const auto& c = t.curves[int(Stat::Acceleration)];
    CHECK(c.at(1) / c.scale == doctest::Approx(0.035f));
    CHECK(c.at(10) / c.scale == doctest::Approx(0.08f));
    CHECK(c.at(20) / c.scale == doctest::Approx(0.13f));
}

TEST_CASE("kart reaches its top speed on road and holds it") {
    Bench b(test::flat_track());
    b.step(gas(), 60 * 8);
    CHECK(b.kart.grounded);
    CHECK(b.kart.speed == doctest::Approx(b.params.top_speed).epsilon(0.01));
    CHECK(b.kart.pos.z > 100.0f);
}

TEST_CASE("higher acceleration Level reaches 90% of top speed sooner") {
    auto frames_to_90 = [](Build build) {
        Bench b(test::flat_track());
        b.params = test::default_params(b.tables, b.parts, build);
        int f = 0;
        while (b.kart.speed < 0.9f * b.params.top_speed && f < 2000) {
            b.step(gas());
            ++f;
        }
        return f;
    };
    const int light = frames_to_90({0, 1, 1, 1}); // Pip, Pipe Frame, Roller, Parafoil
    const int heavy = frames_to_90({3, 2, 3, 0}); // Brute, Mud Buggy, Monster, Wing
    CHECK(light < heavy);
}

TEST_CASE("off-road classes cap speed at BrakeRt_DIRT multipliers") {
    struct Case { Surface s; float mult; };
    for (Case c : {Case{Surface::OffroadLight, 0.7f}, Case{Surface::OffroadMedium, 0.5f}, Case{Surface::OffroadHeavy, 0.3f}}) {
        CAPTURE(surface_name(c.s));
        Bench b(test::flat_track(c.s, -50.0f, 50.0f));
        b.step(gas(), 60 * 8);
        CHECK(b.kart.surface == c.s);
        CHECK(b.kart.speed == doctest::Approx(b.params.top_speed * c.mult).epsilon(0.02));
    }
}

TEST_CASE("a boost ignores off-road slowdown") {
    Bench b(test::flat_track(Surface::OffroadHeavy, -50.0f, 50.0f));
    b.step(gas(), 60 * 4);
    const float slow = b.kart.speed;
    kart_give_boost(b.kart, BoostKind::Mushroom, 90, b.tables.constants.mushroom_mult, true);
    b.step(gas(), 60);
    CHECK(b.kart.speed > slow * 2.5f);
}

TEST_CASE("mini-turbo charge: 5 units per frame with the stick into the turn, tiers at the thresholds") {
    Bench b(test::flat_track());
    b.step(gas(), 60 * 3);
    b.step(gas(1.0f, true), 1);         // hop
    int hop_frames = 0;
    while (!b.kart.grounded && hop_frames < 60) {
        b.step(gas(1.0f, true));
        ++hop_frames;
    }
    REQUIRE(b.kart.drift == DriftState::Drifting);
    CHECK(b.kart.drift_dir == 1);
    const auto& k = b.tables.constants;
    const float rate = k.charge_fast * b.params.mt_charge;
    const int to_blue = int(std::ceil(k.mt_thresholds[0] / rate));
    b.step(gas(1.0f, true), to_blue - 1);
    CHECK(b.kart.mt_tier(k) == 0);
    b.step(gas(1.0f, true), 1);
    CHECK(b.kart.mt_tier(k) == 1);
    const int to_orange = int(std::ceil(k.mt_thresholds[1] / rate));
    b.step(gas(1.0f, true), to_orange - to_blue);
    CHECK(b.kart.mt_tier(k) == 2);
    b.step(gas(1.0f), 1); // release
    CHECK(b.kart.drift == DriftState::None);
    CHECK(b.kart.boost == BoostKind::SuperMiniTurbo);
    CHECK(b.kart.boost_frames == int(std::round(float(k.mt_frames[1]) * b.params.mt_boost)));
}

TEST_CASE("stick away from the turn charges at the slow rate") {
    Bench b(test::flat_track());
    b.step(gas(), 60 * 3);
    b.step(gas(-1.0f, true), 1);
    while (!b.kart.grounded) b.step(gas(-1.0f, true));
    REQUIRE(b.kart.drift == DriftState::Drifting);
    const float before = b.kart.mt_charge;
    b.step(gas(0.2f, true), 10); // relative steer -0.2: outward
    CHECK(b.kart.mt_charge - before == doctest::Approx(10 * b.tables.constants.charge_slow * b.params.mt_charge));
}

TEST_CASE("a drift starts by carrying the old direction, then settles into a tighter arc") {
    Bench b(test::flat_track());
    b.step(gas(), 60 * 3);
    b.step(gas(1.0f, true), 1);
    while (!b.kart.grounded) b.step(gas(1.0f, true));
    REQUIRE(b.kart.drift == DriftState::Drifting);
    const auto& k = b.tables.constants;
    auto heading = [&] { const vec3 f = b.kart.forward(); return std::atan2(f.x, f.z); };
    auto travel = [&] { const vec3 v = b.kart.velocity(); return std::atan2(v.x, v.z); };
    auto wrap = [](float a) { while (a > kPi) a -= 2 * kPi; while (a < -kPi) a += 2 * kPi; return a; };
    // Over the entry the nose swings in far more than the path bends.
    const float h0 = heading(), t0 = travel();
    b.step(gas(1.0f, true), k.drift_entry_frames / 2);
    const float nose_early = std::abs(wrap(heading() - h0)), path_early = std::abs(wrap(travel() - t0));
    CHECK(nose_early > 2.0f * path_early);
    // Once settled the path bends faster than it did on entry.
    b.step(gas(1.0f, true), k.drift_entry_frames);
    const float t1 = travel();
    b.step(gas(1.0f, true), k.drift_entry_frames / 2);
    const float path_late = std::abs(wrap(travel() - t1));
    CHECK(path_late > 1.5f * path_early);
}

TEST_CASE("a hop without steering does not start a drift") {
    Bench b(test::flat_track());
    b.step(gas(), 60 * 3);
    b.step(gas(0.0f, true), 1);
    while (!b.kart.grounded) b.step(gas(0.0f, true));
    CHECK(b.kart.drift == DriftState::None);
}

TEST_CASE("braking stops the kart and then reverses") {
    Bench b(test::flat_track());
    b.step(gas(), 120);
    KartInput brake;
    brake.brake = true;
    b.step(brake, 120);
    CHECK(b.kart.speed < 0.0f);
    CHECK(b.kart.speed >= -b.tables.constants.reverse_top - 0.01f);
}

TEST_CASE("ice keeps sideways momentum longer than road") {
    auto slide_after_turn = [](Surface s) {
        Bench b(test::flat_track(s, -50.0f, 50.0f));
        b.step(gas(), 60 * 3);
        b.step(gas(1.0f), 20);
        return glm::length(b.kart.slide);
    };
    CHECK(slide_after_turn(Surface::Ice) > 3.0f * slide_after_turn(Surface::Road));
}
