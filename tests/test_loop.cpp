// The loop: a vertical loop spliced into the road (#43, #44).
#include "sim_fixture.h"

#include "gk/sim/ai.h"
#include "gk/sim/track_def.h"

#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <string>

using namespace gk;
using namespace gk::sim;

namespace {

// A level rectangle with two 300 m straights; `features` is spliced into the JSON.
TrackDef rect(const std::string& features) {
    const std::string text = R"({
      "id": "loopy", "name": "Loopy", "half_width": 7,
      "points": [[0, 0, -150], [0, 0, 150], [60, 0, 200], [120, 0, 150], [120, 0, -150], [60, 0, -200]],
      "features": [)" + features + R"(]
    })";
    TrackDef d;
    std::string err;
    REQUIRE_MESSAGE(parse_track_def(text, d, &err), err);
    return d;
}
const char* kLoop = R"({"type": "loop", "at": [0, 100], "radius": 12, "shift": 20})";

quat facing(vec3 forward, vec3 up) {
    forward = glm::normalize(forward - up * glm::dot(forward, up));
    const vec3 left = glm::normalize(glm::cross(up, forward));
    return glm::normalize(glm::quat_cast(glm::mat3(left, glm::cross(forward, left), forward)));
}

void put(World& w, int i, int sample, float lateral, float speed) {
    KartState& k = w.karts_mut()[size_t(i)];
    const CenterSample& c = w.track().sample(sample);
    k.pos = c.pos + c.right * lateral + c.up * 0.05f;
    k.rot = facing(c.tangent, c.up);
    k.speed = speed;
    k.slide = k.air_vel = vec3(0.0f);
    k.grounded = true;
    k.sample = k.last_safe_sample = sample;
}

World world_of(const TrackDef& d, int karts) {
    World w(build_track(d), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1});
    for (int i = 0; i < karts; ++i) w.add_kart(test::default_params(w.tables(), test::parts()));
    w.tick(std::vector<KartInput>(size_t(karts)));
    return w;
}

// The loop's samples: first, top and last.
struct LoopSpan { int first = -1, top = -1, last = -1; };
LoopSpan loop_span(const Track& t, int nth = 0) {
    LoopSpan sp;
    int seen = 0;
    for (size_t i = 0; i < t.center.size(); ++i) {
        const bool on = t.center[i].loop_r > 0.0f, before = i > 0 && t.center[i - 1].loop_r > 0.0f;
        if (on && !before && seen++ < nth) continue;
        if (on && !before && sp.first < 0) sp.first = int(i);
        if (sp.first >= 0 && on) {
            sp.last = int(i);
            if (sp.top < 0 || t.center[i].pos.y > t.center[size_t(sp.top)].pos.y) sp.top = int(i);
        }
        if (sp.first >= 0 && !on) break;
    }
    return sp;
}

struct Run { int respawns = 0, air = 0, frames = 0; float max_speed = 0.0f, speed_in = 0.0f, speed_out = 0.0f; bool out = false; };
template <class F>
Run drive(World& w, const LoopSpan& sp, F input, int frames = 60 * 30) {
    Run r;
    for (int f = 0; f < frames; ++f, ++r.frames) {
        std::vector<KartInput> in(w.karts().size());
        in[0] = input(w);
        w.tick(in);
        const KartState& k = w.karts()[0];
        for (const auto& e : w.events()) r.respawns += e.type == KartEventType::Respawn;
        if (k.sample >= sp.first && k.sample <= sp.last) {
            r.air += !k.grounded;
            if (r.speed_in == 0.0f) r.speed_in = k.speed;
            r.max_speed = std::max(r.max_speed, k.speed);
        }
        if (k.sample > sp.last + 5 && k.grounded) {
            r.speed_out = k.speed;
            r.out = true;
            break;
        }
    }
    return r;
}

} // namespace

