#pragma once
#include <cstdint>

namespace gk::sim {

// Terrain classes, as MK8 groups its floor collision types. Every track
// triangle carries one (a material slot in the glTF export).
enum class Surface : uint8_t {
    Road,
    OffroadLight,   // short grass, dirt
    OffroadMedium,  // tall grass, gravel
    OffroadHeavy,   // mud, deep grass, snow drifts
    Sand,           // slippery, scaled by off-road traction
    Ice,            // slippery, scaled by off-road traction
    Water,          // underwater sections use the water stats
    BoostPad,       // dash panel
    GlideRamp,      // blue ramp that opens the glider
    TrickRamp,      // ramp lip that allows a trick
    Wall,
    OutOfBounds,    // lava, void, deep water: respawn
    Count
};

constexpr bool is_offroad(Surface s) {
    return s == Surface::OffroadLight || s == Surface::OffroadMedium || s == Surface::OffroadHeavy;
}
constexpr bool is_launch_surface(Surface s) {
    return s == Surface::GlideRamp || s == Surface::TrickRamp || s == Surface::BoostPad;
}
const char* surface_name(Surface s);

} // namespace gk::sim
