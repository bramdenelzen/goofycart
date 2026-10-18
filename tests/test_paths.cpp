// Side roads and the things that change during a race: branches that fork
// off the main loop and rejoin it, gates opened by the lap, the race clock or
// a switch, barriers, vanishing roads and surface zones.
#include "sim_fixture.h"

#include "gk/net/protocol.h"
#include "gk/sim/ai.h"
#include "gk/sim/track_def.h"

#include <algorithm>
#include <set>

using namespace gk;
using namespace gk::sim;

namespace {

// A long oval: an always-open shortcut on the inside of the far bend, a
// bridge outside the near bend that only exists while a switch is pressed,
// a side road that opens from lap 2, a door on a timer and a flood on lap 2.
const char* kTrack = R"({
  "id": "paths", "name": "Paths", "theme": "meadow", "ground": true, "half_width": 7,
  "gates": {"lap2": {"from_lap": 2}, "door": {"cycle": [8, 4]}, "lever": {"switch": "lever"}},
  "points": [[0,0,-150],[0,0,0],[0,0,150],[60,0,220],[140,0,150],[140,0,0],[140,0,-150],[70,0,-220]],
  "features": [
    {"type": "item_boxes", "at": [1, 0]},
    {"type": "item_boxes", "at": [5, 30]},
    {"type": "switch", "name": "lever", "at": [5, 60], "offset": 4.5},
    {"type": "barrier", "at": [5, 0], "width": 6, "offset": -4, "when": "door"},
    {"type": "zone", "from": [6, 0], "to": [6, 60], "surface": "water", "when": "lap2"},
    {"type": "boost_pad", "at": [0, 20], "length": 8}
  ],
  "branches": [
    {"name": "inside", "from": [2, 50], "to": [4, 50], "side": "left", "points": [[75,0,192,5],[118,0,148,5]],
     "features": [{"type": "boost_pad", "at": 40, "length": 8}, {"type": "item_boxes", "at": 60}], "cpu_share": 0.6},
    {"name": "bridge", "from": [6, 0], "to": [7, 40], "side": "right", "points": [[160,4,-215],[100,4,-245]], "when": "lever", "vanish": true},
    {"name": "late", "from": [1, 30], "to": [2, 10], "side": "right", "points": [[-22,0,75],[-20,0,105]], "when": "lap2", "half_width": 5}
  ]
})";

Track paths_track() {
    TrackDef d;
    std::string err;
    REQUIRE_MESSAGE(parse_track_def(kTrack, d, &err), err);
    return build_track(d);
}

int branch_named(const Track& t, const std::string& name) {
    for (size_t b = 0; b < t.branches.size(); ++b)
        if (t.branches[b].name == name) return int(b);
    FAIL("no branch " << name);
    return -1;
}

World paths_world(int karts) {
    const auto tables = test::tables();
    const auto parts = test::parts();
    World w(paths_track(), tables, RaceConfig{.laps = 3, .countdown_frames = 60, .seed = 5});
    for (int i = 0; i < karts; ++i) {
        KartParams p = test::default_params(tables, parts, {i % 4, i % 4, (i / 2) % 4, i % 2});
        p.rubber_band = cpu_rubber_band(CpuLevel::Normal);
        w.add_kart(p);
    }
    return w;
}

} // namespace

