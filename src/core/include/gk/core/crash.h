#pragma once
// Crash reports. install_crash_handler() makes a crash (bad memory access,
// abort, illegal instruction, divide by zero, uncaught exception) write
// <dir>/crash-<time>.txt with the program, version, platform, the signal or
// exception, a stack trace (module + offset; symbolise with addr2line, atos
// or a .pdb) and the last log lines, before the process dies as it would
// have. The game sends these to the master server on its next launch.
#include <string>
#include <vector>

namespace gk {

void install_crash_handler(const std::string& dir, const char* program, const char* version);

// Reports waiting in `dir`, oldest first.
std::vector<std::string> pending_crash_reports(const std::string& dir);
std::string read_crash_report(const std::string& path, size_t max_bytes = 64 * 1024);
void remove_crash_report(const std::string& path);

// Deliberately crashes (for testing the handler): "segv", "abort" or "throw".
[[noreturn]] void crash_now(const std::string& how);

} // namespace gk
