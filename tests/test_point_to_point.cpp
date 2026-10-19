// Point-to-point tracks: an open road from a start line to a finish line,
// raced once and reported in sections instead of laps. The grid forms before
// the line, the finish ends the run, placings go by distance along the road,
// gates read the leader's section, and a ghost replays the run.
#include "sim_fixture.h"

#include "gk/sim/ai.h"
#include "gk/sim/ghost.h"
#include "gk/sim/track_def.h"

using namespace gk;
using namespace gk::sim;

namespace {

// A downhill road 700 m long with a bend in the middle: start 60 m in,
// finish 80 m before the end (the defaults), three equal sections.
TrackDef open_road() {
    TrackDef d;
    d.id = "open_road";
    d.name = "Open Road";
    d.point_to_point = true;
    d.half_width = 8.0f;
    for (vec3 p : {vec3{0, 40, 0}, {0, 36, 100}, {0, 30, 200}, {25, 22, 300}, {0, 14, 400}, {-10, 8, 500}, {0, 3, 600}, {0, 0, 700}})
        d.points.push_back(vec4(p, 0.0f));
    TrackFeature boxes;
    boxes.kind = TrackFeature::Kind::ItemBoxes;
    boxes.at = {2, 0};
    d.features.push_back(boxes);
    GateDef second;
    second.gate.when = Gate::When::Laps;
    second.gate.lap_lo = 2; // "from_section": 2
    d.gates.push_back(second);
    return d;
}

} // namespace

TEST_CASE("an open road has a start line, a finish line and sections") {
    const Track t = build_track(open_road());
    REQUIRE(t.center.size() > 100);
    CHECK_FALSE(t.closed);
    CHECK(t.point_to_point);
    CHECK(t.length == doctest::Approx(700.0f).epsilon(0.05));
    CHECK(t.sections() == 3);
    REQUIRE(t.section_s.size() == 2);
    const float start_s = t.center[size_t(t.start)].s, finish_s = t.center[size_t(t.finish)].s;
    CHECK(start_s == doctest::Approx(60.0f).epsilon(0.05));
    CHECK(finish_s == doctest::Approx(t.length - 80.0f).epsilon(0.05));
    CHECK(t.section_s[0] == doctest::Approx(start_s + (finish_s - start_s) / 3.0f).epsilon(0.02));
    CHECK(t.section_s[1] == doctest::Approx(start_s + 2.0f * (finish_s - start_s) / 3.0f).epsilon(0.02));
    CHECK(t.section_at(0.0f) == 1);
    CHECK(t.section_at(t.section_s[0] + 1.0f) == 2);
    CHECK(t.section_at(t.length) == 3);
    // Region 0 is the grid, region 1 begins at the start line, the last at
    // the finish line; nothing wraps.
    CHECK(t.checkpoints.front() == 0);
    CHECK(t.checkpoints.at(1) == t.start);
    CHECK(t.checkpoints.back() == t.finish);
    CHECK(t.checkpoint_of(t.finish) == int(t.checkpoints.size()) - 1);
    CHECK(t.wrap(-5) == 0);
    CHECK(t.wrap(int(t.center.size()) + 5) == int(t.center.size()) - 1);
    CHECK(t.next(int(t.center.size()) - 1) == int(t.center.size()) - 1);
    // The road ends where the last control point is, not back at the first.
    CHECK(glm::distance(t.center.back().pos, vec3(0, 0, 700)) < 3.0f);
    CHECK(glm::distance(t.center.front().pos, vec3(0, 40, 0)) < 3.0f);
    // The grid: twelve slots on the road before the start line, all in region 0.
    for (int i = 0; i < 12; ++i) {
        const Track::Slot slot = t.grid_slot(i);
        const int at = t.nearest_sample(slot.pos);
        CAPTURE(i);
        CHECK(t.sample(at).s < start_s);
        CHECK(t.sample(at).s > 0.0f);
        CHECK(t.checkpoint_of(at) == 0);
        CHECK(glm::dot(slot.forward, t.sample(at).tangent) > 0.9f);
    }
    // Walls across both ends: a kart can't drive off the road's start or end.
    for (const int end : {0, int(t.center.size()) - 1}) {
        const CenterSample& c = t.center[size_t(end)];
        const vec3 beyond = c.pos + c.tangent * (end == 0 ? -0.3f : 0.3f) + c.up * 0.5f;
        std::vector<WallContact> contacts;
        t.collide_sphere(beyond, 0.55f, contacts);
        bool wall = false;
        for (const WallContact& w : contacts) wall |= w.surface == Surface::Wall && std::abs(glm::dot(w.normal, c.tangent)) > 0.7f;
        CAPTURE(end);
        CHECK(wall);
    }
    // The race is one run, in the road's own sections, whatever laps were asked for.
    World w(build_track(open_road()), test::tables(), RaceConfig{.laps = 5});
    CHECK(w.config().laps == 3);
    CHECK(w.track().laps == 3);
}

