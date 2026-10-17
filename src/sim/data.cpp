#include "gk/sim/data.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace gk::sim {

std::string data_path(const std::string& name) {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    if (const char* env = std::getenv("GK_DATA")) roots.emplace_back(env);
    roots.emplace_back("data");
    roots.emplace_back("../data");
#ifdef GK_DATA_DIR
    roots.emplace_back(GK_DATA_DIR);
#endif
    for (const auto& r : roots) {
        std::error_code ec;
        if (fs::exists(r / name, ec)) return (r / name).string();
    }
    return (fs::path("data") / name).string();
}

} // namespace gk::sim
