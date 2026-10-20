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

// Flat road that steps down by `drop` metres at z = 0: a plain ledge, no
// ramp or launch surface.
Track ledge_track(float drop) {
    Track t;
    t.name = "ledge";
    const float e = 2000.0f;
    auto add_quad = [&](float y, float z0, float z1) {
        const uint32_t b = uint32_t(t.vertices.size());
        t.vertices.push_back({-e, y, z0});
        t.vertices.push_back({-e, y, z1});
        t.vertices.push_back({e, y, z1});
        t.vertices.push_back({e, y, z0});
        t.triangles.push_back({b, b + 1, b + 2, Surface::Road});
        t.triangles.push_back({b, b + 2, b + 3, Surface::Road});
    };
    add_quad(drop, -e, 0.0f);
    add_quad(0.0f, 0.0f, e);
    for (int i = 0; i < 1000; ++i) {
        CenterSample c;
        c.pos = {0, i * 2.0f < 100.0f ? drop : 0.0f, float(i) * 2.0f - 100.0f};
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

// Drives off the ledge, pressing drift once in the air while the trick
// window is open. Returns whether the window opened.
bool trick_off_ledge(Bench& b) {
    bool opened = false, pressed = false;
    for (int f = 0; f < 60 * 6; ++f) {
        const bool press = !b.kart.grounded && b.kart.trick_window > 0 && !pressed;
        opened |= b.kart.trick_window > 0;
        pressed |= press;
        b.step(gas(0.0f, press));
    }
    return opened;
}

} // namespace

TEST_CASE("stat points sum to Levels clamped to 0..20") {
    auto parts = test::parts();
    const StatArray lv = parts.levels({4, 2, 3, 0}); // Brute, Mud Buggy, Monster, Wing (Meowie sits at 2)
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
    // The charge started in the air, past the top of the hop.
    const float pre = b.kart.mt_charge;
    CHECK(pre > 0.0f);
    const int to_blue = int(std::ceil((k.mt_thresholds[0] - pre) / rate));
    b.step(gas(1.0f, true), to_blue - 1);
    CHECK(b.kart.mt_tier(k) == 0);
    b.step(gas(1.0f, true), 1);
    CHECK(b.kart.mt_tier(k) == 1);
    const int to_orange = int(std::ceil((k.mt_thresholds[1] - pre) / rate));
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

namespace {
// Drops the kart from `height` onto flat road and returns the frames from
// touch-down, with the largest compression seen.
struct Drop {
    int land_frame = -1;
    float peak = 0.0f;
    int settle_frames = -1; // after landing, until the body stays within 1 cm and 0.01 rad
    bool wobbled = false;
};
Drop drop(float height, float weight01) {
    Bench b(test::flat_track(), {0, height, 0});
    b.params.weight01 = weight01;
    b.kart.grounded = false;
    Drop d;
    int still = 0;
    for (int f = 0; f < 400; ++f) {
        b.step(KartInput{});
        if (d.land_frame < 0) {
            if (b.kart.grounded) d.land_frame = f;
            else continue;
        }
        d.peak = std::max(d.peak, b.kart.susp_y);
        d.wobbled = d.wobbled || b.kart.susp_wobble > 0;
        const bool calm = std::abs(b.kart.susp_y) < 0.01f && std::abs(b.kart.susp_pitch) < 0.01f && std::abs(b.kart.susp_vy) < 0.1f;
        still = calm ? still + 1 : 0;
        if (still == 10 && d.settle_frames < 0) d.settle_frames = f - d.land_frame - 9;
    }
    return d;
}
} // namespace

TEST_CASE("suspension: a landing bounces in proportion to the fall and settles in about half a second") {
    const Drop low = drop(0.6f, 0.5f), high = drop(2.5f, 0.5f);
    REQUIRE(low.land_frame >= 0);
    REQUIRE(high.land_frame >= 0);
    CHECK(low.peak > 0.02f);          // a hop's landing is visible
    CHECK(high.peak > low.peak * 1.5f); // a bigger fall squashes harder
    CHECK(high.peak <= test::tables().constants.susp_max_compress + 1e-4f);
    CHECK(low.settle_frames > 5);     // it bounces, it doesn't just snap back
    CHECK(low.settle_frames < 45);    // and is calm again within ~0.75 s
    CHECK(high.settle_frames < 60);
    CHECK_FALSE(low.wobbled);
}

TEST_CASE("suspension: heavier karts bounce lower and settle slower; a hard landing wobbles the steering") {
    const Drop light = drop(1.5f, 0.0f), heavy = drop(1.5f, 1.0f);
    CHECK(heavy.peak < light.peak);
    CHECK(heavy.settle_frames > light.settle_frames);
    const Drop big = drop(9.0f, 0.5f);
    CHECK(big.wobbled);

    // While wobbling the kart turns less for the same stick.
    Bench a(test::flat_track()), w(test::flat_track());
    a.step(gas(), 120);
    w.kart = a.kart;
    w.kart.susp_wobble = 8;
    a.step(gas(1.0f), 4);
    w.step(gas(1.0f), 4);
    const float turned_a = std::acos(std::clamp(a.kart.forward().z, -1.0f, 1.0f));
    const float turned_w = std::acos(std::clamp(w.kart.forward().z, -1.0f, 1.0f));
    CHECK(turned_w < turned_a * 0.9f);
}

TEST_CASE("suspension: pitches under acceleration and braking, rolls out of turns, and is deterministic") {
    Bench b(test::flat_track());
    float max_pitch = 0.0f;
    for (int f = 0; f < 20; ++f) b.step(gas()), max_pitch = std::max(max_pitch, b.kart.susp_pitch);
    CHECK(max_pitch > 0.02f); // nose up pulling away
    b.step(gas(), 120);
    float min_pitch = 0.0f;
    KartInput brake;
    brake.brake = true;
    for (int f = 0; f < 20; ++f) b.step(brake), min_pitch = std::min(min_pitch, b.kart.susp_pitch);
    CHECK(min_pitch < -0.02f); // nose down braking

    Bench t(test::flat_track());
    t.step(gas(), 120);
    float min_roll = 0.0f;
    for (int f = 0; f < 40; ++f) t.step(gas(1.0f)), min_roll = std::min(min_roll, t.kart.susp_roll);
    CHECK(min_roll < -0.02f); // a right turn leans the body left (out of the turn)

    // Same inputs, same body motion, bit for bit.
    auto run = [] {
        Bench r(test::flat_track(), {0, 1.2f, 0});
        r.kart.grounded = false;
        for (int f = 0; f < 300; ++f) r.step(gas(std::sin(float(f) * 0.05f), (f / 40) % 2 == 1));
        return r.kart;
    };
    const KartState x = run(), y = run();
    CHECK(x.susp_y == y.susp_y);
    CHECK(x.susp_vy == y.susp_vy);
    CHECK(x.susp_pitch == y.susp_pitch);
    CHECK(x.susp_roll == y.susp_roll);
    CHECK(x.susp_roll_v == y.susp_roll_v);
    CHECK(x.pos == y.pos);
}

TEST_CASE("two wheels: a hard corner tips the kart onto its outside wheels, more for faster and lighter karts") {
    // Peak tilt (radians, + right side down) over a second of the given input.
    auto corner = [](float weight, EngineClass cc, KartInput in, int8_t* drift_dir = nullptr) {
        Bench b(test::flat_track());
        b.params = test::default_params(b.tables, b.parts, {1, 0, 0, 0}, cc);
        b.params.weight01 = weight;
        b.step(gas(), 150);
        float peak = 0.0f;
        for (int f = 0; f < 60; ++f) {
            b.step(in);
            if (std::abs(b.kart.tilt) > std::abs(peak)) peak = b.kart.tilt;
        }
        if (drift_dir) *drift_dir = b.kart.drift_dir;
        return peak;
    };
    const float k = test::tables().constants.tilt_max;
    CHECK(corner(0.5f, EngineClass::cc150, gas(0.5f)) == 0.0f);   // ordinary steering stays on four wheels
    CHECK(corner(0.5f, EngineClass::cc50, gas(1.0f)) == 0.0f);    // and so does a slow full lock
    int8_t dir = 0;
    const float tight = corner(0.5f, EngineClass::cc150, gas(1.0f, true), &dir);
    REQUIRE(dir == 1);
    CHECK(tight < -0.1f);                // a tight right drift lifts the right (inside) wheels
    CHECK(tight >= -k);
    CHECK(corner(0.5f, EngineClass::cc150, gas(-1.0f, true)) > 0.1f); // and a left one the left
    CHECK(corner(0.5f, EngineClass::cc200, gas(1.0f, true)) < tight); // faster, further up
    CHECK(corner(0.0f, EngineClass::cc150, gas(1.0f, true)) < corner(1.0f, EngineClass::cc150, gas(1.0f, true)));

    // Straightening out, the wheels come down and the kart sits flat again.
    Bench b(test::flat_track());
    b.params = test::default_params(b.tables, b.parts, {1, 0, 0, 0}, EngineClass::cc200);
    b.step(gas(), 150);
    b.step(gas(1.0f, true), 60);
    REQUIRE(b.kart.tilt < -0.1f);
    b.step(gas(), 60);
    CHECK(std::abs(b.kart.tilt) < 0.01f);
    CHECK(b.kart.tilt <= 0.0f); // never tipped past flat onto the other side
}

TEST_CASE("a trick works off any ledge, but only pays out after real air") {
    Bench high(ledge_track(2.0f), {0, 2.0f, -80.0f});
    REQUIRE(trick_off_ledge(high));
    CHECK(high.count(KartEventType::Trick) == 1);
    int boosts = 0;
    for (const auto& e : high.events) boosts += e.type == KartEventType::Boost && e.value == int(BoostKind::Trick);
    CHECK(boosts == 1);

    // A lip barely below the road: the trick plays, but there's no boost.
    Bench low(ledge_track(0.12f), {0, 0.12f, -80.0f});
    trick_off_ledge(low);
    for (const auto& e : low.events) CHECK_FALSE((e.type == KartEventType::Boost && e.value == int(BoostKind::Trick)));
}

TEST_CASE("the mini-turbo starts charging past the top of the drift hop, not on landing") {
    Bench b(test::flat_track());
    b.step(gas(), 60 * 3);
    b.step(gas(1.0f, true), 1); // hop
    int hop_frames = 1, first_charge = -1;
    while (!b.kart.grounded && hop_frames < 60) {
        b.step(gas(1.0f, true));
        ++hop_frames;
        if (first_charge < 0 && b.kart.mt_charge > 0.0f) first_charge = hop_frames;
    }
    REQUIRE(first_charge > 0);
    MESSAGE("hop ", hop_frames, " frames, charging from frame ", first_charge);
    CHECK(first_charge <= hop_frames / 2 + 2); // about half way: ~0.13 s after the press
    CHECK(first_charge < hop_frames);           // before the wheels are down

    // A hop without the stick held over charges nothing.
    Bench c(test::flat_track());
    c.step(gas(), 60 * 3);
    c.step(gas(0.0f, true), 1);
    while (!c.kart.grounded) c.step(gas(0.0f, true));
    CHECK(c.kart.mt_charge == 0.0f);

    // Charged one way in the air, landed into a drift the other way: it starts over.
    Bench d(test::flat_track());
    d.step(gas(), 60 * 3);
    d.step(gas(1.0f, true), 1);
    while (!d.kart.grounded && d.kart.mt_charge == 0.0f) d.step(gas(1.0f, true));
    REQUIRE(!d.kart.grounded);
    while (!d.kart.grounded) d.step(gas(-1.0f, true));
    REQUIRE(d.kart.drift == DriftState::Drifting);
    CHECK(d.kart.drift_dir == -1);
    CHECK(d.kart.mt_charge < 10.0f * d.tables.constants.charge_fast * d.params.mt_charge);
}

namespace {

// Puts the kart in the air `height` above the flat road, flying on at 20 m/s.
void fly(Bench& b, float height, float rise) {
    b.kart.pos = {0.0f, height, 0.0f};
    b.kart.grounded = false;
    b.kart.speed = 20.0f;
    b.kart.air_vel = {0.0f, rise, 20.0f};
    b.kart.air_frames = 3;
    b.kart.trick_window = 0;
}

int tricks(const Bench& b) { return b.count(KartEventType::Trick); }
int trick_boosts(const Bench& b) {
    int n = 0;
    for (const auto& e : b.events) n += e.type == KartEventType::Boost && e.value == int(BoostKind::Trick);
    return n;
}

} // namespace

TEST_CASE("a trick works anywhere in the air close above the road, and chains 150 ms apart") {
    Bench b(test::flat_track());
    fly(b, 1.0f, 8.0f);
    const int gap = b.tables.constants.trick_chain_frames;
    REQUIRE(gap == 9);
    b.step(gas(0.0f, true));  // trick
    CHECK(tricks(b) == 1);
    b.step(gas(), 2);
    b.step(gas(0.0f, true));  // too soon after the first
    CHECK(tricks(b) == 1);
    b.step(gas(), gap - 4);
    b.step(gas(0.0f, true));  // a moment later: a second one
    CHECK(tricks(b) == 2);
    for (int f = 0; f < 120 && !b.kart.grounded; ++f) b.step(gas());
    REQUIRE(b.kart.grounded);
    CHECK(trick_boosts(b) == 1); // one boost for the chain
}

TEST_CASE("no trick of its own high above the road, gliding, under water or on a drift hop") {
    Bench high(test::flat_track());
    fly(high, 10.0f, 0.0f);
    high.step(gas(0.0f, true));
    CHECK(tricks(high) == 0);

    Bench glide(test::flat_track());
    fly(glide, 1.5f, 0.0f);
    glide.kart.gliding = true;
    glide.step(gas(0.0f, true));
    CHECK(tricks(glide) == 0);

    Bench water(test::flat_track());
    fly(water, 1.5f, 0.0f);
    water.kart.underwater = true;
    water.step(gas(0.0f, true));
    CHECK(tricks(water) == 0);

    Bench hop(test::flat_track());
    hop.step(gas(), 60 * 2);
    hop.step(gas(0.0f, true)); // the hop
    hop.step(gas());
    REQUIRE_FALSE(hop.kart.grounded);
    hop.step(gas(0.0f, true)); // pressed again mid-hop
    CHECK(tricks(hop) == 0);
}

namespace {

// Drives a snake down the straight: hop into a drift one way with the stick
// over, counter-steer through the charge so the kart runs nearly straight,
// release at `tier`, and hop the other way. The first drift is cut short at
// `setup` radians of heading so the snake runs symmetric about the centre
// line, as a player lines one up. Returns how far it got.
struct Snake {
    float z = 0.0f, max_x = 0.0f, swing = 0.0f;
    int boosts = 0, drifts = 0;
};
Snake snake(int seconds, int tier, float counter = -1.0f, bool straight_only = false, float wide = 0.0f, float setup = 0.0f) {
    Bench b(test::flat_track());
    if (wide > 0.0f) b.tables.constants.drift_turn_wide = wide;
    b.step(gas(), 60 * 3);
    const float z0 = b.kart.pos.z;
    int dir = -1;
    bool lining_up = true;
    float last_release = 0.0f;
    Snake r;
    auto heading = [&] { const vec3 fw = b.kart.forward(); return std::atan2(fw.x, fw.z); };
    for (int f = 0; f < seconds * 60; ++f) {
        KartInput in = gas();
        if (!straight_only) {
            const KartState& k = b.kart;
            if (k.drift == DriftState::None && k.grounded) {
                in = gas(float(dir), true); // hop, stick over to start the drift
                ++r.drifts;
            } else if (k.drift == DriftState::Hop) {
                in = gas(float(dir), true);
            } else if (k.drift == DriftState::Drifting) {
                const bool done = lining_up ? std::abs(heading()) >= setup : k.mt_tier(b.tables.constants) >= tier;
                if (done) { // release, next one the other way
                    in = gas(0.0f, false);
                    dir = -dir;
                    if (!lining_up && r.swing == 0.0f && last_release != 0.0f) r.swing = std::abs(heading() - last_release);
                    last_release = lining_up ? 0.0f : heading();
                    lining_up = false;
                } else {
                    in = gas(float(dir) * counter, true);
                }
            }
        }
        b.step(in);
        r.max_x = std::max(r.max_x, std::abs(b.kart.pos.x));
    }
    r.z = b.kart.pos.z - z0;
    r.boosts = b.count(KartEventType::Boost);
    return r;
}
// A snake lined up so its heading swings evenly either side of straight.
Snake even_snake(int seconds, int tier, float counter = -1.0f, float wide = 0.0f) {
    const Snake rough = snake(seconds, tier, counter, false, wide, 0.0f);
    return snake(seconds, tier, counter, false, wide, rough.swing * 0.5f);
}

} // namespace

TEST_CASE("snaking: alternating counter-steered drifts on a straight earn mini-turbos and gain on flat out") {
    const Snake flat = snake(15, 1, -1.0f, true);
    const Snake blue = even_snake(15, 1);
    const Snake blue_half = even_snake(15, 1, -0.5f);
    const Snake blue_in = even_snake(15, 1, 0.0f);
    const Snake orange = even_snake(15, 2);
    for (float wide : {0.12f, 0.08f, 0.04f}) {
        const Snake w = even_snake(15, 1, -1.0f, wide);
        MESSAGE("wide " << wide << ": " << w.z << " m, " << w.boosts << " boosts, max x " << w.max_x << ", swing " << w.swing);
    }
    MESSAGE("flat out " << flat.z << " m; blue snake " << blue.z << " m (" << blue.boosts << " boosts, " << blue.drifts << " drifts, max x " << blue.max_x
                        << " m, swing " << blue.swing << "); half counter " << blue_half.z << " m (x " << blue_half.max_x << "); stick centred "
                        << blue_in.z << " m (x " << blue_in.max_x << "); orange " << orange.z << " m (" << orange.boosts << " boosts, x " << orange.max_x << ")");
    CHECK(blue.boosts >= 4);
    CHECK(blue.max_x < 5.0f);       // stays on a 14 m road
    CHECK(blue.z > flat.z * 1.01f); // and it pays, a little
    CHECK(blue.z < flat.z * 1.08f); // but not so much that a straight is a drift test
}