TEST_CASE("a loop splices into the road with a level frame, upside down at the top, exiting beside its entry") {
    const TrackDef plain = rect("");
    const Track flat = build_track(plain);
    const Track t = build_track(rect(kLoop));
    const LoopSpan sp = loop_span(t);
    REQUIRE(sp.first > 0);
    REQUIRE(sp.top > sp.first);
    REQUIRE(sp.last > sp.top);
    const float L = 2.0f * kPi * 12.0f + 12.0f;
    CHECK(t.length > flat.length + L * 0.8f); // the loop's length is in the lap, less the straight it consumed
    for (size_t i = 0; i < t.center.size(); ++i) {
        const CenterSample& c = t.center[i];
        CHECK(std::isfinite(c.right.x + c.up.x + c.tangent.x));
        if (c.loop_r > 0.0f) {
            CHECK(std::abs(glm::dot(c.up, vec3(-1, 0, 0))) < 0.03f); // the road's up stays in the loop's plane: no twist
            CHECK(std::abs(c.right.y) < 0.35f);                      // right tilts only by the sideways shift
            CHECK(c.grip);
            CHECK_FALSE(c.respawn_ok);
            CHECK(c.loop_r == doctest::Approx(12.0f));
        }
    }
    const CenterSample& entry = t.center[size_t(sp.first - 1)];
    const CenterSample& top = t.center[size_t(sp.top)];
    const CenterSample& exit = t.center[size_t(sp.last + 1)];
    CHECK(top.up.y < -0.95f);
    CHECK(top.pos.y > entry.pos.y + 22.0f);
    CHECK(glm::dot(top.pos - entry.pos, entry.right) == doctest::Approx(10.0f).epsilon(0.2)); // half the shift at the top
    CHECK(glm::dot(exit.pos - entry.pos, entry.right) == doctest::Approx(20.0f).epsilon(0.1));
    CHECK(std::abs(exit.pos.y - entry.pos.y) < 0.5f);
    CHECK(std::abs(exit.up.y - 1.0f) < 0.02f);
    // Right flips nowhere: consecutive frames agree.
    for (size_t i = 1; i < t.center.size(); ++i) CHECK(glm::dot(t.center[i].right, t.center[i - 1].right) > 0.0f);
    // There is a floor at the top, facing the road's up, and walls either side of it.
    const auto hit = t.raycast(top.pos + top.up * 1.0f, -top.up, 3.0f);
    REQUIRE(hit);
    CHECK(glm::dot(hit->normal, top.up) > 0.9f);
    CHECK(t.raycast(top.pos + top.up * 0.5f, top.right, top.half_width + 6.0f)); // the wall stands beyond the verge
    // The road 80 m past the exit is back on the designer's line.
    const CenterSample& later = t.center[size_t(sp.last + 1 + 40)];
    CHECK(glm::distance(later.pos, flat.sample(flat.nearest_sample(later.pos, -1)).pos) < 1.0f);
    // A feature placed by a later control point still lands there.
    const Track with_box = build_track(rect(std::string(kLoop) + R"(,{"type": "item_boxes", "at": [2, 0]})"));
    REQUIRE(with_box.item_boxes.size() >= 4);
    CHECK(glm::distance(vec3(with_box.item_boxes[0].x, 0.0f, with_box.item_boxes[0].z), vec3(60.0f, 0.0f, 200.0f)) < 8.0f);
}

TEST_CASE("the loop's rules: not on a bend, not with a ramp, not too close to the entry road") {
    std::string err;
    TrackDef d;
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","half_width":7,"points":[[0,0,-150],[0,0,150],[60,0,200],[120,0,150],[120,0,-150],[60,0,-200]],
        "features":[{"type":"loop","at":[0,100],"radius":12,"shift":10}]})", d, &err));
    CHECK(err.find("shift") != std::string::npos);
    CHECK_FALSE(parse_track_def(R"({"id":"x","name":"x","half_width":7,"points":[[0,0,-150],[0,0,150],[60,0,200],[120,0,150],[120,0,-150],[60,0,-200]],
        "features":[{"type":"loop","at":[0,100],"radius":5}]})", d, &err));
    CHECK_THROWS(build_track(rect(R"({"type": "loop", "at": [1, 20], "radius": 12, "shift": 20})")));                    // on the bend
    CHECK_THROWS(build_track(rect(std::string(kLoop) + R"(,{"type": "trick_ramp", "at": [0, 120], "length": 8})"))); // a ramp on it
    CHECK_THROWS(build_track(rect(std::string(kLoop) + R"(,{"type": "bank", "from": [0, 90], "to": [0, 140], "angle": 10})")));
}

