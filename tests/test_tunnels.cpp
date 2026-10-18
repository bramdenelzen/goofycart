// Tunnels: the track format, the roof's shape, and what the roof does to a
// kart (no glider under it, jumps stop at it).
#include "sim_fixture.h"

#include "gk/sim/track_def.h"

#include <algorithm>

using namespace gk;
using namespace gk::sim;

namespace {

// A flat straight with a 6 m roof over samples [100, 150), z = 100 .. 200 m.
Track roofed_track() {
    Track t = test::flat_track();
    Track::Tunnel tn;
    tn.first = 100, tn.count = 50;
    tn.height = 6.0f;
    tn.half_span.assign(50, 10.0f);
    t.tunnels.push_back(tn);
    return t;
}

struct Bench {
    PhysicsTables tables = test::tables();
    PartsCatalog parts = test::parts();
    KartParams params = test::default_params(tables, parts);
    Track track = roofed_track();
    KartState kart;
    std::vector<KartEvent> events;

    explicit Bench(vec3 at) {
        kart = test::kart_at(at);
        kart.sample = track.nearest_sample(kart.pos);
    }
    void step(const KartInput& in) {
        KartStepContext ctx{track, tables.constants, params, 0, false, &events};
        kart_step(kart, in, ctx);
        kart_update_progress(kart, track);
    }
};

} // namespace

TEST_CASE("tunnels: read from a track file, roofed over their stretch only, walled on both sides") {
    TrackDef d;
    std::string err;
    REQUIRE_MESSAGE(parse_track_def(R"({"id": "t", "name": "T", "half_width": 6,
        "points": [[0, 0, 0], [0, 0, 120], [80, 0, 120], [80, 0, 0]],
        "features": [{"type": "tunnel", "from": [1, 10], "to": [2, 10], "height": 6.5, "style": "masonry"},
                     {"type": "no_walls", "from": [1, 0], "to": [2, 30]}]})",
                                    d, &err),
                    err);
    const Track t = build_track(d);
    REQUIRE(t.tunnels.size() == 1);
    const Track::Tunnel& tn = t.tunnels[0];
    CHECK(tn.style == "masonry");
    CHECK(tn.height == doctest::Approx(6.5f));
    CHECK(tn.count > 20);
    const CenterSample& mid = t.sample(tn.first + tn.count / 2);
    const auto roof = t.tunnel_roof(mid.pos, tn.first + tn.count / 2);
    REQUIRE(roof.has_value());
    CHECK(*roof == doctest::Approx(mid.pos.y + 6.5f));
    // Lower towards the walls: an arch.
    CHECK(*t.tunnel_roof(mid.pos + mid.right * 6.0f, tn.first + tn.count / 2) < *roof);
    CHECK_FALSE(t.tunnel_roof(t.sample(0).pos, 0).has_value());
    // The tunnel keeps its walls where the rest of that stretch has none.
    int walls = 0;
    for (const auto& tri : t.triangles)
        if (tri.surface == Surface::Wall) {
            const vec3 c = (t.vertices[tri.a] + t.vertices[tri.b] + t.vertices[tri.c]) / 3.0f;
            const int s = t.nearest_sample(c);
            walls += s > tn.first + 2 && s < tn.first + tn.count - 2;
        }
    CHECK(walls >= 4 * (tn.count - 6)); // two triangles a side per segment, all the way through
    REQUIRE_MESSAGE(!parse_track_def(R"({"id": "t", "name": "T", "points": [[0,0,0],[0,0,120],[80,0,120],[80,0,0]],
        "features": [{"type": "tunnel", "from": [1, 10], "to": [2, 10], "height": 2}]})", d, &err), "a 2 m roof is refused");
}

TEST_CASE("tunnels: a jump stops at the roof and comes back down") {
    Bench b({0.0f, 0.0f, 120.0f});
    b.kart.grounded = false;
    b.kart.air_vel = vec3(0.0f, 30.0f, 10.0f); // far more than the roof allows
    float top = 0.0f;
    for (int f = 0; f < 90; ++f) b.step(KartInput{}), top = std::max(top, b.kart.pos.y);
    CHECK(top <= 6.0f - 1.4f + 1e-3f);
    CHECK(b.kart.grounded);

    // Outside the tunnel the same jump goes much higher.
    Bench open({0.0f, 0.0f, 20.0f});
    open.kart.grounded = false;
    open.kart.air_vel = vec3(0.0f, 30.0f, 0.0f);
    float open_top = 0.0f;
    for (int f = 0; f < 90; ++f) open.step(KartInput{}), open_top = std::max(open_top, open.kart.pos.y);
    CHECK(open_top > 10.0f);
}

TEST_CASE("tunnels: the glider folds away under the roof") {
    Bench b({0.0f, 3.0f, 95.0f});
    b.kart.grounded = false;
    b.kart.gliding = true;
    b.kart.air_vel = vec3(0.0f, 0.0f, 20.0f);
    for (int f = 0; f < 30; ++f) b.step(KartInput{});
    CHECK_FALSE(b.kart.gliding);
    CHECK(std::count_if(b.events.begin(), b.events.end(), [](const KartEvent& e) { return e.type == KartEventType::GliderClose; }) == 1);
}
