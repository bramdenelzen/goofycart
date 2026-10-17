#include "gk/core/crash.h"

#include "gk/core/log.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gk {
namespace {

// Everything the handler needs is prepared at install time; at crash time it
// only formats into static buffers and writes.
char g_dir[1024];
char g_header[512];
char g_log[48 * 170];
bool g_installed = false;

#if defined(_WIN32)
constexpr const char* kOs = "windows";
#elif defined(__APPLE__)
constexpr const char* kOs = "macos";
#else
constexpr const char* kOs = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr const char* kArch = "arm64";
#else
constexpr const char* kArch = "x64";
#endif

// "<dir>crash-<unix time>.txt" without snprintf's locale machinery.
void report_path(char* out, size_t size) {
    char digits[24];
    int n = 0;
    for (auto t = static_cast<unsigned long long>(std::time(nullptr)); n == 0 || t; t /= 10) digits[n++] = char('0' + t % 10);
    size_t used = 0;
    auto put = [&](const char* s) {
        while (*s && used + 1 < size) out[used++] = *s++;
    };
    put(g_dir);
    put("crash-");
    while (n && used + 1 < size) out[used++] = digits[--n];
    put(".txt");
    out[used] = 0;
}

const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (bad memory access)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGILL: return "SIGILL (illegal instruction)";
#ifndef _WIN32
    case SIGBUS: return "SIGBUS (bad memory access)";
#endif
    }
    return "signal";
}

void on_terminate() {
    // An exception nobody caught: say which, then abort into the handler.
    if (auto e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            log_write(LogLevel::Error, std::string("uncaught exception: ") + ex.what());
        } catch (...) {
            log_write(LogLevel::Error, "uncaught exception of unknown type");
        }
    }
    std::abort();
}

#ifdef _WIN32

void write_all(HANDLE f, const char* s) {
    DWORD done = 0;
    WriteFile(f, s, DWORD(std::strlen(s)), &done, nullptr);
}

void write_report(const char* what, const void* address, const CONTEXT* ctx) {
    char path[1200];
    report_path(path, sizeof(path));
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[600];
    write_all(f, g_header);
    std::snprintf(line, sizeof(line), "crash: %s at %p\nstack:\n", what, address);
    write_all(f, line);
    void* frames[64];
    const USHORT n = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
    (void)ctx;
    for (USHORT i = 0; i < n; ++i) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &mod))
            GetModuleFileNameA(mod, name, sizeof(name));
        const char* base = std::strrchr(name, '\\');
        std::snprintf(line, sizeof(line), "  #%02u %s+0x%llx\n", unsigned(i), base ? base + 1 : name,
                      static_cast<unsigned long long>(static_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(mod)));
        write_all(f, line);
    }
    write_all(f, "recent log:\n");
    log_recent(g_log, sizeof(g_log));
    write_all(f, g_log);
    CloseHandle(f);
    std::fprintf(stderr, "Crashed; report written to %s\n", path);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) == 0) {
        char what[64];
        std::snprintf(what, sizeof(what), "exception 0x%08lx", info->ExceptionRecord->ExceptionCode);
        write_report(what, info->ExceptionRecord->ExceptionAddress, info->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void on_signal(int sig) {
    write_report(signal_name(sig), nullptr, nullptr);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void install_platform() {
    SetUnhandledExceptionFilter(on_exception);
    std::signal(SIGABRT, on_signal);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // no dialog box
}

#else

void write_all(int fd, const char* s) {
    size_t left = std::strlen(s);
    while (left > 0) {
        const ssize_t w = write(fd, s, left);
        if (w <= 0) return;
        s += w;
        left -= size_t(w);
    }
}

void on_signal(int sig, siginfo_t* info, void*) {
    static volatile sig_atomic_t entered = 0;
    if (!entered) {
        entered = 1;
        char path[1200];
        report_path(path, sizeof(path));
        const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            write_all(fd, g_header);
            write_all(fd, "crash: ");
            write_all(fd, signal_name(sig));
            char addr[40] = "";
            if (sig == SIGSEGV || sig == SIGBUS) std::snprintf(addr, sizeof(addr), " at %p", info->si_addr);
            write_all(fd, addr);
            write_all(fd, "\nstack:\n");
            void* frames[64];
            const int n = backtrace(frames, 64);
            backtrace_symbols_fd(frames, n, fd);
            write_all(fd, "recent log:\n");
            log_recent(g_log, sizeof(g_log));
            write_all(fd, g_log);
            close(fd);
            write_all(2, "Crashed; report written to ");
            write_all(2, path);
            write_all(2, "\n");
        }
    }
    // Die the way the program would have without us.
    signal(sig, SIG_DFL);
    raise(sig);
}

void install_platform() {
    // Run on our own stack, so a stack overflow can still be reported.
    static char alt_stack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    sigaltstack(&ss, nullptr);
    // backtrace() loads libgcc on first use, which is not safe in a handler.
    void* warm[1];
    backtrace(warm, 1);
    struct sigaction sa{};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) sigaction(sig, &sa, nullptr);
}

#endif

} // namespace

void install_crash_handler(const std::string& dir, const char* program, const char* version) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string d = dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    std::snprintf(g_dir, sizeof(g_dir), "%s", d.c_str());
    const std::time_t now = std::time(nullptr);
    char started[32] = "";
    std::strftime(started, sizeof(started), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    std::snprintf(g_header, sizeof(g_header), "Gobby Kart crash report\nprogram: %s\nversion: %s\nplatform: %s-%s\nstarted: %s\n",
                  program, version, kOs, kArch, started);
    if (g_installed) return;
    g_installed = true;
    std::set_terminate(on_terminate);
    install_platform();
}

std::vector<std::string> pending_crash_reports(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (e.is_regular_file(ec) && name.rfind("crash-", 0) == 0 && e.path().extension() == ".txt")
            out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end()); // same-width timestamps sort by time
    return out;
}

std::string read_crash_report(const std::string& path, size_t max_bytes) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.size() > max_bytes) s.resize(max_bytes);
    return s;
}

void remove_crash_report(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void crash_now(const std::string& how) {
    log_write(LogLevel::Warn, "crashing on purpose (" + how + ")");
    if (how == "abort") std::abort();
    if (how == "throw") throw std::runtime_error("deliberate crash");
    volatile int* p = nullptr;
    *p = 1;
    std::abort();
}

} // namespace gk
