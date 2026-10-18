#include "settings.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <SDL3/SDL.h>

namespace gk {

const char* bind_name(Bind b) {
    switch (b) {
    case Bind::SteerLeft: return "STEER LEFT";
    case Bind::SteerRight: return "STEER RIGHT";
    case Bind::Accelerate: return "ACCELERATE";
    case Bind::Brake: return "BRAKE / REVERSE";
    case Bind::Drift: return "HOP / DRIFT / TRICK";
    case Bind::Item: return "USE ITEM";
    case Bind::LookBack: return "LOOK BACK";
    case Bind::Up: return "NOSE DOWN / AIM FORWARD";
    case Bind::Down: return "NOSE UP / AIM BACK";
    default: return "?";
    }
}

const char* bind_key(Bind b) {
    static const char* kKeys[kBindCount] = {"key_left", "key_right", "key_accel", "key_brake", "key_drift", "key_item", "key_look_back", "key_up", "key_down"};
    return kKeys[int(b)];
}

std::array<std::array<int, 2>, kBindCount> default_binds() {
    std::array<std::array<int, 2>, kBindCount> b{};
    b[int(Bind::SteerLeft)] = {SDL_SCANCODE_LEFT, SDL_SCANCODE_A};
    b[int(Bind::SteerRight)] = {SDL_SCANCODE_RIGHT, SDL_SCANCODE_D};
    b[int(Bind::Accelerate)] = {SDL_SCANCODE_X, SDL_SCANCODE_SPACE};
    b[int(Bind::Brake)] = {SDL_SCANCODE_Z, SDL_SCANCODE_S};
    b[int(Bind::Drift)] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_V};
    b[int(Bind::Item)] = {SDL_SCANCODE_E, SDL_SCANCODE_LCTRL};
    b[int(Bind::LookBack)] = {SDL_SCANCODE_C, 0};
    b[int(Bind::Up)] = {SDL_SCANCODE_UP, SDL_SCANCODE_W};
    b[int(Bind::Down)] = {SDL_SCANCODE_DOWN, SDL_SCANCODE_S};
    return b;
}

bool Settings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key == "join_address") {
            join_address = line.substr(eq + 1);
            continue;
        }
        bool bound = false;
        for (int b = 0; b < kBindCount; ++b)
            if (key == bind_key(Bind(b))) {
                std::istringstream ss(line.substr(eq + 1));
                int k0 = 0, k1 = 0;
                char comma = 0;
                if (ss >> k0) {
                    binds[size_t(b)][0] = std::clamp(k0, 0, int(SDL_SCANCODE_COUNT) - 1);
                    if (ss >> comma >> k1) binds[size_t(b)][1] = std::clamp(k1, 0, int(SDL_SCANCODE_COUNT) - 1);
                    else binds[size_t(b)][1] = 0;
                }
                bound = true;
            }
        if (bound) continue;
        if (key == "master_url") {
            master_url = line.substr(eq + 1);
            continue;
        }
        int v = 0;
        try {
            v = std::stoi(line.substr(eq + 1));
        } catch (...) {
            continue;
        }
        if (key == "master") master = std::clamp(v, 0, 10);
        else if (key == "music") music = std::clamp(v, 0, 10);
        else if (key == "sfx") sfx = std::clamp(v, 0, 10);
        else if (key == "pixel_look") low_res = v != 0; // was low_res; renamed when full resolution became the default
        else if (key == "fullscreen") fullscreen = v != 0;
        else if (key == "show_help") show_help = v != 0;
        else if (key == "rumble") rumble = v != 0;
        else if (key == "crash_reports") crash_reports = v != 0;
    }
    return true;
}

bool Settings::save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc);
    f << "master=" << master << "\nmusic=" << music << "\nsfx=" << sfx << "\npixel_look=" << low_res
      << "\nfullscreen=" << fullscreen << "\nshow_help=" << show_help << "\nrumble=" << rumble
      << "\ncrash_reports=" << crash_reports << "\nmaster_url=" << master_url << "\njoin_address=" << join_address << "\n";
    for (int b = 0; b < kBindCount; ++b) f << bind_key(Bind(b)) << "=" << binds[size_t(b)][0] << "," << binds[size_t(b)][1] << "\n";
    return bool(f);
}

} // namespace gk
