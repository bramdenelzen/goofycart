#pragma once
#include "gk/core/math.h"
#include "gk/sim/surface.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gk::sim {

struct TrackTriangle {
    uint32_t a, b, c;
    Surface surface;
    uint8_t group = 0; // 0 always there; k > 0 the road of Track::branches[k - 1] (gone while its gate is shut)
};

// One sample of the closed 3D spline that runs along the middle of the road.
// It drives lap progress, wrong-way detection, respawn points and the AI.
struct CenterSample {
    vec3 pos;
    vec3 tangent;   // unit, direction of travel
    vec3 right;     // unit, across the road (banked)
    vec3 up;        // unit, road normal
    float s = 0.0f; // distance from the start line along the spline
    float half_width = 6.0f;
    bool respawn_ok = true;
    bool has_floor = true; // false over gaps the glider crosses
    int branch = -1;       // -1 the main loop, else the index in Track::branches
    float u = 0.0f;        // distance along its own road (m): texture runs, not progress
};

struct RayHit {
    float distance = 0.0f;
    vec3 point{};
    vec3 normal{};
    Surface surface = Surface::Road;
};

struct WallContact {
    vec3 normal;     // pointing away from the wall, towards the sphere
    float depth;
    Surface surface;
};

class TrackCollision; // Jolt-backed, defined in track_collision.cpp

// What opens and shuts during a race. Every condition reads only shared race
// state (the race clock, the leader's lap, switch timers in the WorldState),
// so the server and every predicting client agree on it tick by tick.
struct Gate {
    enum class When : uint8_t { Always, Laps, Cycle, Switch };
    When when = When::Always;
    int lap_lo = 1, lap_hi = 99;   // Laps: holds while the leader's lap is in [lo, hi]
    int period = 600, open = 300, offset = 0; // Cycle (ticks): holds for `open` of every `period`, from `offset`
    int sw = -1;                   // Switch: holds while that switch is pressed
    bool invert = false;           // holds exactly when the rest would not
};

class Track {
public:
    Track();
    ~Track();
    Track(Track&&) noexcept;
    Track& operator=(Track&&) noexcept;

    std::string id;
    std::string name;
    std::string theme = "meadow";  // palette and scenery, for the client
    bool ground = false;           // the client draws terrain under and around the road
    std::vector<vec3> vertices;
    std::vector<TrackTriangle> triangles;
    std::vector<CenterSample> center;
    std::vector<int> checkpoints;    // sample index where each checkpoint region starts; [0] = start line
    float length = 0.0f;             // centre spline length in metres
    float kill_y = -100.0f;          // falling below this triggers a respawn
    int laps = 3;
    std::vector<vec3> item_boxes;    // centres, in rows across the road
    // Stretches of road drawn as a flight of steps, (from, to) in metres
    // along the centre line; the kart drives a smooth slope under them.
    std::vector<std::pair<float, float>> stairs;
    bool on_stairs(float s) const {
        for (const auto& [a, b] : stairs)
            if (s >= a && s < b) return true;
        return false;
    }

    // Solid things standing on the road: prisms on the road's frame. Their
    // faces are in `triangles` (as walls), from first_tri on, so the client
    // can draw them as props instead of track.
    // Obstacles that break (crates, barrels, crystals) or topple (tire
    // stacks) when hit, and come back a while later. They have no triangles:
    // the World handles them.
    enum class Breakable : uint8_t { None, Crate, Barrel, Crystal, Tires };
    struct Obstacle {
        vec3 pos;                    // base centre
        vec3 forward, right, up;
        vec3 half;                   // across, up, along (m)
        int sides = 4;
        int sample = 0;
        Breakable breakable = Breakable::None;
        uint32_t first_tri = 0, tri_count = 0;
    };
    std::vector<Obstacle> obstacles;

    // Things that move on a loop: platforms karts drive and land on (with
    // sloped ends), and solid blocks to dodge. Where they are depends only on
    // the world tick, so prediction and rollback need no extra state.
    struct Mover {
        enum class Kind : uint8_t { Platform, Block };
        Kind kind = Kind::Platform;
        vec3 base{0.0f};               // centre of the base at the middle of the swing
        vec3 forward{0, 0, 1}, right{1, 0, 0}, up{0, 1, 0};
        vec3 half{2.0f, 0.4f, 3.0f};   // across, height / 2, along (m)
        vec3 travel{0.0f};             // swing amplitude, world metres: base +- travel
        int period = 240;              // ticks per swing there and back
        float phase = 0.0f;            // 0..1 offset into the swing
        float ramp = 0.0f;             // platforms: length of each sloped end (m)
        Surface deck = Surface::Road;  // platforms: BoostPad makes a moving dash panel
        int sample = 0;
    };
    std::vector<Mover> movers;

