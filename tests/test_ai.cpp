// CPU drivers: the racing line, lap pace by difficulty, drifting, tricks,
// item use and rubber banding.
#include "sim_fixture.h"

#include "gk/sim/ai.h"

#include <algorithm>

using namespace gk;
using namespace gk::sim;

namespace {

struct LapRun {
    float seconds = 0.0f;
    int mini_turbos = 0, tricks = 0, respawns = 0, walls = 0;
};

// One CPU alone on the greybox loop, items off.
LapRun solo_lap(CpuLevel level, Track track, int seat = 0) {
    auto t = test::tables();
    auto c = test::parts();
    World w(std::move(track), t, RaceConfig{.laps = 3, .countdown_frames = 1, .items = false});
    w.add_kart(test::default_params(t, c));
    LapRun r;
    int frames = 0;
    // Start from a rolling lap: the first lap begins from the grid.
    while (w.karts()[0].lap < 3 && frames < 60 * 240) {
        KartInput in = cpu_input(w, 0, cpu_skill(level, seat));
        w.tick(std::span(&in, 1));
        if (w.karts()[0].lap == 2) {
            ++frames;
            for (const auto& e : w.events()) {
                if (e.type == KartEventType::Boost && (e.value == int(BoostKind::MiniTurbo) || e.value == int(BoostKind::SuperMiniTurbo) ||
                                                       e.value == int(BoostKind::UltraMiniTurbo)))
                    ++r.mini_turbos;
                r.tricks += e.type == KartEventType::Trick;
                r.respawns += e.type == KartEventType::Respawn;
                r.walls += e.type == KartEventType::WallHit;
            }
        }
    }
    r.seconds = float(frames) / 60.0f;
    return r;
}

} // namespace

TEST_CASE("the racing line stays on the road and cuts corners") {
    const Track t = make_greybox_track();
    REQUIRE(t.line.size() == t.center.size());
    CHECK(t.closed);
    float max_off = 0.0f;
    for (size_t i = 0; i < t.line.size(); ++i) {
        const auto& lp = t.line[i];
        CHECK(lp.offset >= lp.lo - 1e-3f);
        CHECK(lp.offset <= lp.hi + 1e-3f);
        if (!t.center[i].has_floor) CHECK(lp.offset == 0.0f);
        max_off = std::max(max_off, std::abs(lp.offset));
    }
    CHECK(max_off > 2.0f); // it does use the width of the road
    // Shorter than the centre line.
    float len = 0.0f;
    for (size_t i = 0; i < t.line.size(); ++i) len += glm::length(t.line_point(int(i) + 1) - t.line_point(int(i)));
    MESSAGE("centre " << t.length << " m, racing line " << len << " m");
    CHECK(len < t.length);
    // Smooth: no zig-zag between neighbouring samples (second difference).
    float kink = 0.0f;
    for (int i = 0; i < int(t.line.size()); ++i)
        kink = std::max(kink, std::abs(t.line[size_t(t.wrap(i - 1))].offset - 2.0f * t.line[size_t(i)].offset +
                                       t.line[size_t(t.wrap(i + 1))].offset));
    MESSAGE("largest kink " << kink << " m");
    CHECK(kink < 0.5f);
}

TEST_CASE("CPUs lap the greybox loop cleanly, harder ones faster, with tricks") {
    const LapRun easy = solo_lap(CpuLevel::Easy, make_greybox_track());
    const LapRun normal = solo_lap(CpuLevel::Normal, make_greybox_track());
    const LapRun hard = solo_lap(CpuLevel::Hard, make_greybox_track());
    MESSAGE("easy " << easy.seconds << " s, normal " << normal.seconds << " s (" << normal.mini_turbos << " MT), hard "
                    << hard.seconds << " s (" << hard.mini_turbos << " MT, " << hard.tricks << " tricks, " << hard.walls
                    << " wall hits)");
    for (const LapRun* r : {&easy, &normal, &hard}) {
        CHECK(r->seconds > 25.0f);
        CHECK(r->seconds < 40.0f);
        CHECK(r->respawns == 0);
    }
    CHECK(hard.seconds <= normal.seconds);
    CHECK(normal.seconds < easy.seconds);
    CHECK(hard.tricks >= 1);
}

TEST_CASE("CPUs drift through hairpins for mini-turbos") {
    const LapRun easy = solo_lap(CpuLevel::Easy, test::stadium_track());
    const LapRun hard = solo_lap(CpuLevel::Hard, test::stadium_track());
    MESSAGE("stadium: easy " << easy.seconds << " s, hard " << hard.seconds << " s with " << hard.mini_turbos
                             << " mini-turbos");
    CHECK(hard.respawns == 0);
    CHECK(easy.respawns == 0);
    CHECK(hard.mini_turbos >= 2);
    CHECK(easy.mini_turbos == 0);
    CHECK(hard.seconds < easy.seconds);
}

