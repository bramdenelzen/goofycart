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
                      Barrier, Switch, Zone, Tunnel, Bank, Current, Vent, Grip, Conveyor, Wave, Loop };
    Kind kind = Kind::Road;
    TrackPos at, to;        // ramps, pads and boxes use `at`; ranges use at..to
    float length = 8.0f;    // ramps, pads and obstacles (m, along the road)
    float height = 1.5f;    // ramps and obstacles (m)
    float gap = 0.0f;       // glide ramps: the chasm after the lip (m)
    float width = 9.0f;     // verges and obstacles (m, across the road)
    float offset = 0.0f;    // obstacles: from the centre line, + to the right (m)
    int sides = 4;          // obstacles: 4 a block, 8 a round pillar
    std::string prop;       // obstacles: "solid", "crate", "barrel", "crystal", "tires" or "" (by theme)
    // Movers: "platform", "block", "press" or "swing"; swing in the road's
    // frame (across, up, along, metres), seconds per swing, phase 0..1, ramp
    // length (m). A press drops `drop` metres onto the road; a swing hangs
    // `arm` metres under its pivot and sways `angle` degrees either way.
    // Waves: `period` seconds per wave, `phase` 0..1.
    enum class MoverKind : uint8_t { Platform, Block, Press, Swing };
    MoverKind mover = MoverKind::Platform;
    vec3 travel{0.0f};
    float period = 4.0f;
    float phase = 0.0f;
    float ramp = 2.0f;
    float drop = 3.0f;
    float arm = 6.0f;
    // Waves: the road rises and falls `amplitude` metres in rolling waves
    // `wavelength` metres long that travel with the traffic.
    float amplitude = 0.6f;
    float wavelength = 12.0f;
    bool boost = false;     // movers: the platform's deck is a dash panel
    int lanes = 0b1111;     // boost pads: which of the 4 lanes (bit 0 = leftmost)
    bool left = true, right = true; // verges and walls: which side; zones: which half of the road
    Surface surface = Surface::Road;
    int gate = -1;          // barriers stand, zones are on and obstacles are there while this gate holds
    bool deep = false;      // water zones: "depth": "deep", driven under in the underwater mode
    float level = 1e9f;     // deep water: the surface's height (m); unset = the road's height at the zone's lower end
    std::string name;       // switches: what gates call it; tunnels: the style (rock, masonry, ice, concrete, lava; "" by theme)
    float hold = 10.0f;     // switches: seconds a press lasts
    float angle = 0.0f;     // banks: degrees, + the right side lower (banked into a right turn); swings: the sway
    // Currents (deep water) and conveyors (dry road): m/s along the road (+
    // with the traffic) and across it (+ to the right). Vents: the lift (m/s up) and, if timed,
    // seconds blowing out of each `period` (0: always).
    float push = 0.0f, drift = 0.0f;
    float lift = 12.0f;
    // Loops: a vertical loop spliced into the road at `at`, `radius` metres
    // round and shifted `shift` metres sideways (+ right) so its exit lands
    // beside its entry.
    float radius = 12.0f, shift = 0.0f;
    float open = 0.0f;
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

// A track as data: a spline through control points (x, y, z, and an
// optional road half-width) plus features placed along it. The geometry,
// centre line, checkpoints and item boxes are generated from it. The spline
// is a closed loop raced in laps, or (point_to_point) an open road from a
// start line to a finish line, raced once and reported in sections.
struct TrackDef {
    std::string id;
    std::string name;
    std::string theme = "meadow"; // the client's palette and scenery
    bool ground = false;          // the track sits on rolling terrain instead of floating islands
    int laps = 3;                 // point-to-point: the number of sections
    bool point_to_point = false;
    TrackPos start{0.0f, 60.0f};  // point-to-point: the start line (the grid forms behind it)
    TrackPos finish{-1.0f, 0.0f}; // point-to-point: the finish line (ctrl < 0: 80 m before the road's end)
    std::vector<TrackPos> sections; // point-to-point: where each section after the first begins (empty: equal parts)
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
