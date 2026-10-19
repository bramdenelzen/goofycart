// The glider: height and speed are one budget. Hands off it holds its air
// speed stat and sinks at 1/glide_ratio of it; a dive buys speed, a climb
// spends it, and it never stalls. And every kart build still clears every
// glide gap on every track.
#include "sim_fixture.h"

#include "gk/sim/track_def.h"

#include <map>
#include <utility>

using namespace gk;
using namespace gk::sim;

namespace {

quat heading(vec3 forward, vec3 up) {
    forward = glm::normalize(forward - up * glm::dot(forward, up));
    const vec3 left = glm::normalize(glm::cross(up, forward));
    return glm::normalize(glm::quat_cast(glm::mat3(left, glm::cross(forward, left), forward)));
}

// A kart already flying over the flat test ground, high up, heading +Z.
struct Flight {
    PhysicsTables tables = test::tables();
    PartsCatalog parts = test::parts();
    KartParams p = test::default_params(tables, parts);
    Track track = test::flat_track();
    KartState s;
    Flight(float speed, float climb = 0.0f) {
        s.pos = vec3(0.0f, 300.0f, 0.0f);
        s.rot = heading(vec3(0, 0, 1), kUp);
        s.grounded = false;
        s.gliding = true;
        s.air_vel = vec3(0.0f, climb, speed);
        s.air_frames = 10;
    }
    void fly(int frames, float pitch = 0.0f, float steer = 0.0f) {
        KartInput in;
        in.accelerate = true;
        in.pitch = pitch;
        in.steer = steer;
        for (int i = 0; i < frames; ++i) kart_step(s, in, {track, tables.constants, p});
    }
    float speed() const { return glm::length(s.air_vel); }
    float trim_sink() const { return p.glide_speed / std::sqrt(1.0f + tables.constants.glide_ratio * tables.constants.glide_ratio); }
};

} // namespace

TEST_CASE("hands off, the glider settles at its air speed and sinks at its glide ratio") {
    // The drag's pull back to trim is gentle (a boost should carry for
    // seconds), so give it a while.
    Flight f(20.0f);
    f.fly(60 * 15);
    REQUIRE(f.s.gliding);
    CHECK(f.speed() == doctest::Approx(f.p.glide_speed).epsilon(0.03));
    CHECK(-f.s.air_vel.y == doctest::Approx(f.trim_sink()).epsilon(0.08));
    // From above trim it bleeds back down too: no hidden target speed, just drag.
    Flight g(f.p.glide_speed * 1.25f);
    g.fly(60 * 15);
    CHECK(g.speed() == doctest::Approx(g.p.glide_speed).epsilon(0.04));
}

TEST_CASE("a boosted launch carries into the glide and bleeds off over seconds, not at once") {
    const float boosted = Flight(0.0f).p.top_speed * 1.32f;
    Flight f(boosted, 7.0f);
    f.fly(60);
    CHECK(f.speed() > f.p.glide_speed * 1.15f); // a second in, most of the boost is still there
    f.fly(60 * 2);
    CHECK(f.speed() > f.p.glide_speed * 1.05f);
    CHECK(f.speed() < boosted);
}

TEST_CASE("diving buys speed up to the cap and climbing spends it down to the floor, with no stall") {
    const auto& k = Flight(0.0f).tables.constants;
    Flight dive(24.0f);
    dive.fly(60 * 3, -1.0f);
    const float cap = dive.p.top_speed * k.glide_cap_mult;
    CHECK(dive.speed() > dive.p.glide_speed * 1.15f);
    CHECK(dive.speed() <= cap + 0.01f);
    CHECK(dive.s.air_vel.y < -dive.trim_sink() * 1.5f); // and it is going down fast
    // Pull out of the dive: the speed it bought carries while the path flattens.
    dive.fly(30, 0.0f);
    CHECK(dive.speed() > dive.p.glide_speed * 1.1f);

    Flight climb(24.0f);
    climb.fly(45, 1.0f);
    CHECK(climb.s.air_vel.y > -climb.trim_sink() * 0.5f); // the nose up lifts it (or near enough)
    climb.fly(60 * 8, 1.0f);
    const float floor = climb.p.glide_speed * k.glide_floor;
    CHECK(climb.speed() >= floor - 0.01f);
    CHECK(climb.speed() < climb.p.glide_speed);
    // Held at full back stick it mushes: the nose drops by itself, so it is
    // still flying forward and sinking rather than hanging in the air.
    CHECK(climb.s.air_vel.y < 0.0f);
    CHECK(climb.s.air_vel.z > floor * 0.8f);
    CHECK(climb.s.gliding);
}