TEST_CASE("the autopilot and a hard CPU drive the loop without leaving it, out about 15% faster at most") {
    World w = world_of(rect(kLoop), 1);
    const LoopSpan sp = loop_span(w.track());
    put(w, 0, sp.first - 25, 0.0f, 20.0f);
    const Run a = drive(w, sp, [&](const World& w) { return autopilot(w.karts()[0], w.track()); });
    MESSAGE("autopilot: out " << a.out << " respawns " << a.respawns << " air " << a.air << " in " << a.speed_in << " max " << a.max_speed << " out " << a.speed_out);
    CHECK(a.out);
    CHECK(a.respawns == 0);
    CHECK(a.air <= 3);
    CHECK(a.max_speed <= a.speed_in * 1.2f);
    put(w, 0, sp.first - 25, 0.0f, 20.0f);
    const Run c = drive(w, sp, [&](const World& w) { return cpu_input(w, 0, cpu_skill(CpuLevel::Hard, 0)); });
    MESSAGE("hard CPU: out " << c.out << " respawns " << c.respawns << " air " << c.air << " max " << c.max_speed);
    CHECK(c.out);
    CHECK(c.respawns == 0);
    CHECK(c.air <= 6);
    // Slowly too: a kart crawling into the loop still gets round (it sticks at any speed).
    put(w, 0, sp.first - 25, 0.0f, 6.0f);
    const Run slow = drive(w, sp, [&](const World& w) { return autopilot(w.karts()[0], w.track()); }, 60 * 40);
    MESSAGE("slow: out " << slow.out << " respawns " << slow.respawns << " air " << slow.air);
    CHECK(slow.out);
    CHECK(slow.respawns == 0);
}

TEST_CASE("a hop at the top of the loop comes back to the road, and a hit there stays on it") {
    World w = world_of(rect(kLoop), 1);
    const Track& t = w.track();
    const LoopSpan sp = loop_span(t);
    put(w, 0, sp.top - 3, 0.0f, 22.0f);
    int air = 0, respawns = 0;
    for (int f = 0; f < 120; ++f) {
        std::vector<KartInput> in(1);
        in[0] = autopilot(w.karts()[0], t);
        in[0].drift = f == 2;
        w.tick(in);
        for (const auto& e : w.events()) respawns += e.type == KartEventType::Respawn;
        air += !w.karts()[0].grounded;
    }
    CHECK(respawns == 0);
    CHECK(air > 0);
    CHECK(air < 25);
    CHECK(w.karts()[0].grounded);
    put(w, 0, sp.top, 0.0f, 22.0f);
    w.apply_hit(0, HitKind::Tumble, -1);
    air = 0, respawns = 0;
    for (int f = 0; f < 240; ++f) {
        std::vector<KartInput> in(1);
        in[0] = autopilot(w.karts()[0], t);
        w.tick(in);
        for (const auto& e : w.events()) respawns += e.type == KartEventType::Respawn;
        air += !w.karts()[0].grounded;
    }
    CHECK(respawns == 0);
    CHECK(air < 60);
    CHECK(w.karts()[0].grounded);
}

