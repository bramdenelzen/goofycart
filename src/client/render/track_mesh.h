#pragma once
#include "mesh.h"

#include "gk/sim/track.h"

namespace gk::render {

// Render mesh for a track: per-surface colours, a checkered start line,
// red/white walls and a dark underside so the floating track reads as solid.
MeshData make_track_mesh(const sim::Track& track);
// Low-poly scenery placed around the track (trees on islands, clouds).
MeshData make_scenery_mesh(const sim::Track& track);

MeshData make_glider_mesh(uint32_t color);
MeshData make_spark_mesh();
MeshData make_flame_mesh();

} // namespace gk::render
