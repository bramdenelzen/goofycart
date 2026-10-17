// The track catalog: every track loads, is drivable by the CPU drivers
// without falling off, and its features are where the data says.
#include "sim_fixture.h"

#include "gk/sim/ai.h"
#include "gk/sim/track_def.h"

#include <algorithm>

using namespace gk;
using namespace gk::sim;

namespace {

const TrackCatalog& catalog() {
    static TrackCatalog c = [] {
        TrackCatalog cat;
        std::string err;
        const bool ok = cat.load(data_path("tracks/tracks.json"), &err);
        INFO(err);
        REQUIRE(ok);
        return cat;
    }();
    return c;
}

} // namespace

TEST_CASE("the track catalog has two cups of four") {
    const TrackCatalog& c = catalog();
    REQUIRE(c.cups.size() == 2);
    for (const Cup& cup : c.cups) CHECK(cup.tracks.size() == 4);
    CHECK(c.tracks.size() == 8);
    std::string err;
    TrackDef bad;
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0]]})", bad, &err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("every track is a clean lap for a CPU, and a 12-kart race finishes") {
    const auto tables = test::tables();
    const auto parts = test::parts();
    for (const TrackDef& def : catalog().tracks) {
        CAPTURE(def.id);
        Track track = build_track(def);
        CHECK(track.length > 400.0f);
        CHECK(track.item_boxes.size() >= 12);
        CHECK(track.checkpoints.size() > 8);
        bool surfaces[int(Surface::Count)] = {};
        for (const auto& tri : track.triangles) surfaces[int(tri.surface)] = true;
        CHECK(surfaces[int(Surface::BoostPad)]);

        World w(std::move(track), tables, RaceConfig{.laps = 2, .countdown_frames = 180, .seed = 3});
        for (int i = 0; i < 12; ++i) {
            KartParams p = test::default_params(tables, parts, {i % 4, i % 4, (i / 2) % 4, i % 2});
            p.rubber_band = cpu_rubber_band(CpuLevel::Normal);
            w.add_kart(p);
        }
        std::vector<KartInput> in(12);
        int respawns = 0, frames = 0, first_lap = 0;
        auto done = [&] { return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; }); };
        while (!done() && frames < 60 * 60 * 4) {
            for (int i = 0; i < 12; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(i % 3 == 0 ? CpuLevel::Hard : CpuLevel::Normal, i));
            w.tick(in);
            ++frames;
            for (const auto& e : w.events()) {
                if (e.type == KartEventType::Respawn) {
                    ++respawns;
                    MESSAGE("respawn of kart " << e.kart << " near sample " << w.karts()[size_t(e.kart)].last_safe_sample);
                }
                if (e.type == KartEventType::Lap && first_lap == 0) first_lap = w.race_ticks();
            }
        }
        MESSAGE(def.name << ": first lap " << first_lap / 60.0f << " s, race " << frames / 60.0f << " s, " << respawns
                         << " respawns");
        CHECK(done());
        CHECK(respawns <= 3); // items can knock a kart off an edge now and then
    }
}
