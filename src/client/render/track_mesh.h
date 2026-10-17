#pragma once
#include "mesh.h"

#include "gk/sim/track.h"

#include <string>

namespace gk::render {

// Colours, sky and scenery for a track theme (meadow, desert, harbor, snow,
// canyon, volcano, sky, night). Unknown themes fall back to meadow.
struct ThemeStyle {
    uint32_t road, offroad_light, offroad_medium, offroad_heavy, sand, ice, water, wall_a, wall_b, underside, below;
    vec3 sky;
    float fog_density;
    float ambient;
    enum class Scenery { Trees, Cactus, Harbor, Pines, Mesas, Lava, Clouds, City } scenery;
};
const ThemeStyle& theme_style(const std::string& theme);

// Render mesh for a track: per-surface colours, a checkered start line,
// two-tone walls and a dark underside so the floating track reads as solid.
MeshData make_track_mesh(const sim::Track& track);
// Low-poly scenery placed around the track, by theme.
MeshData make_scenery_mesh(const sim::Track& track);

MeshData make_spark_mesh();
MeshData make_flame_mesh();

} // namespace gk::render
