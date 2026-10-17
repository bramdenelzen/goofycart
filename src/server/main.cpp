// gk_server: headless dedicated server. Links only gk_sim (and, from the
// netcode phase on, networking). No GPU, window or audio.
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/sim/data.h"
#include "gk/sim/world.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> g_quit{false};
void on_signal(int) { g_quit = true; }
} // namespace

int main(int argc, char** argv) {
    long max_ticks = -1; // run forever by default
    bool fast = false;   // step as fast as possible (tests)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) max_ticks = std::atol(argv[++i]);
        else if (std::strcmp(argv[i], "--fast") == 0) fast = true;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    gk::sim::PhysicsTables tables;
    gk::sim::PartsCatalog parts;
    std::string err;
    if (!tables.load(gk::sim::data_path("physics.json"), &err) || !parts.load(gk::sim::data_path("parts.json"), &err)) {
        gk::log_error("Could not load game data: {}", err);
        return EXIT_FAILURE;
    }

    gk::log_info("gk_server starting, {} Hz", gk::kTickRate);
    gk::sim::World world(gk::sim::make_greybox_track(), tables);
    // Until networking lands, the server races 12 line-following karts.
    for (int i = 0; i < 12; ++i) {
        gk::sim::Build b{i % int(parts.drivers.size()), i % int(parts.bodies.size()), i % int(parts.tires.size()), 0};
        world.add_kart(tables.params(parts.levels(b), parts.body(b), gk::sim::EngineClass::cc150));
    }
    gk::log_info("Track '{}': {:.0f} m, {} checkpoints, 12 karts", world.track().name, world.track().length,
                 world.track().checkpoints.size());

    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    const auto step = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gk::kTickSeconds));
    std::vector<gk::sim::KartInput> inputs(world.karts().size());
    while (!g_quit && (max_ticks < 0 || static_cast<long>(world.tick_count()) < max_ticks)) {
        for (size_t i = 0; i < inputs.size(); ++i) inputs[i] = gk::sim::autopilot(world.karts()[i], world.track());
        world.tick(inputs);
        for (const auto& e : world.events()) {
            if (e.type == gk::sim::KartEventType::Lap)
                gk::log_info("kart {} started lap {} at {:.2f} s", e.kart, e.value, world.race_ticks() / 60.0);
            else if (e.type == gk::sim::KartEventType::Finish)
                gk::log_info("kart {} finished in place {} at {:.2f} s", e.kart, e.value, world.race_ticks() / 60.0);
        }
        if (!fast) {
            next += step;
            std::this_thread::sleep_until(next);
        }
    }
    gk::log_info("gk_server stopped after {} ticks", world.tick_count());
    return EXIT_SUCCESS;
}
