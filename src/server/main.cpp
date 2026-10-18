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
#include "upnp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace {
std::atomic<bool> g_quit{false};
void on_signal(int) { g_quit = true; }

// The game that started this server, so a crashed or killed game doesn't
// leave its server holding the port.
class Parent {
public:
    explicit Parent(long pid) {
#if defined(_WIN32)
        if (pid > 0) handle_ = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
#else
        pid_ = pid;
#endif
    }
    bool gone() const {
#if defined(_WIN32)
        return handle_ && WaitForSingleObject(handle_, 0) == WAIT_OBJECT_0;
#else
        return pid_ > 0 && kill(pid_t(pid_), 0) != 0 && errno == ESRCH; // the server may be detached, so not getppid()
#endif
    }

private:
#if defined(_WIN32)
    HANDLE handle_ = nullptr;
#else
    long pid_ = 0;
#endif
};
} // namespace

int main(int argc, char** argv) {
    bool upnp = false;
    long parent_pid = 0;
    std::string info_file, workdir;
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
        else if (std::strcmp(argv[i], "--upnp") == 0) upnp = true; // ask the router to forward the port
        else if (std::strcmp(argv[i], "--info-file") == 0) info_file = next(); // write the addresses to share here
        else if (std::strcmp(argv[i], "--workdir") == 0) workdir = next();     // where crash reports go
        else if (std::strcmp(argv[i], "--parent-pid") == 0) parent_pid = std::atol(next()); // quit when this process ends
        else if (std::strcmp(argv[i], "--cc") == 0) {
            const int cc = std::atoi(next());
            cfg.engine = cc <= 50 ? gk::sim::EngineClass::cc50 : cc <= 100 ? gk::sim::EngineClass::cc100
                       : cc <= 150 ? gk::sim::EngineClass::cc150 : gk::sim::EngineClass::cc200;
        }
    }
    if (!workdir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(workdir, ec);
        std::filesystem::current_path(workdir, ec);
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
        // Who can join, and how: forward the port on the router when asked,
        // and tell the host which addresses to share.
        gk::Upnp router;
        {
            gk::PortForward fw;
            fw.local_ip = gk::local_ip();
            if (upnp) {
                gk::log_info("Asking the router to forward UDP port {}...", port);
                fw = router.forward_udp(port, "Gobby Kart server");
                if (fw.ok) gk::log_info("The router forwards UDP port {} to this computer", port);
                else gk::log_warn("Automatic port forwarding didn't work: {}", fw.error);
                // Behind a second router (a modem in router mode, or carrier NAT) the
                // forwarded port only reaches as far as that one.
                const std::string internet = gk::internet_ip();
                if (fw.ok && gk::is_private_ip(fw.public_ip)) {
                    gk::log_warn("Your router is itself behind another router ({}): forward UDP port {} to {} on that one too, "
                                 "or put it in bridge mode", fw.public_ip, port, fw.public_ip);
                    fw.ok = false;
                    fw.error = "double NAT: the router's internet side is " + fw.public_ip;
                }
                if (!internet.empty()) fw.public_ip = internet;
            }
            const std::string p = port == gk::net::kDefaultPort ? "" : ":" + std::to_string(port);
            gk::log_info("==============================================================");
            gk::log_info(" Gobby Kart server is running");
            gk::log_info(" On this computer, join with:      127.0.0.1{}", p);
            if (!fw.local_ip.empty()) gk::log_info(" Friends on your network join with: {}{}", fw.local_ip, p);
            if (fw.ok && !fw.public_ip.empty()) gk::log_info(" Friends anywhere join with:        {}{}", fw.public_ip, p);
            else if (upnp && !fw.public_ip.empty())
                gk::log_info(" Friends elsewhere join with {}{} once UDP port {} reaches this computer", fw.public_ip, p, port);
            gk::log_info("==============================================================");
            if (!info_file.empty()) {
                std::ofstream f(info_file, std::ios::trunc);
                f << "port=" << port << "\nlan=" << fw.local_ip << "\npublic=" << fw.public_ip
                  << "\nupnp=" << (fw.ok ? "ok" : upnp ? "failed" : "off") << "\nerror=" << fw.error << "\n";
            }
        }
        gk::log_info("gk_server ready at {} Hz, {} seats, {} laps, {} races per Grand Prix", gk::kTickRate, cfg.seats,
                     cfg.laps, cfg.races_per_gp);

        using clock = std::chrono::steady_clock;
        auto next = clock::now();
        const auto step = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gk::kTickSeconds));
        long ticks = 0;
        // Per 30-second window: the slowest tick and the bandwidth, for sizing.
        double worst_tick_ms = 0.0;
        uint64_t sent_before = 0, received_before = 0;
        const Parent parent(parent_pid);
        while (!g_quit && (max_ticks < 0 || ticks < max_ticks)) {
            const auto tick_start = clock::now();
            net.service(room);
            room.tick();
            net.flush();
            worst_tick_ms = std::max(worst_tick_ms, std::chrono::duration<double, std::milli>(clock::now() - tick_start).count());
            if (master) master->sync(room);
            ++ticks;
            if (ticks % 60 == 0 && parent.gone()) {
                gk::log_info("the game that started this server has closed; stopping");
                break;
            }
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
