#pragma once
#include <cstdint>

namespace gk::sim {

// Placeholder world for the foundation phase: holds the tick counter that the
// server and client step at a fixed 60 Hz. Karts, the track and items arrive
// in later phases.
class World {
public:
    void tick();
    uint64_t tick_count() const { return tick_; }

private:
    uint64_t tick_ = 0;
};

} // namespace gk::sim
