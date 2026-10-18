#pragma once
// Player settings, saved as key=value lines in the user data folder.
#include <array>
#include <string>

namespace gk {

// Keyboard controls a player can rebind: two keys each (SDL scancodes, 0 = none).
enum class Bind : int { SteerLeft, SteerRight, Accelerate, Brake, Drift, Item, LookBack, Up, Down, Count };
inline constexpr int kBindCount = int(Bind::Count);
const char* bind_name(Bind b);
const char* bind_key(Bind b); // the settings file key
std::array<std::array<int, 2>, kBindCount> default_binds();

struct Settings {
    int master = 8, music = 6, sfx = 9; // volumes, 0..10
    bool low_res = false;               // the chunky upscaled look (F2)
    bool fullscreen = false;
    bool show_help = true;              // control hints in races (F1)
    bool rumble = true;
    bool crash_reports = true;          // send crash reports to the master server
    std::string master_url;             // ONLINE's master server (also --master)
    std::string join_address;           // the last server joined by address
    std::array<std::array<int, 2>, kBindCount> binds = default_binds();

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

} // namespace gk
