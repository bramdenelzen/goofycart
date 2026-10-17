#pragma once
#include "gk/core/math.h"
#include "gk/sim/surface.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gk::sim {

struct TrackTriangle {
    uint32_t a, b, c;
    Surface surface;
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

class Track {
public:
    Track();
    ~Track();
    Track(Track&&) noexcept;
    Track& operator=(Track&&) noexcept;

    std::string id;
    std::string name;
    std::string theme = "meadow";  // palette and scenery, for the client
    std::vector<vec3> vertices;
    std::vector<TrackTriangle> triangles;
    std::vector<CenterSample> center;
    std::vector<int> checkpoints;    // sample index where each checkpoint region starts; [0] = start line
    float length = 0.0f;             // centre spline length in metres
    float kill_y = -100.0f;          // falling below this triggers a respawn
    int laps = 3;
    std::vector<vec3> item_boxes;    // centres, in rows across the road

    // Solid things standing on the road: prisms on the road's frame. Their
    // faces are in `triangles` (as walls), from first_tri on, so the client
    // can draw them as props instead of track.
    struct Obstacle {
        vec3 pos;                    // base centre
        vec3 forward, right, up;
        vec3 half;                   // across, up, along (m)
        int sides = 4;
        int sample = 0;
        uint32_t first_tri = 0, tri_count = 0;
    };
    std::vector<Obstacle> obstacles;

    // The CPU drivers' racing line, one entry per centre sample, built by
    // finalize(): lateral offset along `right`, the drivable band around the
    // centre and the line's signed curvature (1/m, positive turning right).
    struct LinePoint {
        float offset = 0.0f;
        float lo = 0.0f, hi = 0.0f;
        float curvature = 0.0f;
        bool blocked = false;        // an obstacle narrows the band here
    };
    std::vector<LinePoint> line;
    bool closed = true;              // the centre line loops (a lap circuit)

    // Builds the collision structure and the racing line. Call after filling the geometry.
    void finalize();
    vec3 line_point(int sample) const;
    int wrap(int sample) const;

    std::optional<RayHit> raycast(vec3 origin, vec3 dir, float max_dist) const;
    // Contacts between a sphere and the track (walls and floors).
    void collide_sphere(vec3 center, float radius, std::vector<WallContact>& out) const;

    // Index of the centre sample nearest to `p`, searching around `hint` first.
    int nearest_sample(vec3 p, int hint = -1) const;
    // Which checkpoint region a sample belongs to.
    int checkpoint_of(int sample) const;
    // Start grid: position and facing for each of up to 12 slots.
    struct Slot { vec3 pos; vec3 forward; };
    Slot grid_slot(int index) const;
    int respawn_sample_before(int sample) const;

private:
    std::unique_ptr<TrackCollision> collision_;
};

// The greybox test track: hills, a trick ramp, a glide ramp over a chasm,
// boost pads and every surface class.
Track make_greybox_track();

} // namespace gk::sim
