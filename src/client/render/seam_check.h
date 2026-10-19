#pragma once
#include "gk/core/math.h"
#include "gk/sim/track.h"

#include <string>
#include <vector>

namespace gk::render {

// Where a track's meshes don't meet: found from the same geometry the game
// draws, for docs/SEAMS.md and the client's --seam-tour.
struct SeamIssue {
    std::string kind;   // crack, step, overlap, terrain, floating, clearance
    vec3 pos{0.0f};
    int sample = 0;     // nearest road sample
    float size = 0.0f;  // metres: crack width, step height, overlap gap, poke depth, float height, clearance
    int count = 1;      // findings merged into this one (within a few metres)
    std::string what;   // a few words on what is where
};

// Cracks and steps between floors that should meet, floors overlapping close
// enough to z-fight, terrain poking up through the road, props standing in
// the air, and bridges too low over the road beneath.
std::vector<SeamIssue> find_seams(const sim::Track& track);

} // namespace gk::render
