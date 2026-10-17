#pragma once
#include <cstdint>

namespace gk::sim {

// Everything a driver can do in one tick. Players, bots and Bullet Bill's
// autopilot all drive through this one struct, so the shared physics is the
// only thing that moves a kart.
struct KartInput {
    float steer = 0.0f;    // -1 (left) .. +1 (right)
    float pitch = 0.0f;    // -1 (nose down) .. +1 (nose up), used while gliding
    bool accelerate = false;
    bool brake = false;    // also reverse
    bool drift = false;    // hop / drift / trick button, held
    bool item = false;     // use item, held (held = trail)
    bool item_back = false;// aim item backwards

    bool operator==(const KartInput&) const = default;
};

} // namespace gk::sim