TEST_CASE("respawning from a loop puts the kart back on the road before it") {
    World w = world_of(rect(kLoop), 1);
    const Track& t = w.track();
    const LoopSpan sp = loop_span(t);
    put(w, 0, sp.top, 0.0f, 20.0f);
    w.karts_mut()[0].last_safe_sample = sp.top;
    w.karts_mut()[0].pos.y = t.kill_y - 10.0f;
    int placed = -1;
    for (int f = 0; f < 300 && placed < 0; ++f) {
        w.tick(std::vector<KartInput>(1));
        const KartState& q = w.karts()[0];
        if (q.respawn_frames > 0 && q.respawn_frames < w.tables().constants.respawn_frames / 2) placed = q.sample;
    }
    REQUIRE(placed >= 0);
    CHECK(placed < sp.first);
    CHECK(t.sample(placed).respawn_ok);
    CHECK(w.karts()[0].up().y > 0.9f);
}

TEST_CASE("karts on the loop's wall collide in the road's plane") {
    World w = world_of(rect(kLoop), 2);
    const Track& t = w.track();
    const LoopSpan sp = loop_span(t);
    int wall = sp.first;
    for (int i = sp.first; i < sp.top; ++i)
        if (std::abs(t.center[size_t(i)].up.y) < std::abs(t.center[size_t(wall)].up.y)) wall = i;
    REQUIRE(std::abs(t.center[size_t(wall)].up.y) < 0.1f);
    // Side by side across the wall, clear of each other: no bump.
    put(w, 0, wall, -2.0f, 15.0f);
    put(w, 1, wall, 2.0f, 15.0f);
    w.tick(std::vector<KartInput>(2));
    CHECK(glm::distance(w.karts()[0].pos, w.karts()[1].pos) > 3.5f);
    CHECK(w.karts()[0].spin_frames == 0);
    // One just ahead of the other along the wall (that's one above the other in
    // world y): the push is along the road, and they end up apart.
    put(w, 0, wall, 0.0f, 15.0f);
    w.karts_mut()[1] = w.karts()[0];
    w.karts_mut()[1].pos += t.sample(wall).tangent * 1.3f;
    const vec3 d0 = w.karts()[1].pos - w.karts()[0].pos;
    w.tick(std::vector<KartInput>(2));
    const vec3 d1 = w.karts()[1].pos - w.karts()[0].pos;
    const vec3 push = d1 - d0;
    CHECK(glm::length(d1) > glm::length(d0) + 0.05f);
    CHECK(std::abs(glm::dot(push, t.sample(wall).up)) < 0.5f * glm::length(push) + 1e-3f);
}

TEST_CASE("two loops on one track, and a race of 12 round them") {
    const std::string two = std::string(kLoop) + R"(,{"type": "loop", "at": [3, 100], "radius": 10, "shift": -18})";
    const Track t = build_track(rect(two));
    const LoopSpan a = loop_span(t, 0), b = loop_span(t, 1);
    REQUIRE(a.first > 0);
    REQUIRE(b.first > a.last);
    CHECK(t.center[size_t(a.top)].up.y < -0.95f);
    CHECK(t.center[size_t(b.top)].up.y < -0.95f);
    CHECK(t.center[size_t(b.top)].loop_r == doctest::Approx(10.0f));
    for (size_t i = 1; i < t.center.size(); ++i) CHECK(glm::dot(t.center[i].right, t.center[i - 1].right) > 0.0f);

    World w(build_track(rect(two)), test::tables(), RaceConfig{.laps = 2, .countdown_frames = 60});
    const auto parts = test::parts();
    for (int i = 0; i < 12; ++i) w.add_kart(test::default_params(w.tables(), parts, {i % 4, i % 4, (i / 2) % 4, i % 2}));
    int respawns = 0, frames = 0;
    auto done = [&] { return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; }); };
    std::vector<KartInput> in(12);
    while (!done() && frames < 60 * 60 * 3) {
        for (int i = 0; i < 12; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(i % 3 == 0 ? CpuLevel::Hard : CpuLevel::Normal, i));
        w.tick(in);
        ++frames;
        for (const auto& e : w.events()) respawns += e.type == KartEventType::Respawn && !w.karts()[size_t(e.kart)].finished;
    }
    MESSAGE("12 karts, 2 laps, two loops: " << frames / 60.0f << " s, " << respawns << " respawns");
    CHECK(done());
    CHECK(respawns <= 2);
}