TEST_CASE("a full race of 12 CPUs with items finishes") {
    auto t = test::tables();
    auto c = test::parts();
    World w(make_greybox_track(), t, RaceConfig{.laps = 3, .countdown_frames = 180});
    for (int i = 0; i < 12; ++i) {
        KartParams p = test::default_params(t, c);
        p.rubber_band = cpu_rubber_band(CpuLevel::Normal);
        w.add_kart(p);
    }
    std::vector<KartInput> in(12);
    int uses = 0, hits = 0, boxes = 0, rockets = 0;
    int frames = 0;
    auto all_done = [&] {
        return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; });
    };
    while (!all_done() && frames < 60 * 60 * 5) {
        for (int i = 0; i < 12; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(CpuLevel::Normal, i));
        w.tick(in);
        ++frames;
        for (const auto& e : w.events()) {
            uses += e.type == KartEventType::ItemUse;
            hits += e.type == KartEventType::Hit;
            boxes += e.type == KartEventType::ItemBox;
            rockets += e.type == KartEventType::Boost && e.value == int(BoostKind::RocketStart);
        }
    }
    MESSAGE("race " << frames / 60.0f << " s: " << boxes << " boxes, " << uses << " throws, " << hits << " hits, "
                    << rockets << " rocket starts");
    CHECK(all_done());
    CHECK(boxes > 30);
    CHECK(uses > 10);
    CHECK(hits > 3);
    CHECK(rockets >= 6);
}

TEST_CASE("rubber banding pulls CPUs towards the leading player") {
    auto t = test::tables();
    auto c = test::parts();
    auto run = [&](float rb) {
        World w(make_greybox_track(), t, RaceConfig{.laps = 3, .countdown_frames = 1, .items = false});
        w.add_kart(test::default_params(t, c)); // the player, parked ahead
        KartParams p = test::default_params(t, c);
        p.rubber_band = rb;
        w.add_kart(p);
        // The player starts 150 m up the road.
        KartState& player = w.karts_mut()[0];
        const int s = w.track().wrap(75);
        player.pos = w.track().center[size_t(s)].pos + vec3(0, 0.5f, 0);
        player.sample = s;
        std::vector<KartInput> in(2);
        float top = 0.0f;
        for (int f = 0; f < 60 * 6; ++f) {
            in[1] = cpu_input(w, 1, cpu_skill(CpuLevel::Hard, 1));
            w.tick(in);
            top = std::max(top, w.karts()[1].speed);
        }
        return top;
    };
    const float plain = run(0.0001f), banded = run(1.0f);
    MESSAGE("top speed " << plain << " vs " << banded << " m/s");
    CHECK(banded > plain * 1.04f);
}

TEST_CASE("the CPU driver is a pure function of the world state") {
    World w = test::racing_world();
    for (int f = 0; f < 400; ++f) {
        KartInput in = cpu_input(w, 0, cpu_skill(CpuLevel::Hard, 0));
        w.tick(std::span(&in, 1));
    }
    const WorldState s = w.save();
    const KartInput a = cpu_input(w, 0, cpu_skill(CpuLevel::Hard, 0));
    w.restore(s);
    const KartInput b = cpu_input(w, 0, cpu_skill(CpuLevel::Hard, 0));
    CHECK(a == b);
}

namespace {

// Two CPUs on the flat straight, `front` metres apart, items on.
struct Duel {
    World world;
    explicit Duel(float gap)
        : world(test::flat_track(), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1}) {
        const auto parts = test::parts();
        for (int i = 0; i < 2; ++i) world.add_kart(test::default_params(world.tables(), parts));
        std::vector<KartInput> none(2);
        world.tick(none);
        place(0, 300.0f + gap);
        place(1, 300.0f);
    }
    void place(int i, float z) {
        KartState& k = world.karts_mut()[size_t(i)];
        k.pos = {0.0f, 0.0f, z};
        k.rot = quat(1, 0, 0, 0);
        k.speed = 20.0f;
        k.sample = world.track().nearest_sample(k.pos);
    }
    std::vector<KartEvent> run(int ticks) {
        std::vector<KartEvent> all;
        std::vector<KartInput> in(2);
        for (int t = 0; t < ticks; ++t) {
            for (int i = 0; i < 2; ++i) in[size_t(i)] = cpu_input(world, i, cpu_skill(CpuLevel::Hard, i));
            world.tick(in);
            all.insert(all.end(), world.events().begin(), world.events().end());
        }
        return all;
    }
};

bool used(const std::vector<KartEvent>& ev, int kart, Item item) {
    return std::any_of(ev.begin(), ev.end(), [&](const KartEvent& e) {
        return e.type == KartEventType::ItemUse && e.kart == kart && e.value == int(item);
    });
}

} // namespace

TEST_CASE("CPUs fire shells at the kart ahead and keep bananas as shields") {
    Duel d(25.0f);
    d.world.give_item(1, Item::GreenShell);
    d.world.give_item(0, Item::Banana);
    auto ev = d.run(60);
    CHECK(used(ev, 1, Item::GreenShell));
    CHECK(d.world.karts()[0].trailing); // nobody close behind: hold it

    Duel r(60.0f);
    r.world.give_item(1, Item::RedShell);
    ev = r.run(60);
    CHECK(used(ev, 1, Item::RedShell));
}

TEST_CASE("CPUs in front save mushrooms for straights and use stars at once") {
    Duel d(25.0f);
    d.world.give_item(0, Item::Mushroom);
    d.world.give_item(1, Item::Star);
    d.run(60);
    CHECK(d.world.karts()[0].item == Item::None); // a straight: boost now
    CHECK(d.world.karts()[1].star_frames > 0);
}