    // ---- alternate roads and things that change during the race

    // A side road: leaves the main loop at `fork`, rejoins it at `merge`.
    // Its samples sit in `road` from `first`; their `s` runs from the fork's
    // to the merge's, so progress, checkpoints and placings work on it.
    struct Branch {
        int fork = 0, merge = 0;       // main-loop samples
        int first = 0, count = 0;      // its samples in `road`
        uint32_t first_vertex = 0;     // its vertex grid (kStride per sample, as the main loop's)
        int gate = -1;                 // open while this holds (-1 always)
        bool vanish = false;           // shut: the road itself is gone (else a barrier closes its mouth)
        float ai_share = 0.5f;         // the fraction of CPUs that take it when it's open
        int side = 1;                  // +1 leaves on the right of the main road, -1 the left
        std::string name;
    };
    std::vector<Branch> branches;
    // Every sample of every road: the main loop (= `center`, first), then each
    // branch's. A kart's `sample` indexes this.
    std::vector<CenterSample> road;
    const CenterSample& sample(int i) const { return road[size_t(i)]; }

    std::vector<Gate> gates;
    // Whether each gate holds this tick; set by the World. Empty: everything open.
    std::vector<uint8_t> gate_state;
    bool gate_holds(int g) const { return g < 0 || size_t(g) >= gate_state.size() || gate_state[size_t(g)] != 0; }
    bool branch_open(int b) const { return gate_holds(branches[size_t(b)].gate); }
    // A road that vanishes stays while anyone is on it (set by the World), so
    // it never drops out from under a kart; its mouth is fenced off meanwhile.
    std::vector<uint8_t> branch_busy;
    bool branch_present(int b) const {
        const Branch& br = branches[size_t(b)];
        return !br.vanish || branch_open(b) || (size_t(b) < branch_busy.size() && branch_busy[size_t(b)]);
    }

    // A pad on the road: driving over it presses it for `hold` ticks.
    struct Switch {
        vec3 pos{0.0f};
        vec3 forward{0, 0, 1}, right{1, 0, 0}, up{0, 1, 0};
        float radius = 2.5f;
        int hold = 600;
        int sample = 0;
    };
    std::vector<Switch> switches;

    // A wall that stands while its gate holds and sinks into the road when it
    // doesn't: closing doors, rising bollards, a branch's mouth.
    struct Barrier {
        vec3 pos{0.0f};                // base centre
        vec3 forward{0, 0, 1}, right{1, 0, 0}, up{0, 1, 0};
        vec3 half{4.0f, 1.0f, 0.5f};   // across, height / 2, along (m)
        int gate = -1;
        int sample = 0;
        int branch = -1;               // closes this branch's mouth (drawn as its gate)
    };
    std::vector<Barrier> barriers;
    bool barrier_up(const Barrier& b) const { return gate_holds(b.gate); }

    // A stretch of road whose surface changes while its gate holds: a flood,
    // an oil spill, black ice, a dash strip, lava (out of bounds).
    struct Zone {
        int first = 0, count = 0;      // samples in `road`, consecutive on one road
        float lo = -1e9f, hi = 1e9f;   // across the road (m from its centre, + right)
        Surface surface = Surface::Water;
        int gate = -1;
        // Deep water: a volume the karts drive under, up to `level` (m), in
        // the underwater mode. The floor keeps its own surface.
        bool deep = false;
        float level = 0.0f;
    };
    std::vector<Zone> zones;
    // Whether `p` (on road sample `sample`) is under the water of an active deep zone.
    bool in_deep_water(vec3 p, int sample) const;
    // The surface a zone puts at `p` (on road sample `sample`), if one is active there.
    std::optional<Surface> zone_surface(vec3 p, int sample) const;

    // Deep water only: a stream over a stretch of road that carries karts
    // along it (+ with the traffic) or across it (+ to the right), in m/s.
    struct Current {
        int first = 0, count = 0;      // samples in `road`, consecutive on one road
        float lo = -1e9f, hi = 1e9f;   // across the road (m from its centre, + right)
        float push = 0.0f, drift = 0.0f;
        int gate = -1;
    };
    std::vector<Current> currents;
    // The carry a kart at `p` (on road sample `sample`) gets from the currents, m/s.
    vec3 current_at(vec3 p, int sample) const;

