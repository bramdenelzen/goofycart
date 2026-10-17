// Time Trial ghosts: a recorded run replays exactly, survives the file
// format, and is rejected once the physics no longer reproduce it.
#include "gk/sim/ghost.h"
#include "gk/sim/track_def.h"
#include "sim_fixture.h"

using namespace gk;
using namespace gk::sim;

namespace {

// Records a hard CPU through a 1-lap Time Trial, as the client records a player.
Ghost record(const PhysicsTables& t, const KartParams& p, RaceConfig rc) {
    World w = make_time_trial_world(make_greybox_track(), t, rc, p);
    Ghost g;
    g.track = w.track().id;
    g.engine = rc.engine;
    g.laps = rc.laps;
    for (int i = 0; i < 60 * 120 && !w.karts()[0].finished; ++i) {
        const KartInput in = quantize(cpu_input(w, 0, cpu_skill(CpuLevel::Hard, 0)));
        g.inputs.push_back(in);
        w.tick(std::span(&in, 1));
    }
    g.finish_tick = w.karts()[0].finished ? w.karts()[0].finish_tick : 0;
    return g;
}

} // namespace

TEST_CASE("a ghost replays its run exactly, through the file format") {
    const auto t = test::tables();
    const auto c = test::parts();
    const KartParams p = test::default_params(t, c);
    const RaceConfig rc{.laps = 1};
    const Ghost g = record(t, p, rc);
    REQUIRE(g.finish_tick > 0);
    MESSAGE("ghost lap " << float(g.finish_tick) / 60.0f << " s, " << g.encode().size() << " bytes");

    Ghost back;
    REQUIRE(Ghost::decode(g.encode(), back));
    CHECK(back.inputs == g.inputs);
    CHECK(back.finish_tick == g.finish_tick);
    CHECK(back.track == g.track);

    // Replayed in lockstep, the ghost kart matches the recorded one.
    GhostRunner run(make_greybox_track(), t, rc, p, &back);
    for (size_t i = 0; i < back.inputs.size(); ++i) run.tick();
    CHECK(run.kart().finished);
    CHECK(run.kart().finish_tick == g.finish_tick);
    CHECK(ghost_still_valid(back, make_greybox_track(), t, p));

    // Different physics (here a slower kart) no longer reproduce the run.
    KartParams slow = p;
    slow.top_speed *= 0.9f;
    CHECK_FALSE(ghost_still_valid(back, make_greybox_track(), t, slow));

    // Garbage is rejected.
    auto bytes = g.encode();
    bytes.resize(bytes.size() - 1);
    CHECK_FALSE(Ghost::decode(bytes, back));
    CHECK_FALSE(Ghost::decode(std::vector<uint8_t>{1, 2, 3}, back));
}

TEST_CASE("with no recording the ghost drives as a CPU") {
    const auto t = test::tables();
    const auto c = test::parts();
    GhostRunner run(make_greybox_track(), t, RaceConfig{.laps = 1}, test::default_params(t, c), nullptr);
    CHECK_FALSE(run.replaying());
    for (int i = 0; i < 60 * 60 && !run.kart().finished; ++i) run.tick();
    CHECK(run.kart().finished);
}
