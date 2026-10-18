#pragma once
// Painted walls, wall rails and posts, kerbs and flags for a track.
#include "gobby_assets.h"

#include "gk/sim/track.h"

namespace gk::render {

// `group` 0: the fixed track; k > 0: only branch k - 1's road (one that comes and goes).
PaintedModel make_trackside(const sim::Track& track, int group = 0);

} // namespace gk::render
