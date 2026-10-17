// gk_server: headless dedicated server. One race room over ENet (UDP); the
// server owns the simulation and clients send only inputs. No GPU, window or
// audio.
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/net/room.h"
#include "gk/net/transport.h"
#include "gk/sim/data.h"

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
    bool fast = false;   // step as fast as possible (tests)
    uint16_t port = gk::net::kDefaultPort;
    gk::net::RoomConfig cfg;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (std::strcmp(argv[i], "--ticks") == 0) max_ticks = std::atol(next());
        else if (std::strcmp(argv[i], "--fast") == 0) fast = true;
        else if (std::strcmp(argv[i], "--port") == 0) port = uint16_t(std::atoi(next()));
        else if (std::strcmp(argv[i], "--laps") == 0) cfg.laps = std::atoi(next());
        else if (std::strcmp(argv[i], "--races") == 0) cfg.races_per_gp = std::atoi(next());
        else if (std::strcmp(argv[i], "--lobby") == 0) cfg.lobby_ticks = std::atoi(next()) * gk::kTickRate;
        else if (std::strcmp(argv[i], "--cpu") == 0) cfg.cpu_level = gk::sim::parse_cpu_level(next()); // easy, normal, hard
        else if (std::strcmp(argv[i], "--bots") == 0) cfg.idle_without_humans = false; // race even when empty
        else if (std::strcmp(argv[i], "--cc") == 0) {
            const int cc = std::atoi(next());
            cfg.engine = cc <= 50 ? gk::sim::EngineClass::cc50 : cc <= 100 ? gk::sim::EngineClass::cc100
                       : cc <= 150 ? gk::sim::EngineClass::cc150 : gk::sim::EngineClass::cc200;
        }
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
    if (!gk::net::transport_init()) return EXIT_FAILURE;

    int rc = EXIT_SUCCESS;
    {
        gk::net::NetServer net;
        if (!net.listen(port)) {
            gk::net::transport_shutdown();
            return EXIT_FAILURE;
        }
        gk::net::Room room(tables, parts, cfg, net);
        gk::log_info("gk_server ready at {} Hz, {} seats, {} laps, {} races per Grand Prix", gk::kTickRate, cfg.seats,
                     cfg.laps, cfg.races_per_gp);

        using clock = std::chrono::steady_clock;
        auto next = clock::now();
        const auto step = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gk::kTickSeconds));
        long ticks = 0;
        while (!g_quit && (max_ticks < 0 || ticks < max_ticks)) {
            net.service(room);
            room.tick();
            net.flush();
            ++ticks;
            if (ticks % (60 * 30) == 0 && room.racing())
                gk::log_info("race {}: {} human(s), tick {}, late inputs {}", room.race_id(), room.human_count(),
                             room.world()->tick_count(), room.late_inputs());
            if (!fast) {
                next += step;
                if (clock::now() > next + std::chrono::milliseconds(250)) next = clock::now(); // after a stall
                std::this_thread::sleep_until(next);
            }
        }
        gk::log_info("gk_server stopped after {} ticks", ticks);
    }
    gk::net::transport_shutdown();
    return rc;
}
