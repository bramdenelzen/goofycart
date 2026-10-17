#include "gk/core/log.h"

#include <chrono>
#include <mutex>

namespace gk {
namespace {
std::mutex g_mutex;
LogLevel g_min = LogLevel::Info;
const auto g_start = std::chrono::steady_clock::now();

const char* level_tag(LogLevel l) {
    switch (l) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info ";
    case LogLevel::Warn: return "warn ";
    case LogLevel::Error: return "error";
    }
    return "?";
}
} // namespace

void log_set_min_level(LogLevel level) { g_min = level; }

void log_write(LogLevel level, std::string_view msg) {
    if (level < g_min) return;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    std::lock_guard lock(g_mutex);
    FILE* out = level >= LogLevel::Warn ? stderr : stdout;
    std::fprintf(out, "[%9.3f] %s %.*s\n", t, level_tag(level), static_cast<int>(msg.size()), msg.data());
    std::fflush(out);
}

} // namespace gk
