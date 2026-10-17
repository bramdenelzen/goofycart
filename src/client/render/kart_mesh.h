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
MeshData make_kart_mesh(const KartLook& look);
MeshData make_glider_mesh(const sim::PartLook& glider);

// 0xRRGGBB from the data files to the renderer's RGBA8.
constexpr uint32_t hex_rgba(uint32_t rgb) { return rgba(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

} // namespace gk::render