TEST_CASE("the glide is deterministic") {
    Flight a(26.0f), b(26.0f);
    for (int i = 0; i < 240; ++i) {
        const float pitch = (i / 30) % 2 ? -0.7f : 0.5f, steer = (i / 45) % 2 ? 0.6f : -0.3f;
        a.fly(1, pitch, steer);
        b.fly(1, pitch, steer);
    }
    CHECK(a.s.pos == b.s.pos);
    CHECK(a.s.air_vel == b.s.air_vel);
    CHECK(a.s.rot == b.s.rot);
}

TEST_CASE("every kart build clears every glide gap on every track") {
    // The glide's reach depends on two numbers of a build, its air speed and
    // its top speed (the ramp's boost and the cap); fly each distinct pair
    // once, over each gap, entering the ramp at top speed and hands off (no
    // dive, no pull-up: the worst honest line).
    const auto tables = test::tables();
    const auto parts = test::parts();
    std::map<std::pair<float, float>, Build> distinct;
    for (int d = 0; d < int(parts.drivers.size()); ++d)
        for (int b = 0; b < int(parts.bodies.size()); ++b)
            for (int t = 0; t < int(parts.tires.size()); ++t)
                for (int g = 0; g < int(parts.gliders.size()); ++g) {
                    const Build build{d, b, t, g};
                    const KartParams p = test::default_params(tables, parts, build);
                    distinct.emplace(std::make_pair(p.glide_speed, p.top_speed), build);
                }
    MESSAGE(distinct.size() << " distinct glides among " << parts.drivers.size() * parts.bodies.size() * parts.tires.size() * parts.gliders.size() << " builds");
    CHECK(distinct.size() >= 8);

    TrackCatalog cat;
    std::string err;
    REQUIRE_MESSAGE(cat.load(data_path("tracks/tracks.json"), &err), err);
    int gaps_flown = 0;
    for (const TrackDef& def : cat.tracks) {
        CAPTURE(def.id);
        World w(build_track(def), tables, RaceConfig{.countdown_frames = 1, .seed = 1});
        w.add_kart(test::default_params(tables, parts));
        KartInput none;
        w.tick(std::span(&none, 1));
        const Track& track = w.track();
        // Each run of floorless samples on the main loop is a gap; the glide ramp sits before it.
        const int n = int(track.center.size());
        std::vector<std::pair<int, int>> gaps; // [first, last] sample over the chasm
        for (int i = 0; i < n; ++i) {
            if (track.center[size_t(i)].has_floor || (i > 0 && !track.center[size_t(i - 1)].has_floor)) continue;
            int j = i;
            while (j + 1 < n && !track.center[size_t(j + 1)].has_floor) ++j;
            gaps.push_back({i, j});
        }
        REQUIRE(!gaps.empty());
        const float spacing = track.length / float(n);
        for (const auto& [first, last] : gaps) {
            CAPTURE(first);
            const float gap_m = float(last - first + 1) * spacing;
            float worst_margin = 1e9f;
            std::string worst;
            for (const auto& [key, build] : distinct) {
                const KartParams p = test::default_params(tables, parts, build);
                w.set_kart_params(0, p);
                KartState& k = w.karts_mut()[0];
                k = KartState{};
                const int start = track.wrap(first - int(60.0f / spacing));
                const CenterSample& c = track.center[size_t(start)];
                k.pos = c.pos + c.up * 0.1f;
                k.rot = heading(c.tangent, c.up);
                k.speed = p.top_speed;
                k.sample = start;
                k.last_safe_sample = start;
                k.checkpoint = 0;
                bool landed = false, fell = false, opened = false;
                int frames = 0;
                while (!landed && !fell && frames < 60 * 12) {
                    KartInput in = autopilot(k, track);
                    in.pitch = 0.0f;
                    w.tick(std::span(&in, 1));
                    ++frames;
                    for (const auto& e : w.events()) {
                        opened |= e.type == KartEventType::GliderOpen;
                        fell |= e.type == KartEventType::Respawn;
                    }
                    const int at = ((k.sample - first) % n + n) % n;
                    landed = opened && k.grounded && at > last - first && at < n / 2;
                }
                CAPTURE(build.driver);
                CAPTURE(build.body);
                CAPTURE(build.tires);
                CAPTURE(build.glider);
                CHECK(opened);
                CHECK(landed);
                CHECK_FALSE(fell);
                if (landed) {
                    const float margin = float(((k.sample - first) % n + n) % n - (last - first + 1)) * spacing;
                    if (margin < worst_margin) {
                        worst_margin = margin;
                        worst = "driver " + std::to_string(build.driver) + " body " + std::to_string(build.body) + " tires " +
                                std::to_string(build.tires) + " glider " + std::to_string(build.glider);
                    }
                }
            }
            MESSAGE(def.id << ": " << gap_m << " m gap, worst margin " << worst_margin << " m past it (" << worst << ")");
            ++gaps_flown;
        }
    }
    CHECK(gaps_flown >= 12);
}
