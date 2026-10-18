#pragma once
// Painted walls, wall rails and posts, kerbs and flags for a track.
#include "gobby_assets.h"

#include "gk/sim/track.h"

namespace gk::render {

PaintedModel make_trackside(const sim::Track& track);

} // namespace gk::render