TEST_CASE("a track file with branches, gates, switches, barriers and zones builds") {
    const Track t = paths_track();
    REQUIRE(t.branches.size() == 3);
    CHECK(t.road.size() == t.center.size() + size_t(t.branches[0].count + t.branches[1].count + t.branches[2].count));
    CHECK(t.line.size() == t.road.size());
    CHECK(t.switches.size() == 1);
    CHECK(t.zones.size() == 1);
    // The door, a barrier across the late road's mouth, a fence along the bridge's opening.
    CHECK(t.barriers.size() >= 3);
    CHECK(t.item_boxes.size() == 12);
    for (const Track::Branch& b : t.branches) {
        CAPTURE(b.name);
        // Each side road starts beside the main road and ends beside it again.
        const CenterSample &first = t.sample(b.first), &last = t.sample(b.first + b.count - 1);
        const CenterSample &fork = t.center[size_t(b.fork)], &merge = t.center[size_t(b.merge)];
        CHECK(glm::distance(first.pos, fork.pos) == doctest::Approx(fork.half_width + first.half_width).epsilon(0.05));
        CHECK(glm::distance(last.pos, merge.pos) == doctest::Approx(merge.half_width + last.half_width).epsilon(0.05));
        // Progress runs from the fork's to the merge's along it.
        float prev = first.s;
        for (int k = 1; k < b.count; ++k) {
            const float s = t.sample(b.first + k).s;
            CHECK(std::fmod(s - prev + t.length, t.length) < 8.0f);
            prev = s;
        }
        // Samples sit about 2 m apart.
        for (int k = 1; k < b.count; ++k) CHECK(glm::distance(t.sample(b.first + k).pos, t.sample(b.first + k - 1).pos) < 2.6f);
    }
    // Walking the roads: past a fork with the branch's bit set goes onto it,
    // and its end leads back to the merge.
    const Track::Branch& in = t.branches[size_t(branch_named(t, "inside"))];
    const int b = branch_named(t, "inside");
    CHECK(t.next(in.fork, 1u << b) == in.first);
    CHECK(t.next(in.fork, 0) == t.wrap(in.fork + 1));
    CHECK(t.next(in.first + in.count - 1) == in.merge);
    CHECK(t.prev(in.first) == in.fork);
    CHECK(t.advance(in.fork, in.count + 1, 1u << b) == in.merge);
    CHECK(t.main_sample(in.first + in.count / 2) != 0);

    std::string err;
    TrackDef bad;
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","points":[[0,0,0],[1,0,0],[1,0,1],[0,0,1]],
        "branches":[{"from":0,"to":2,"when":"nope"}]})", bad, &err));
    CHECK(err.find("nope") != std::string::npos);
}

TEST_CASE("gates follow the leader's lap, the race clock and switches") {
    World w = paths_world(2);
    const Track& t = w.track();
    const int late = branch_named(t, "late"), bridge = branch_named(t, "bridge");
    CHECK_FALSE(t.branch_open(late));
    CHECK_FALSE(t.branch_open(bridge));
    CHECK(t.branch_open(branch_named(t, "inside")));

    // The bridge's road isn't there while it's shut: nothing to land on.
    const CenterSample& mid = t.sample(t.branches[size_t(bridge)].first + t.branches[size_t(bridge)].count / 2);
    auto floor_under = [&](const CenterSample& c) {
        const auto hit = w.track().raycast(c.pos + c.up * 2.0f, -c.up, 4.0f);
        return hit && hit->surface != Surface::OutOfBounds;
    };
    CHECK_FALSE(floor_under(mid));
    WorldState s = w.save();
    REQUIRE(s.switches.size() == 1);
    s.switches[0] = 100;
    w.restore(s);
    CHECK(w.track().branch_open(bridge));
    CHECK(floor_under(mid));

    // Lap 2 for the leader opens the late road and floods the zone.
    s = w.save();
    s.karts[1].lap = 2;
    w.restore(s);
    CHECK(w.track().branch_open(late));
    const Track::Zone& z = w.track().zones[0];
    const CenterSample& zc = w.track().sample(z.first + z.count / 2);
    const auto wet = w.track().raycast(zc.pos + zc.up * 2.0f, -zc.up, 4.0f);
    REQUIRE(wet);
    CHECK(wet->surface == Surface::Water);
    s.karts[1].lap = 1;
    w.restore(s);
    const auto dry = w.track().raycast(zc.pos + zc.up * 2.0f, -zc.up, 4.0f);
    REQUIRE(dry);
    CHECK(dry->surface == Surface::Road);

    // The door: up for 4 s of every 8 s of race time, solid while it's up.
    const Track::Barrier* door = nullptr;
    for (const auto& b : w.track().barriers)
        if (b.branch < 0) door = &b;
    REQUIRE(door != nullptr);
    auto blocked = [&] {
        std::vector<WallContact> contacts;
        w.track().collide_sphere(door->pos + door->up * 0.8f, 0.55f, contacts);
        return !contacts.empty() && std::any_of(contacts.begin(), contacts.end(), [](const WallContact& c) { return c.surface == Surface::Wall; });
    };
    s = w.save();
    s.race_ticks = 60;
    w.restore(s);
    CHECK(blocked());
    s.race_ticks = 60 * 5;
    w.restore(s);
    CHECK_FALSE(blocked());
    s.race_ticks = 60 * 9;
    w.restore(s);
    CHECK(blocked());

    // Driving over the switch presses it, and it stays pressed a while.
    const Track::Switch& sw = w.track().switches[0];
    s = w.save();
    s.switches[0] = 0;
    s.countdown_left = 0;
    s.karts[0].pos = sw.pos + sw.up * 0.05f;
    s.karts[0].grounded = true;
    w.restore(s);
    std::vector<KartInput> none(2);
    w.tick(none);
    CHECK(w.switches()[0] > 0);
    CHECK(w.track().branch_open(bridge));
}

