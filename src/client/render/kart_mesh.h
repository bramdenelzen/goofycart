#pragma once
// Karts and gliders built from the parts a racer picked: body shape, tyre
// profile, the driver's colours, size and hat, and the glider style.
#include "mesh.h"

#include "gk/sim/stats.h"

namespace gk::render {

struct KartLook {
    uint32_t paint = 0xffffffff; // body colour (RGBA8), the racer's slot colour
    sim::PartLook driver, body, tires, glider;
};

KartLook kart_look(const sim::PartsCatalog& parts, const sim::Build& build, uint32_t paint);

// Where the pieces of a kart sit, for placing loaded models in place of the
// procedural ones: wheel centres (y = radius), the driver's seat, the hands.
struct KartLayout {
    struct Wheel {
        vec3 center;
        float radius;
    };
    std::vector<Wheel> wheels;
    vec3 seat{0, 0.5f, -0.2f};
    vec3 hands{0, 0.85f, 0.35f};
    bool bike = false;
    float lift = 0.0f; // how far big tyres raise the body
};
// Which pieces to build; the layout comes out the same whatever is built.
enum KartParts : unsigned { KartBody = 1, KartDriver = 2, KartWheels = 4, KartAll = 7 };
MeshData make_kart_mesh(const KartLook& look, KartLayout* layout = nullptr, unsigned parts = KartAll);
MeshData make_glider_mesh(const sim::PartLook& glider);

// 0xRRGGBB from the data files to the renderer's RGBA8.
constexpr uint32_t hex_rgba(uint32_t rgb) { return rgba(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

} // namespace gk::render
