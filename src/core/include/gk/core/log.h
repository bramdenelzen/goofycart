#pragma once
#include <cstdio>
#include <format>
#include <string_view>

namespace gk {

enum class LogLevel { Debug, Info, Warn, Error };

void log_write(LogLevel level, std::string_view msg);
void log_set_min_level(LogLevel level);

template <class... Args>
void log_info(std::format_string<Args...> fmt, Args&&... args) {
    log_write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args) {
    log_write(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) {
    log_write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args) {
    log_write(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace gk
