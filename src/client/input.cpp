#include "input.h"

#include "gk/core/log.h"

#include <algorithm>
#include <cmath>

namespace gk {
namespace {
float axis(SDL_Gamepad* pad, SDL_GamepadAxis a) { return float(SDL_GetGamepadAxis(pad, a)) / 32767.0f; }
} // namespace

InputMapper::~InputMapper() {
    for (auto* p : pads_) SDL_CloseGamepad(p);
}

void InputMapper::handle_event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* p = SDL_OpenGamepad(e.gdevice.which)) {
            pads_.push_back(p);
            log_info("Gamepad connected: {}", SDL_GetGamepadName(p) ? SDL_GetGamepadName(p) : "unknown");
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (auto it = pads_.begin(); it != pads_.end(); ++it) {
            if (SDL_GetGamepadID(*it) == e.gdevice.which) {
                log_info("Gamepad disconnected");
                SDL_CloseGamepad(*it);
                pads_.erase(it);
                break;
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_START: actions_.pause_pressed = true; break;
        case SDL_GAMEPAD_BUTTON_BACK: actions_.restart_pressed = true; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: actions_.accept = true; break;
        case SDL_GAMEPAD_BUTTON_EAST: actions_.back = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: actions_.menu_up = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: actions_.menu_down = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: actions_.menu_left = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: actions_.menu_right = true; break;
        default: break;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        // Menu keys accept key repeat; the rest do not.
        switch (e.key.key) {
        case SDLK_UP: case SDLK_W: actions_.menu_up = true; break;
        case SDLK_DOWN: case SDLK_S: actions_.menu_down = true; break;
        case SDLK_LEFT: case SDLK_A: actions_.menu_left = true; break;
        case SDLK_RIGHT: case SDLK_D: actions_.menu_right = true; break;
        default: break;
        }
        if (e.key.repeat) break;
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE || e.key.key == SDLK_X) actions_.accept = true;
        if (e.key.key == SDLK_BACKSPACE) actions_.back = true;
        if (e.key.key == SDLK_ESCAPE) {
            actions_.pause_pressed = true;
            actions_.back = true;
        }
        if (e.key.key == SDLK_F2) actions_.toggle_resolution_pressed = true;
        if (e.key.key == SDLK_F1) actions_.toggle_help_pressed = true;
        if (e.key.key == SDLK_R) actions_.restart_pressed = true;
        break;
    case SDL_EVENT_QUIT:
        actions_.quit_requested = true;
        break;
    default:
        break;
    }
}

void InputMapper::begin_frame() {
    const bool quit = actions_.quit_requested;
    actions_ = {};
    actions_.quit_requested = quit;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    actions_.look_back = keys[SDL_SCANCODE_C];
    for (auto* p : pads_)
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST)) actions_.look_back = true;

    // Left stick as a d-pad in menus: one step per push.
    int dir = 0;
    for (auto* p : pads_) {
        const float x = axis(p, SDL_GAMEPAD_AXIS_LEFTX), y = axis(p, SDL_GAMEPAD_AXIS_LEFTY);
        if (std::abs(x) > 0.6f && std::abs(x) > std::abs(y)) dir = x < 0 ? 3 : 4;
        else if (std::abs(y) > 0.6f) dir = y < 0 ? 1 : 2;
        if (dir) break;
    }
    if (dir != stick_dir_) {
        actions_.menu_up |= dir == 1;
        actions_.menu_down |= dir == 2;
        actions_.menu_left |= dir == 3;
        actions_.menu_right |= dir == 4;
        stick_dir_ = dir;
    }
}

sim::KartInput InputMapper::sample() const {
    sim::KartInput in;
    const bool* k = SDL_GetKeyboardState(nullptr);
    float steer = 0.0f, pitch = 0.0f;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) steer -= 1.0f;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) steer += 1.0f;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) pitch -= 1.0f;   // push forward = nose down, as on a stick
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) pitch += 1.0f;
    in.accelerate = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE];
    in.brake = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN];
    in.drift = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_V];
    in.item = k[SDL_SCANCODE_E] || k[SDL_SCANCODE_LCTRL];
    in.item_back = k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN];

    for (auto* p : pads_) {
        // Radial dead zone, then a response curve, so small corrections stay fine.
        const float x = axis(p, SDL_GAMEPAD_AXIS_LEFTX), y = axis(p, SDL_GAMEPAD_AXIS_LEFTY);
        const float mag = std::sqrt(x * x + y * y);
        if (mag > deadzone) {
            const float scaled = std::pow(std::min(1.0f, (mag - deadzone) / (1.0f - deadzone)), steer_curve);
            const float sx = x / mag * scaled, sy = y / mag * scaled;
            if (std::abs(sx) > std::abs(steer)) steer = sx;
            if (std::abs(sy) > std::abs(pitch)) pitch = sy;
            if (sy > 0.5f) in.item_back = true;
        }
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) steer = -1.0f;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) steer = 1.0f;
        // Nintendo layout positions: A = east, B = south on Xbox naming.
        in.accelerate |= SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH) || axis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f;
        in.brake |= SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST);
        in.drift |= SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        in.item |= SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
                   axis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.3f;
    }
    in.steer = std::clamp(steer, -1.0f, 1.0f);
    in.pitch = std::clamp(pitch, -1.0f, 1.0f);
    return in;
}

void InputMapper::rumble(float low, float high, int ms) {
    for (auto* p : pads_)
        SDL_RumbleGamepad(p, Uint16(std::clamp(low, 0.0f, 1.0f) * 65535), Uint16(std::clamp(high, 0.0f, 1.0f) * 65535),
                          Uint32(ms));
}

} // namespace gk
