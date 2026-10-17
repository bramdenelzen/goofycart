#pragma once
// Player settings, saved as key=value lines in the user data folder.
#include <string>

namespace gk {

struct Settings {
    int master = 8, music = 6, sfx = 9; // volumes, 0..10
    bool low_res = true;                // the chunky upscaled look (F2)
    bool fullscreen = false;
    bool show_help = true;              // control hints in races (F1)
    bool rumble = true;
    std::string master_url;             // ONLINE's master server (also --master)

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

} // namespace gk
