// The track catalog: every track loads, is drivable by the CPU drivers
// without falling off, and its features are where the data says.
#include "sim_fixture.h"

#include "gk/sim/ai.h"
#include "gk/sim/track_def.h"

#include <algorithm>
#include <string>

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

TEST_CASE("the track catalog's cups hold every track once") {
    const TrackCatalog& c = catalog();
    REQUIRE(c.cups.size() >= 3);
    std::vector<std::string> seen;
    for (const Cup& cup : c.cups) {
        CHECK(cup.tracks.size() >= 1);
        CHECK(cup.tracks.size() <= 4);
        for (const std::string& id : cup.tracks) {
            CAPTURE(id);
            CHECK(c.find(id) >= 0);
            CHECK(std::find(seen.begin(), seen.end(), id) == seen.end()); // in one cup only
            seen.push_back(id);
        }
    }
    CHECK(seen.size() == c.tracks.size()); // and every track is in a cup
    CHECK(c.tracks.size() >= 12);
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
        CHECK(surfaces[int(Surface::GlideRamp)]);
        CHECK(std::count_if(def.features.begin(), def.features.end(),
                            [](const TrackFeature& f) { return f.kind == TrackFeature::Kind::TrickRamp; }) >= 2);
        CHECK(track.obstacles.size() >= 2);
        // The new track format in use: side roads, things that change, roads that narrow and widen.
        CHECK(track.branches.size() >= 1);
        CHECK(track.gates.size() >= 2);
        float narrow = 1e9f, wide = 0.0f;
        for (const CenterSample& c : track.center) narrow = std::min(narrow, c.half_width), wide = std::max(wide, c.half_width);
        CHECK(wide - narrow >= 2.0f);

        World w(std::move(track), tables, RaceConfig{.laps = 2, .countdown_frames = 180, .seed = 3});
        for (int i = 0; i < 12; ++i) {
            KartParams p = test::default_params(tables, parts, {i % 4, i % 4, (i / 2) % 4, i % 2});
            p.rubber_band = cpu_rubber_band(CpuLevel::Normal);
            w.add_kart(p);
        }
        std::vector<KartInput> in(12);
        std::vector<int> last_hit(12, -1000); // frame each kart was last hit by an item
        int respawns = 0, unforced = 0, frames = 0, first_lap = 0;
        auto done = [&] { return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; }); };
        while (!done() && frames < 60 * 60 * 4) {
            for (int i = 0; i < 12; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(i % 3 == 0 ? CpuLevel::Hard : CpuLevel::Normal, i));
            w.tick(in);
            ++frames;
            for (const auto& e : w.events()) {
                if (e.type == KartEventType::Hit) last_hit[size_t(e.kart)] = frames;
                if (e.type == KartEventType::Respawn && !w.karts()[size_t(e.kart)].finished) { // finished karts coast without steering
                    ++respawns;
                    // Hit within the last while: an item did it, not the road (a
                    // kart knocked out of a glide can take some seconds to come down).
                    const bool forced = frames - last_hit[size_t(e.kart)] < 60 * 15;
                    unforced += !forced;
                    MESSAGE("respawn of kart " << e.kart << " near sample " << w.karts()[size_t(e.kart)].last_safe_sample
                                               << (forced ? std::string(" after a hit") : std::string(" unforced")));
                }
                if (e.type == KartEventType::Lap && first_lap == 0) first_lap = w.race_ticks();
            }
        }
        MESSAGE(def.name << ": first lap " << first_lap / 60.0f << " s, race " << frames / 60.0f << " s, " << respawns
                         << " respawns, " << unforced << " unforced");
        CHECK(done());
        CHECK(unforced <= 3); // the road itself: a slip off an edge now and then
        // A hit mid-glide drops the kart now, so one lightning over a chasm can
        // take several karts at once; how many depends on the platform's rounding
        // and on the item luck of this one race (Cloud Spire over seeds 1-6:
        // 0-5 before the hit-momentum and Blooper-limit changes, 1-11 after).
        CHECK(respawns <= 12);
    }
}

