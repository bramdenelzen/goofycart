#pragma once
#include "gk/sim/track.h"

#include <optional>
#include <string>
#include <vector>

namespace gk::sim {

// A position along a track's control points: control index plus metres.
struct TrackPos {
    float ctrl = 0.0f;
    float metres = 0.0f;
};

// One piece of a track layout. Which fields matter depends on the kind.
struct TrackFeature {
    enum class Kind { TrickRamp, GlideRamp, BoostPad, Road, Verge, ItemBoxes, NoWalls, Obstacle, Stairs, Mover,
                      Barrier, Switch, Zone };
    Kind kind = Kind::Road;
    TrackPos at, to;        // ramps, pads and boxes use `at`; ranges use at..to
    float length = 8.0f;    // ramps, pads and obstacles (m, along the road)
    float height = 1.5f;    // ramps and obstacles (m)
    float gap = 0.0f;       // glide ramps: the chasm after the lip (m)
    float width = 9.0f;     // verges and obstacles (m, across the road)
    float offset = 0.0f;    // obstacles: from the centre line, + to the right (m)
    int sides = 4;          // obstacles: 4 a block, 8 a round pillar
    std::string prop;       // obstacles: "solid", "crate", "barrel", "crystal", "tires" or "" (by theme)
    // Movers: "platform" or "block"; swing in the road's frame (across, up,
    // along, metres), seconds per swing, phase 0..1, ramp length (m).
    bool block = false;
    vec3 travel{0.0f};
    float period = 4.0f;
    float phase = 0.0f;
    float ramp = 2.0f;
    bool boost = false;     // movers: the platform's deck is a dash panel
    int lanes = 0b1111;     // boost pads: which of the 4 lanes (bit 0 = leftmost)
    bool left = true, right = true; // verges and walls: which side; zones: which half of the road
    Surface surface = Surface::Road;
    int gate = -1;          // barriers stand, zones are on and obstacles are there while this gate holds
    bool deep = false;      // water zones: "depth": "deep", driven under in the underwater mode
    float level = 1e9f;     // deep water: the surface's height (m); unset = the road's height at the zone's lower end
    std::string name;       // switches: what gates call it
    float hold = 10.0f;     // switches: seconds a press lasts
};

// A gate as written in a track file (see Track's Gate); a switch by name.
struct GateDef {
    Gate gate;
    std::string sw;
};

// A side road: from a point on the main loop to a later one, through its own
// control points (x, y, z, optional half-width). It starts and ends level
// with the main road, alongside it, and the builder makes the split and the
// merge: a stretch of shared road, a grass island narrowing to a nose.
struct BranchDef {
    std::string name;
    TrackPos from, to;            // where it leaves and rejoins the main loop
    int side = 1;                 // +1 off the right of the main road, -1 the left
    float half_width = 0.0f;      // 0: the main road's default
    float junction = 22.0f;       // metres of road shared side by side at each end
    float verge = -1.0f;          // < 0: the track's
    Surface verge_surface = Surface::Count; // Count: the track's
    Surface surface = Surface::Road;        // the branch's own road surface (dirt shortcuts...)
    std::vector<vec4> points;
    std::vector<TrackFeature> features;     // positions in metres from where it leaves
    int gate = -1;                // open while this holds
    bool vanish = false;          // shut: the road itself disappears (else a barrier blocks its mouth)
    float ai_share = 0.5f;
};

// A track as data: a closed spline through control points (x, y, z, and an
// optional road half-width) plus features placed along it. The geometry,
// centre line, checkpoints and item boxes are generated from it.
struct TrackDef {
    std::string id;
    std::string name;
    std::string theme = "meadow"; // the client's palette and scenery
    bool ground = false;          // the track sits on rolling terrain instead of floating islands
    int laps = 3;
    float half_width = 7.0f;
    float verge = 5.0f;           // default verge width each side (m)
    Surface verge_surface = Surface::OffroadLight;
    std::vector<vec4> points;     // w = half-width, 0 for the default
    std::vector<TrackFeature> features;
    std::vector<BranchDef> branches;
    std::vector<GateDef> gates;

};

Track build_track(const TrackDef& def);
bool parse_track_def(const std::string& json_text, TrackDef& def, std::string* error = nullptr);
bool load_track_def(const std::string& path, TrackDef& def, std::string* error = nullptr);

// The track list and cups, from data/tracks/tracks.json.
struct Cup {
    std::string name;
    std::vector<std::string> tracks; // ids
};
struct TrackCatalog {
    std::vector<TrackDef> tracks;
    std::vector<Cup> cups;
    bool load(const std::string& index_path, std::string* error = nullptr);
    int find(const std::string& id) const;
};

// Builds the track with this id from the catalog, or the greybox loop when
// the id is unknown or empty.
Track make_track(const TrackCatalog& catalog, const std::string& id);

// The greybox test track as a definition (make_greybox_track builds it).
TrackDef greybox_track_def();

} // namespace gk::sim
