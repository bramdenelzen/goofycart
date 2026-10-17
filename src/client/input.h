#pragma once
#include "gk/sim/input.h"

#include <SDL3/SDL.h>

#include <vector>

namespace gk {

// Client-only actions that never reach the simulation.
struct ClientActions {
    bool look_back = false;
    bool pause_pressed = false;
    bool toggle_resolution_pressed = false;
    bool quit_requested = false;
};

// Merges keyboard and every connected gamepad into one KartInput. SDL3's
// gamepad API maps Xbox, PlayStation, Switch Pro and generic pads to one
// layout, and handles hot-plugging.
class InputMapper {
public:
    ~InputMapper();
    void handle_event(const SDL_Event& e);
    // Call once per rendered frame before sampling.
    void begin_frame();
    sim::KartInput sample() const;
    const ClientActions& actions() const { return actions_; }
    void rumble(float low, float high, int ms);
    size_t gamepad_count() const { return pads_.size(); }

    float deadzone = 0.18f;    // radial dead zone on the steering stick
    float steer_curve = 1.5f;  // response exponent outside the dead zone

private:
    std::vector<SDL_Gamepad*> pads_;
    ClientActions actions_;
};

} // namespace gk
