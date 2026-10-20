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
    int mini_turbos = 0, tricks = 0, respawns = 0, walls = 0, wheelies = 0;
};

// One CPU alone on the greybox loop, items off.
LapRun solo_lap(CpuLevel level, Track track, int seat = 0, Build build = {1, 0, 0, 0}) {
    auto t = test::tables();
    auto c = test::parts();
    World w(std::move(track), t, RaceConfig{.laps = 3, .countdown_frames = 1, .items = false});
    w.add_kart(test::default_params(t, c, build));
    LapRun r;
    int frames = 0;
    // Start from a rolling lap: the first lap begins from the grid.
    while (w.karts()[0].lap < 3 && frames < 60 * 240) {
        KartInput in = cpu_input(w, 0, cpu_skill(level, seat));
        const bool up = w.karts()[0].wheelie_frames > 0;
        w.tick(std::span(&in, 1));
        if (w.karts()[0].lap == 2) {
            ++frames;
            r.wheelies += !up && w.karts()[0].wheelie_frames > 0;
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

TEST_CASE("CPUs on bikes pull wheelies down the straights, and drop them for the corners") {
    // Straights long enough to outlast the mini-turbo out of each hairpin.
    const LapRun bike = solo_lap(CpuLevel::Hard, test::stadium_track(22.0f, 250.0f), 0, {1, 3, 0, 0});
    const LapRun easy = solo_lap(CpuLevel::Easy, test::stadium_track(22.0f, 250.0f), 0, {1, 3, 0, 0});
    MESSAGE("stadium on a bike: hard " << bike.seconds << " s, " << bike.wheelies << " wheelies, " << bike.mini_turbos << " MT; easy "
                                       << easy.seconds << " s, " << easy.wheelies << " wheelies");
    CHECK(bike.wheelies >= 2);
    CHECK(bike.mini_turbos >= 2); // still drifts the hairpins
    CHECK(bike.respawns == 0);
    CHECK(easy.wheelies == 0);
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

namespace {

// The widest a CPU strays from the road's centre in the stadium's bends over
// its second lap. Seat 1: no personal lane offset at any level.
float widest_lateral(CpuLevel level) {
    auto t = test::tables();
    auto c = test::parts();
    World w(test::stadium_track(), t, RaceConfig{.laps = 3, .countdown_frames = 1, .items = false});
    w.add_kart(test::default_params(t, c));
    float widest = 0.0f;
    for (int f = 0; f < 60 * 120 && w.karts()[0].lap < 3; ++f) {
        KartInput in = cpu_input(w, 0, cpu_skill(level, 1));
        w.tick(std::span(&in, 1));
        const KartState& k = w.karts()[0];
        if (k.lap != 2 || std::abs(w.track().line[size_t(k.sample)].curvature) < 0.02f) continue;
        const CenterSample& s = w.track().sample(k.sample);
        widest = std::max(widest, std::abs(glm::dot(k.pos - s.pos, s.right)));
    }
    return widest;
}

} // namespace

TEST_CASE("a hard CPU uses more of the road than an easy one") {
    const float hard = widest_lateral(CpuLevel::Hard), normal = widest_lateral(CpuLevel::Normal), easy = widest_lateral(CpuLevel::Easy);
    MESSAGE("widest from the centre: hard " << hard << " m, normal " << normal << " m, easy " << easy << " m (half width 7)");
    CHECK(hard > normal + 0.3f);
    CHECK(normal > easy + 0.3f);
    CHECK(hard < 6.5f); // and still on the road
}

TEST_CASE("hard and normal CPUs weave on a straight, easy ones hold their lane") {
    auto span = [](CpuLevel level) {
        World w(test::flat_track(), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1, .items = false});
        w.add_kart(test::default_params(w.tables(), test::parts()));
        std::vector<KartInput> in(1);
        w.tick(in);
        KartState& k = w.karts_mut()[0];
        k.pos = {0.0f, 0.0f, 300.0f}; // the grid is at the strip's far end
        k.rot = quat(1, 0, 0, 0);
        k.sample = w.track().nearest_sample(k.pos);
        float lo = 1e9f, hi = -1e9f;
        for (int f = 0; f < 60 * 10; ++f) {
            in[0] = cpu_input(w, 0, cpu_skill(level, 0));
            w.tick(in);
            if (f < 120) continue; // up to speed first
            lo = std::min(lo, w.karts()[0].pos.x), hi = std::max(hi, w.karts()[0].pos.x);
        }
        return hi - lo;
    };
    const float hard = span(CpuLevel::Hard), normal = span(CpuLevel::Normal), easy = span(CpuLevel::Easy);
    MESSAGE("sway across the road over 8 s: hard " << hard << " m, normal " << normal << " m, easy " << easy << " m");
    CHECK(hard > 2.0f);
    CHECK(normal > 1.2f);
    CHECK(easy < 1.0f);
}

TEST_CASE("a hard CPU overtakes a slower kart cleanly") {
    World w(test::flat_track(), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1, .items = false});
    const auto parts = test::parts();
    KartParams slow = test::default_params(w.tables(), parts);
    slow.top_speed *= 0.6f;
    w.add_kart(slow);
    w.add_kart(test::default_params(w.tables(), parts));
    std::vector<KartInput> in(2);
    w.tick(in);
    for (int i = 0; i < 2; ++i) {
        KartState& k = w.karts_mut()[size_t(i)];
        k.pos = {0.0f, 0.0f, i == 0 ? 318.0f : 300.0f};
        k.rot = quat(1, 0, 0, 0);
        k.speed = i == 0 ? 12.0f : 22.0f;
        k.sample = w.track().nearest_sample(k.pos);
    }
    float closest = 1e9f;
    int passed_at = -1, bumps = 0;
    for (int f = 0; f < 60 * 8; ++f) {
        in[0] = KartInput{.accelerate = true};
        in[1] = cpu_input(w, 1, cpu_skill(CpuLevel::Hard, 1));
        w.tick(in);
        const vec3 d = w.karts()[1].pos - w.karts()[0].pos;
        closest = std::min(closest, glm::length(d));
        for (const auto& e : w.events()) bumps += e.type == KartEventType::Hit || e.type == KartEventType::WallHit;
        if (passed_at < 0 && d.z > 3.0f) passed_at = f;
    }
    MESSAGE("passed after " << passed_at / 60.0f << " s, closest " << closest << " m, bumps " << bumps);
    CHECK(passed_at >= 0);
    CHECK(closest > 1.5f); // round it, not through it
    CHECK(bumps == 0);
}

TEST_CASE("CPU slips are seeded from the tick: a normal CPU slips about once a lap, a hard one half as often") {
    World w = test::racing_world();
    std::vector<KartInput> in(1);
    int normal = 0, hard = 0, easy = 0, easy_other = 0;
    CpuMistake was_n = CpuMistake::None, was_h = CpuMistake::None, was_e = CpuMistake::None;
    for (int f = 0; f < 60 * 135; ++f) { // about three laps' worth of ticks
        w.tick(in);
        const CpuMistake n = cpu_mistake(w, 0, cpu_skill(CpuLevel::Normal, 0)), h = cpu_mistake(w, 0, cpu_skill(CpuLevel::Hard, 0)),
                         e = cpu_mistake(w, 0, cpu_skill(CpuLevel::Easy, 0));
        normal += n != CpuMistake::None && n != was_n;
        hard += h != CpuMistake::None && h != was_h;
        easy += e == CpuMistake::WallBrush && e != was_e;
        easy_other += e != CpuMistake::None && e != CpuMistake::WallBrush;
        was_n = n, was_h = h, was_e = e;
    }
    MESSAGE("slips in 135 s: normal " << normal << ", hard " << hard << ", easy " << easy << " wall brushes");
    CHECK(normal >= 1);
    CHECK(normal <= 7);
    CHECK(hard <= normal);
    CHECK(easy_other == 0);
    CHECK(easy >= 1);
    // The same tick gives the same slip for the same kart.
    const WorldState s = w.save();
    const CpuMistake a = cpu_mistake(w, 0, cpu_skill(CpuLevel::Normal, 0));
    w.restore(s);
    CHECK(a == cpu_mistake(w, 0, cpu_skill(CpuLevel::Normal, 0)));
}
