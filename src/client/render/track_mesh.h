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
    enum class Scenery { Trees, Cactus, Harbor, Pines, Mesas, Lava, Clouds, City, Market } scenery;
};
const ThemeStyle& theme_style(const std::string& theme);

// Render mesh for a track: per-surface colours, a checkered start line,
// two-tone walls and a dark underside so the floating track reads as solid.
// `skip_obstacles` leaves the obstacles out, for drawing them as props.
MeshData make_track_mesh(const sim::Track& track, bool skip_obstacles = false);
// Low-poly scenery placed around the track, by theme.
// With `props`, the trees, rocks, buildings, clouds and other props are not
// built into the mesh but listed there, to be drawn with the painted models
// (gobby_assets.h); their islands and ground stay in the mesh.
struct PropPlacement {
    std::string name;
    mat4 transform;
    // Fit the model's bounds to x, z in -1..1 and y in 0..1 first (obstacles).
    bool fit = false;
};
MeshData make_scenery_mesh(const sim::Track& track, std::vector<PropPlacement>* props = nullptr);

MeshData make_spark_mesh();
MeshData make_flame_mesh();

} // namespace gk::render
