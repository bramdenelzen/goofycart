#pragma once
// Gobby Kart's own models: smooth meshes with hand-painted-style textures,
// built in code so they need no files, and exportable as OBJ + PNG for
// editing. Items (what the karts throw and pick up) and map props (what
// stands beside the tracks).
#include "mesh.h"
#include "paint.h"

#include "gk/sim/stats.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gk::render {

struct PaintedModel {
    MeshData mesh;
    Canvas texture{512};
    bool two_sided = false;
    bool blended = false; // see-through by texture alpha
    bool glow = false;    // texture alpha marks self-lit parts (windows, neon, lamps)
    bool unlit = false;   // drawn as painted, no lighting (light pools)
    MeshData glass;       // drawn see-through, after `mesh` (same texture)
    vec3 bounds_min{0.0f}, bounds_max{0.0f};
    void finish();
};

// Items, by the names the game draws them under: banana, green_shell,
// red_shell, spiked_shell, bomb, item_box, bullet, chomper, boomerang,
// mushroom, golden_mushroom, star, lightning, blooper, horn, coin, fireball.
std::map<std::string, PaintedModel> make_gobby_items();

// Map props, by name: tree_round, tree_pine, tree_palm, bush, flower, rock,
// boulder, lamp_post, billboard, tire_stack, cone, crate, barrel, cactus,
// snowman, crystal, boat, building, tower, lava_rock, cloud, mesa.
std::map<std::string, PaintedModel> make_gobby_props();

// The standard kart, painted in a racer's colour (RGBA8). Faces +Z, wheels
// at x = +-0.62/0.64, z = +-0.66, the seat and steering wheel where the
// procedural driver sits.
PaintedModel make_gobby_kart(uint32_t paint_rgba8);
// A tyre and rim for a tyre part's look: radius 0.5 on the X axis, hub +X.
PaintedModel make_gobby_tire(const sim::PartLook& look);

// Items after Albert's reference paintings (gobby_items2.cpp).
PaintedModel make_glass_item_box();
PaintedModel make_banana_peel();
PaintedModel make_tnt_bundle();
PaintedModel make_turtle_shell(vec4 color, bool spiked_and_winged);

// Builds named models on every core at once (painting is pure CPU work and
// the models are independent), returning them by name.
using ModelJob = std::pair<std::string, std::function<PaintedModel()>>;
std::map<std::string, PaintedModel> build_models_parallel(std::vector<ModelJob> jobs);
// Runs `n` independent jobs across the cores and waits for them all.
void parallel_for(int n, const std::function<void(int)>& job);

// Writes <dir>/<name>/<name>.obj, .mtl and .png.
bool export_painted_model(const PaintedModel& model, const std::string& dir, const std::string& name, std::string* error = nullptr);

} // namespace gk::render
