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

// The driver cut into pieces at its joints, so the client can flop them about
// like a rubber doll. Each piece's mesh sits in its own joint's space, and
// `offset` is where that joint sits in its parent's space; with every joint at
// rest the pieces assemble into exactly the welded driver `KartDriver` builds.
struct DriverRig {
    enum Piece { Pelvis, Torso, Head, UpperL, UpperR, ForeL, ForeR, PieceCount };
    MeshData piece[PieceCount];
    vec3 offset[PieceCount]{};
};
// Each piece's parent; Pelvis is the root and its offset is in kart space.
inline constexpr int kDriverParent[DriverRig::PieceCount] = {
    -1, DriverRig::Pelvis, DriverRig::Torso, DriverRig::Torso, DriverRig::Torso, DriverRig::UpperL, DriverRig::UpperR};
// `layout` must be the one `make_kart_mesh` filled in, so the pieces land
// where the welded driver would (seat and hands already carry the tyre lift).
DriverRig make_driver_rig(const KartLook& look, const KartLayout& layout);

// 0xRRGGBB from the data files to the renderer's RGBA8.
constexpr uint32_t hex_rgba(uint32_t rgb) { return rgba(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

} // namespace gk::render
