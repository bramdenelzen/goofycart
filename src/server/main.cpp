// gk_server: headless dedicated server. Links only gk_sim (and, from the
// netcode phase on, networking). No GPU, window or audio.
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/sim/world.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
std::atomic<bool> g_quit{false};
void on_signal(int) { g_quit = true; }
} // namespace

int main(int argc, char** argv) {
    long max_ticks = -1; // run forever by default
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) max_ticks = std::atol(argv[++i]);
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    gk::log_info("gk_server starting, {} Hz", gk::kTickRate);
    gk::sim::World world;

    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    const auto step = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gk::kTickSeconds));
    while (!g_quit && (max_ticks < 0 || static_cast<long>(world.tick_count()) < max_ticks)) {
        world.tick();
        next += step;
        std::this_thread::sleep_until(next);
    }
    gk::log_info("gk_server stopped after {} ticks", world.tick_count());
    return EXIT_SUCCESS;
}