    // A vent on the floor of deep water: a column of bubbles that lifts a kart
    // driving over it, blowing all the time or for `open` of every `period` ticks.
    struct Vent {
        vec3 pos{0.0f};
        vec3 up{0, 1, 0};
        float radius = 3.0f;
        float lift = 12.0f;            // m/s upwards
        int period = 0, open = 0;      // ticks; period 0: always on
        float phase = 0.0f;            // 0..1 of the period
        int sample = 0;
        int gate = -1;
    };
    std::vector<Vent> vents;
    bool vent_active(const Vent& v, double tick) const;

    // A roofed stretch of road: the walls carry on up into an arched roof
    // `height` metres over the road's middle. The roof stops jumps and folds
    // the glider away; the client draws it, lights it and keeps the camera
    // under it.
    struct Tunnel {
        int first = 0, count = 0;      // samples in `road`, consecutive on one road
        float height = 7.0f;           // the roof over the middle of the road (m)
        std::string style;             // rock, masonry, ice, concrete or lava
        std::vector<float> half_span;  // per sample: the walls' distance from the centre (m)
        // The roof's height over the road at `lat` metres from the centre: the
        // walls rise straight to 55% of the height, then the arch.
        float roof_at(int k, float lat) const {
            const float span = half_span.empty() ? 8.0f : half_span[size_t(std::clamp(k, 0, int(half_span.size()) - 1))];
            const float e = std::min(std::abs(lat) / std::max(span, 0.1f), 1.0f);
            const float spring = 0.55f * height;
            return spring + (height - spring) * std::sqrt(1.0f - e * e);
        }
    };
    std::vector<Tunnel> tunnels;
    // The roof's world height above `p` on road sample `sample`, if that
    // sample is under one.
    std::optional<float> tunnel_roof(vec3 p, int sample) const;
    const Tunnel* tunnel_at(int sample) const;

    // Walking the roads: the sample `steps` on (or back), taking the open
    // branches whose bit is set in `route` at their forks.
    int next(int i, uint32_t route = 0) const;
    int prev(int i) const;
    int advance(int i, int steps, uint32_t route = 0) const;
    // The main-loop sample level with road sample `i` (same distance from the line).
    int main_sample(int i) const;
    // A road's samples, for drawing: [0] the main loop, then one per branch.
    struct Strip { int first, count; uint32_t first_vertex; bool closed; int branch; };
    std::vector<Strip> strips() const;
    // The world tick the movers stand at for collision (set by the World).
    double mover_time = 0.0;
    vec3 mover_pos(size_t i, double tick) const;
    // Height of a mover's top above its base at local (x, z), or < 0 off it.
    float mover_top(const Mover& m, float x, float z) const;

    // The CPU drivers' racing line, one entry per centre sample, built by
    // finalize(): lateral offset along `right`, the drivable band around the
    // centre and the line's signed curvature (1/m, positive turning right).
    struct LinePoint {
        float offset = 0.0f;
        float lo = 0.0f, hi = 0.0f;
        float curvature = 0.0f;
        bool blocked = false;        // an obstacle narrows the band here
    };
    std::vector<LinePoint> line;     // one per `road` sample
    bool closed = true;              // the centre line loops (a lap circuit)

    // Builds the collision structure and the racing line. Call after filling the geometry.
    void finalize();
    vec3 line_point(int sample) const;
    int wrap(int sample) const;

    std::optional<RayHit> raycast(vec3 origin, vec3 dir, float max_dist) const;
    // Contacts between a sphere and the track (walls and floors).
    void collide_sphere(vec3 center, float radius, std::vector<WallContact>& out) const;

    // Index of the road sample nearest to `p`, searching around `hint` first.
    int nearest_sample(vec3 p, int hint = -1) const;
    // Which checkpoint region a road sample belongs to.
    int checkpoint_of(int sample) const;
    // Start grid: position and facing for each of up to 12 slots.
    struct Slot { vec3 pos; vec3 forward; };
    Slot grid_slot(int index) const;
    int respawn_sample_before(int sample) const;

private:
    bool group_present(size_t group) const;
    std::vector<std::unique_ptr<TrackCollision>> collision_;
};

// The greybox test track: hills, a trick ramp, a glide ramp over a chasm,
// boost pads and every surface class.
Track make_greybox_track();

} // namespace gk::sim
