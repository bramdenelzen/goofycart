#pragma once
#include <string>
#include "gk/sim/input.h"

#include <SDL3/SDL.h>

#include <vector>

namespace gk {

// Client-only actions that never reach the simulation.
struct ClientActions {
    bool look_back = false;
    bool pause_pressed = false;
    bool toggle_resolution_pressed = false;
    bool toggle_help_pressed = false;
    bool restart_pressed = false;
    bool quit_requested = false;
    // Menu navigation (edges, with key repeat for held directions)
    bool menu_up = false, menu_down = false, menu_left = false, menu_right = false;
    bool accept = false, back = false;
    std::string text;     // typed characters this frame (when text input is on)
    bool erase = false;   // backspace while typing
    bool paste = false;   // Ctrl/Cmd+V
    bool enter = false;   // Return (accept without the X and space keys, for typing)
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
    bool rumble_enabled = true;

private:
    std::vector<SDL_Gamepad*> pads_;
    ClientActions actions_;
    int stick_dir_ = 0;      // last left-stick menu direction, for edge detection
};

} // namespace gk