TEST_CASE("CPUs race on the side roads, never through a shut one, and every kart finishes") {
    World w = paths_world(10);
    const Track& t = w.track();
    const int late = branch_named(t, "late");
    std::set<int> used[3];
    std::vector<KartInput> in(10);
    int frames = 0, respawns = 0, late_while_shut = 0, pressed_ticks = 0;
    auto done = [&] { return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; }); };
    while (!done() && frames < 60 * 60 * 5) {
        // Someone presses the lever now and then, so the bridge opens.
        for (int i = 0; i < 10; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(i % 2 ? CpuLevel::Hard : CpuLevel::Normal, i));
        const bool late_open = t.branch_open(late);
        w.tick(in);
        if (w.switches()[0] > 0) ++pressed_ticks;
        ++frames;
        for (size_t i = 0; i < w.karts().size(); ++i) {
            const int b = t.sample(w.karts()[i].sample).branch;
            if (b >= 0) used[b].insert(int(i));
            if (b == late && !late_open && !w.karts()[i].finished) {
                // Only counts if it went in while shut: it had to pass the barrier.
                const int k = w.karts()[i].sample - t.branches[size_t(late)].first;
                if (k > 8 && k < 20) ++late_while_shut;
            }
        }
        for (const auto& e : w.events())
            if (e.type == KartEventType::Respawn && !w.karts()[size_t(e.kart)].finished) ++respawns;
    }
    MESSAGE("race " << frames / 60.0f << " s, " << respawns << " respawns; inside " << used[0].size() << ", bridge " << used[1].size()
                    << ", late " << used[2].size() << " karts; switch pressed " << pressed_ticks / 60 << " s");
    CHECK(done());
    CHECK(respawns <= 3);
    CHECK(used[size_t(branch_named(t, "inside"))].size() >= 3);
    CHECK(used[size_t(late)].size() >= 2);
    CHECK(late_while_shut == 0);
}

TEST_CASE("a race on side roads rewinds and replays exactly (prediction)") {
    World a = paths_world(6);
    std::vector<KartInput> in(6);
    auto inputs = [&](const World& w) {
        for (int i = 0; i < 6; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(CpuLevel::Hard, i));
        return in;
    };
    for (int f = 0; f < 60 * 40; ++f) a.tick(inputs(a));
    const WorldState mark = a.save();
    std::vector<std::vector<KartInput>> log;
    for (int f = 0; f < 60 * 20; ++f) {
        log.push_back(inputs(a));
        a.tick(log.back());
    }
    World b = paths_world(6);
    b.restore(mark);
    for (const auto& step : log) b.tick(step);
    for (size_t i = 0; i < a.karts().size(); ++i) {
        CHECK(a.karts()[i].pos == b.karts()[i].pos);
        CHECK(a.karts()[i].sample == b.karts()[i].sample);
        CHECK(a.karts()[i].lap == b.karts()[i].lap);
    }
    CHECK(a.switches() == b.switches());
    CHECK(a.track().gate_state == b.track().gate_state);
}

TEST_CASE("switch timers travel in snapshots") {
    net::SnapshotMsg m;
    m.switches = {0, 37, 600};
    const auto bytes = net::encode(m);
    net::SnapshotMsg out;
    REQUIRE(net::decode(bytes, out));
    CHECK(out.switches == m.switches);
}
