#include "gk/sim/data.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace gk::sim {
namespace {

// The folder the running program is in, so an installed or unzipped game
// finds its data wherever it was put.
std::filesystem::path exe_dir() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::weakly_canonical(fs::path(buf)).parent_path();
#else
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return {};
}

} // namespace

std::string data_path(const std::string& name) {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    if (const char* env = std::getenv("GK_DATA")) roots.emplace_back(env);
    if (const auto dir = exe_dir(); !dir.empty()) {
        roots.push_back(dir / "data");                         // zip / portable folder
        roots.push_back(dir / ".." / "Resources" / "data");      // macOS app bundle
        roots.push_back(dir / ".." / "share" / "gobbykart" / "data"); // Linux install prefix
    }
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