TEST_CASE("a kart runs an open road once, section by section, and finishes at the line") {
    const auto tables = test::tables();
    const auto parts = test::parts();
    World w(build_track(open_road()), tables, RaceConfig{.countdown_frames = 1, .items = false, .seed = 2});
    w.add_kart(test::default_params(tables, parts));
    const Track& t = w.track();
    std::vector<int> sections;
    int finish_at = -1, finish_place = 0, frames = 0;
    while (!w.karts()[0].finished && frames < 60 * 90) {
        KartInput in = autopilot(w.karts()[0], t);
        w.tick(std::span(&in, 1));
        ++frames;
        for (const auto& e : w.events()) {
            if (e.type == KartEventType::Lap) sections.push_back(e.value);
            if (e.type == KartEventType::Finish) finish_at = w.karts()[0].sample, finish_place = e.value;
            CHECK(e.type != KartEventType::Respawn);
        }
    }
    const KartState& k = w.karts()[0];
    REQUIRE(k.finished);
    CHECK(k.lap == 4); // past the last section
    CHECK(sections == std::vector<int>{2, 3});
    CHECK(finish_place == 1);
    CHECK(finish_at >= t.finish);
    CHECK(finish_at < t.finish + 6); // within a few samples of the line, not somewhere down the run-off
    CHECK(w.race_lap() == 3);
    MESSAGE("the run took " << k.finish_tick / 60.0f << " s");
    // Past the line the kart rolls out and stops on the road, short of the end wall.
    for (int i = 0; i < 60 * 12 && std::abs(w.karts()[0].speed) > 0.3f; ++i) {
        KartInput in = autopilot(w.karts()[0], t);
        w.tick(std::span(&in, 1));
    }
    CHECK(std::abs(w.karts()[0].speed) <= 0.3f);
    CHECK(w.karts()[0].grounded);
    CHECK(t.sample(w.karts()[0].sample).s < t.length - 2.0f);
    CHECK(w.karts()[0].lap == 4);
}

TEST_CASE("placings on an open road go by distance along it, and the leader's section drives its gates") {
    const auto tables = test::tables();
    const auto parts = test::parts();
    World w(build_track(open_road()), tables, RaceConfig{.countdown_frames = 1, .items = false, .seed = 2});
    for (int i = 0; i < 3; ++i) w.add_kart(test::default_params(tables, parts));
    const Track& t = w.track();
    CHECK_FALSE(t.gate_holds(0)); // "from_section": 2, and everyone is in section 1
    // Drive the karts to three different points, the first furthest.
    auto place_at = [&](int kart, float s) {
        KartState& k = w.karts_mut()[size_t(kart)];
        int i = 0;
        while (i + 1 < int(t.center.size()) && t.center[size_t(i + 1)].s <= s) ++i;
        const CenterSample& c = t.center[size_t(i)];
        k.pos = c.pos + c.up * 0.1f;
        k.sample = k.last_safe_sample = i;
    };
    place_at(0, 500.0f); // the last third
    place_at(1, 300.0f); // the middle
    place_at(2, 100.0f); // still in the first
    std::vector<KartInput> none(3);
    for (int i = 0; i < 3; ++i) w.tick(none);
    CHECK(w.standings() == std::vector<int>{0, 1, 2});
    CHECK(w.place_of(0) == 1);
    CHECK(w.place_of(2) == 3);
    CHECK(w.karts()[0].lap == 3);
    CHECK(w.karts()[1].lap == 2);
    CHECK(w.karts()[2].lap == 1);
    CHECK(w.race_lap() == 3);
    CHECK(t.gate_holds(0));
    CHECK(kart_race_distance(w.karts()[0], t) > kart_race_distance(w.karts()[1], t));
    CHECK(kart_race_distance(w.karts()[1], t) > kart_race_distance(w.karts()[2], t));
}

TEST_CASE("a ghost replays an open-road run exactly") {
    const auto tables = test::tables();
    const auto parts = test::parts();
    const KartParams p = test::default_params(tables, parts);
    const RaceConfig rc{.laps = 3};
    World w = make_time_trial_world(build_track(open_road()), tables, rc, p);
    Ghost g;
    g.track = w.track().id;
    g.engine = rc.engine;
    g.laps = rc.laps;
    for (int i = 0; i < 60 * 120 && !w.karts()[0].finished; ++i) {
        const KartInput in = quantize(cpu_input(w, 0, cpu_skill(CpuLevel::Hard, 0)));
        g.inputs.push_back(in);
        w.tick(std::span(&in, 1));
    }
    REQUIRE(w.karts()[0].finished);
    g.finish_tick = w.karts()[0].finish_tick;
    CHECK(ghost_still_valid(g, build_track(open_road()), tables, p));
    Ghost back;
    REQUIRE(Ghost::decode(g.encode(), back));
    GhostRunner run(build_track(open_road()), tables, rc, p, &back);
    for (size_t i = 0; i < back.inputs.size(); ++i) run.tick();
    CHECK(run.kart().finished);
    CHECK(run.kart().finish_tick == g.finish_tick);
}
