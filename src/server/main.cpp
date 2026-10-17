// gk_server: headless dedicated server. One race room over ENet (UDP); the
// server owns the simulation and clients send only inputs. No GPU, window or
// audio.
//
// With --master URL it registers with a master server (accounts and
// matchmaking): --server-key (or GK_SERVER_KEY), --region, --name, and
// --public-host if players should connect to another address than the one
// the server reaches the master from.
#include "gk/core/crash.h"
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/net/room.h"
#include "gk/net/transport.h"
#include "gk/sim/data.h"
#include "master_link.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
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
    std::string master_url, server_key, crash;
    gk::online::ServerEntry self;
    self.name = "Gobby Kart server";
    if (const char* k = std::getenv("GK_SERVER_KEY")) server_key = k;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (std::strcmp(argv[i], "--ticks") == 0) max_ticks = std::atol(next());
        else if (std::strcmp(argv[i], "--fast") == 0) fast = true;
        else if (std::strcmp(argv[i], "--port") == 0) port = uint16_t(std::atoi(next()));
        else if (std::strcmp(argv[i], "--laps") == 0) cfg.laps = std::atoi(next());
        else if (std::strcmp(argv[i], "--races") == 0) cfg.races_per_gp = std::atoi(next());
        else if (std::strcmp(argv[i], "--lobby") == 0) cfg.lobby_ticks = std::atoi(next()) * gk::kTickRate;
        else if (std::strcmp(argv[i], "--cpu") == 0) cfg.cpu_level = gk::sim::parse_cpu_level(next()); // easy, normal, hard
        else if (std::strcmp(argv[i], "--cup") == 0) cfg.first_cup = std::atoi(next()) - 1; // 1-based
        else if (std::strcmp(argv[i], "--track") == 0) cfg.track = next();               // one track, every race
        else if (std::strcmp(argv[i], "--master") == 0) master_url = next();
        else if (std::strcmp(argv[i], "--server-key") == 0) server_key = next();
        else if (std::strcmp(argv[i], "--region") == 0) self.region = next();
        else if (std::strcmp(argv[i], "--name") == 0) self.name = next();
        else if (std::strcmp(argv[i], "--public-host") == 0) self.host = next();
        else if (std::strcmp(argv[i], "--crash") == 0) crash = next(); // test the crash reporter
        else if (std::strcmp(argv[i], "--bots") == 0) cfg.idle_without_humans = false; // race even when empty
        else if (std::strcmp(argv[i], "--cc") == 0) {
            const int cc = std::atoi(next());
            cfg.engine = cc <= 50 ? gk::sim::EngineClass::cc50 : cc <= 100 ? gk::sim::EngineClass::cc100
                       : cc <= 150 ? gk::sim::EngineClass::cc150 : gk::sim::EngineClass::cc200;
        }
    }
    gk::install_crash_handler("crashes", "gk_server", GK_VERSION);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    gk::sim::PhysicsTables tables;
    gk::sim::PartsCatalog parts;
    std::string err;
    if (!tables.load(gk::sim::data_path("physics.json"), &err) || !parts.load(gk::sim::data_path("parts.json"), &err)) {
        gk::log_error("Could not load game data: {}", err);
        return EXIT_FAILURE;
    }
    if (!cfg.tracks.load(gk::sim::data_path("tracks/tracks.json"), &err)) {
        gk::log_error("Could not load tracks: {}", err);
        return EXIT_FAILURE;
    }
    if (!cfg.track.empty() && cfg.tracks.find(cfg.track) < 0) {
        gk::log_error("Unknown track '{}'", cfg.track);
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
        std::unique_ptr<gk::MasterLink> master;
        if (!master_url.empty()) {
            self.port = port;
            self.protocol = gk::net::kProtocolVersion;
            self.max_players = cfg.seats;
            master = std::make_unique<gk::MasterLink>(master_url, server_key, self);
        }
        if (!crash.empty()) gk::crash_now(crash);
        gk::log_info("gk_server ready at {} Hz, {} seats, {} laps, {} races per Grand Prix", gk::kTickRate, cfg.seats,
                     cfg.laps, cfg.races_per_gp);

        using clock = std::chrono::steady_clock;
        auto next = clock::now();
        const auto step = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gk::kTickSeconds));
        long ticks = 0;
        // Per 30-second window: the slowest tick and the bandwidth, for sizing.
        double worst_tick_ms = 0.0;
        uint64_t sent_before = 0, received_before = 0;
        while (!g_quit && (max_ticks < 0 || ticks < max_ticks)) {
            const auto tick_start = clock::now();
            net.service(room);
            room.tick();
            net.flush();
            worst_tick_ms = std::max(worst_tick_ms, std::chrono::duration<double, std::milli>(clock::now() - tick_start).count());
            if (master) master->sync(room);
            ++ticks;
            if (ticks % (60 * 30) == 0) {
                const double kb = 1.0 / (30.0 * 1024.0);
                const uint64_t sent = net.bytes_sent(), received = net.bytes_received();
                if (room.racing())
                    gk::log_info("race {}: {} human(s), tick {}, late inputs {}, slowest tick {:.2f} ms, {:.1f} KB/s out, {:.1f} KB/s in",
                                 room.race_id(), room.human_count(), room.world()->tick_count(), room.late_inputs(),
                                 worst_tick_ms, double(sent - sent_before) * kb, double(received - received_before) * kb);
                sent_before = sent;
                received_before = received;
                worst_tick_ms = 0.0;
            }
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