TEST_CASE("building a track twice gives the same geometry") {
    // The client draws one copy of the track and races on another, so any
    // difference between two builds shows as the road and its collision
    // drifting apart.
    for (const TrackDef& def : catalog().tracks) {
        INFO(def.id);
        const Track a = make_track(catalog(), def.id), b = make_track(catalog(), def.id);
        REQUIRE(a.vertices.size() == b.vertices.size());
        REQUIRE(a.triangles.size() == b.triangles.size());
        CHECK(std::equal(a.vertices.begin(), a.vertices.end(), b.vertices.begin()));
        bool same_tris = true;
        for (size_t i = 0; i < a.triangles.size(); ++i)
            same_tris &= a.triangles[i].a == b.triangles[i].a && a.triangles[i].b == b.triangles[i].b &&
                         a.triangles[i].c == b.triangles[i].c && a.triangles[i].surface == b.triangles[i].surface;
        CHECK(same_tris);
    }
}

TEST_CASE("where a track passes over itself there is room underneath") {
    // A bridge needs the kart, its camera and the deck's underside to fit.
    int crossings = 0;
    for (const TrackDef& def : catalog().tracks) {
        CAPTURE(def.id);
        const Track t = build_track(def);
        const int n = int(t.center.size());
        bool crosses = false;
        for (int i = 0; i < n; ++i)
            for (int j = i + 20; j < n && n - (j - i) >= 20; ++j) {
                const CenterSample &a = t.center[size_t(i)], &b = t.center[size_t(j)];
                // A loop's back comes down beside its own run-in, shifted
                // clear of it sideways rather than over it (test_loop).
                if (a.loop_r > 0.0f || b.loop_r > 0.0f) continue;
                const float d = glm::length(vec2(a.pos.x - b.pos.x, a.pos.z - b.pos.z));
                if (d > a.half_width + b.half_width + 10.0f) continue;
                CAPTURE(i);
                CAPTURE(j);
                CHECK(std::abs(a.pos.y - b.pos.y) >= 8.0f);
                crosses |= d < 2.0f;
            }
        crossings += crosses;
    }
    CHECK(crossings >= 4);
}

TEST_CASE("side roads keep clear of the rest of the track") {
    // Away from its own fork and merge, a side road either keeps its
    // distance from every other road or passes a bridge's height over or under it.
    for (const TrackDef& def : catalog().tracks) {
        CAPTURE(def.id);
        const Track t = build_track(def);
        for (const Track::Branch& b : t.branches) {
            CAPTURE(b.name);
            for (int k = 0; k < b.count; k += 2) {
                const CenterSample& a = t.sample(b.first + k);
                for (size_t j = 0; j < t.road.size(); j += 2) {
                    const CenterSample& o = t.road[j];
                    if (o.branch == a.branch) continue;
                    float ds = std::abs(o.s - a.s);
                    ds = std::min(ds, t.length - ds);
                    if (ds < 70.0f) continue; // its own junctions, and the main road it runs beside
                    const float d = glm::length(vec2(a.pos.x - o.pos.x, a.pos.z - o.pos.z));
                    if (d > a.half_width + o.half_width + 4.0f) continue;
                    CAPTURE(k);
                    CAPTURE(j);
                    CHECK(std::abs(a.pos.y - o.pos.y) >= 8.0f);
                }
            }
        }
    }
}

TEST_CASE("obstacles are solid from every side") {
    for (const TrackDef& def : catalog().tracks) {
        CAPTURE(def.id);
        const Track t = build_track(def);
        for (const Track::Obstacle& o : t.obstacles) {
            if (o.breakable != Track::Breakable::None) continue; // the World breaks those (test_props)
            for (const vec3 dir : {o.right, -o.right, o.forward, -o.forward}) {
                const float reach = std::abs(glm::dot(dir, o.right)) * o.half.x + std::abs(glm::dot(dir, o.forward)) * o.half.z;
                const vec3 sphere = o.pos + o.up * 1.0f + dir * (reach + 0.3f);
                std::vector<WallContact> contacts;
                t.collide_sphere(sphere, 0.55f, contacts);
                bool pushed_out = false;
                for (const WallContact& c : contacts) pushed_out |= c.surface == Surface::Wall && glm::dot(c.normal, dir) > 0.7f;
                CHECK(pushed_out);
            }
        }
    }
}

