#include "settings.h"

#include <algorithm>
#include <fstream>

namespace gk {

bool Settings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
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
      << "\ncrash_reports=" << crash_reports << "\nmaster_url=" << master_url << "\n";
    return bool(f);
}

} // namespace gk
