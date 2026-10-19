#pragma once
#include "mesh.h"

#include "gk/sim/track.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace gk::render {

// Colours, sky and scenery for a track theme (meadow, desert, harbor, snow,
// canyon, volcano, sky, night). Unknown themes fall back to meadow.
struct ThemeStyle {
    uint32_t road, offroad_light, offroad_medium, offroad_heavy, sand, ice, water, wall_a, wall_b, underside, below;
    vec3 sky;
    float fog_density;
    float ambient;
    enum class Scenery { Trees, Cactus, Harbor, Pines, Mesas, Lava, Clouds, City, Market, Beach } scenery;
};
const ThemeStyle& theme_style(const std::string& theme);

// Render mesh for a track: per-surface colours, a checkered start line,
// two-tone walls and a dark underside so the floating track reads as solid.
// `skip_obstacles` leaves the obstacles out, for drawing them as props.
// `group`: only the triangles of that group (0 the fixed track, k > 0 branch
// k - 1's road that comes and goes); -1 all of them.
MeshData make_track_mesh(const sim::Track& track, bool skip_obstacles = false, int group = 0);
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
// The terrain's height at (x, z) exactly as its mesh is triangulated, for
// tracks with `ground`; NaN where there is none.
std::function<float(float, float)> make_terrain_sampler(const sim::Track& track);
// The colour of the lamps in the tunnel nearest `pos` (its style's, or the theme's).
vec3 tunnel_light_color(const sim::Track& track, vec3 pos);

// Where a volcano track's lava shows: the pool (the out-of-bounds floor)
// where the ground dips below it, by the shader's 4 m bubble cells, and the
// middle line of every lava zone; plus spots for lights along it all.
struct LavaSpots {
    bool pool = false;
    float pool_y = 0.0f;
    std::unordered_set<int64_t> pool_cells; // key(x, z) of 4 m cells
    struct Line {
        vec3 pos, right, tangent;
        float half; // half the lava's width across the road
        int zone;
    };
    std::vector<Line> zone_lines; // one per road sample a lava zone covers
    struct Light {
        vec3 pos;
        int zone; // -1: the pool (always there)
    };
    std::vector<Light> lights;
    static int64_t key(int x, int z) { return (int64_t(x) << 32) | int64_t(uint32_t(z)); }
};
LavaSpots find_lava(const sim::Track& track);

MeshData make_spark_mesh();
MeshData make_flame_mesh();
// A unit sphere for the fireball shader (explosions).
MeshData make_fireball_mesh();
// The track's oil spills (Surface::Oil), lifted just off the road, for the oil shader.
MeshData make_oil_mesh(const sim::Track& track);
// What a zone puts on the road while it's on (a flood, ice, lava...), lifted just off it.
MeshData make_zone_mesh(const sim::Track& track, const sim::Track::Zone& zone);
// A barrier in its own frame (x across, y up, z along), standing at full height.
MeshData make_barrier_mesh(const sim::Track::Barrier& barrier);
// A switch pad in its own frame, flat on the road: raised and red, or pressed and green.
MeshData make_switch_mesh(float radius, bool on);
// A vent on the floor of deep water: a rocky crater around the hole, at the origin.
MeshData make_vent_mesh(float radius);
// A current: chevrons along the floor of its stretch, pointing the way it carries.
MeshData make_current_mesh(const sim::Track& track, const sim::Track::Current& current);
// A puddle: an irregular flat disc about a unit across, for a dropped oil slick.
MeshData make_oil_disc_mesh();

} // namespace gk::render
