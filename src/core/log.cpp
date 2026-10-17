#include "gk/core/log.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

namespace gk {
namespace {
std::mutex g_mutex;
LogLevel g_min = LogLevel::Info;
const auto g_start = std::chrono::steady_clock::now();

// The last lines logged, kept for crash reports.
constexpr int kRecentLines = 48;
constexpr int kRecentWidth = 160;
char g_recent[kRecentLines][kRecentWidth];
std::atomic<int> g_recent_count{0};

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
    const int n = g_recent_count.load(std::memory_order_relaxed);
    std::snprintf(g_recent[n % kRecentLines], kRecentWidth, "[%9.3f] %s %.*s", t, level_tag(level),
                  static_cast<int>(msg.size()), msg.data());
    g_recent_count.store(n + 1, std::memory_order_release);
}

size_t log_recent(char* out, size_t size) {
    // No locks or allocation: this runs inside the crash handler.
    if (size == 0) return 0;
    const int n = g_recent_count.load(std::memory_order_acquire);
    size_t used = 0;
    for (int i = n > kRecentLines ? n - kRecentLines : 0; i < n; ++i) {
        const char* line = g_recent[i % kRecentLines];
        const size_t len = strnlen(line, kRecentWidth);
        if (used + len + 2 > size) break;
        std::memcpy(out + used, line, len);
        used += len;
        out[used++] = '\n';
    }
    out[used] = 0;
    return used;
}

} // namespace gk
