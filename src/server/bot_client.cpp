// gk_botclient: headless clients that join a gk_server over UDP and drive the
// line follower through the real client prediction code. For smoke tests
// (CI) and for loading a server with several players.
//
//   gk_botclient [--connect host[:port]] [--clients N] [--seconds S]
//                [--lag ms] [--loss percent] [--jitter ms]
//
// Exits non-zero if any client failed to join and race.
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/net/client_game.h"
#include "gk/net/transport.h"
#include "gk/sim/data.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace gk;

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = net::kDefaultPort;
    int count = 1;
    double seconds = 10.0;
    net::LinkConditions link;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (std::strcmp(argv[i], "--connect") == 0) {
            host = next();
            if (const auto colon = host.rfind(':'); colon != std::string::npos && host.find(':') == colon) {
                port = uint16_t(std::atoi(host.c_str() + colon + 1));
                host.resize(colon);
            }
        } else if (std::strcmp(argv[i], "--clients") == 0) count = std::max(1, std::atoi(next()));
        else if (std::strcmp(argv[i], "--seconds") == 0) seconds = std::atof(next());
        else if (std::strcmp(argv[i], "--lag") == 0) link.lag_ms = std::atoi(next());
        else if (std::strcmp(argv[i], "--jitter") == 0) link.jitter_ms = std::atoi(next());
        else if (std::strcmp(argv[i], "--loss") == 0) link.loss = float(std::atof(next())) / 100.0f;
    }

    sim::PhysicsTables tables;
    std::string err;
    if (!tables.load(sim::data_path("physics.json"), &err)) {
        log_error("Could not load game data: {}", err);
        return EXIT_FAILURE;
    }
    if (!net::transport_init()) return EXIT_FAILURE;

    struct Bot {
        std::unique_ptr<net::NetClient> link;
        std::unique_ptr<net::ClientGame> game;
        double accum = 0.0;
        int steps = 0;
    };
    std::vector<Bot> bots(static_cast<size_t>(count));
    bool ok = true;
    for (int i = 0; i < count && ok; ++i) {
        Bot& b = bots[size_t(i)];
        b.link = std::make_unique<net::NetClient>();
        b.link->set_conditions(link);
        if (!b.link->connect(host, port)) {
            ok = false;
            break;
        }
        net::NetClient* l = b.link.get();
        b.game = std::make_unique<net::ClientGame>(tables, [l](uint8_t ch, std::vector<uint8_t> d, bool rel) {
            l->send(ch, std::move(d), rel);
        });
        b.game->join("Bot " + std::to_string(i + 1), {i % 4, i % 4, 0, 0});
    }

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto last = start;
    while (ok && clock::now() - start < std::chrono::duration<double>(seconds)) {
        const auto now = clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        for (Bot& b : bots) {
            b.link->service([&](std::span<const uint8_t> d) { b.game->on_message(d); });
            b.accum += dt * b.game->time_scale();
            while (b.accum >= kTickSeconds) {
                b.accum -= kTickSeconds;
                sim::KartInput in;
                const sim::World* w = b.game->world();
                if (w && b.game->seat() >= 0) {
                    in = net::bot_input(w->karts()[size_t(b.game->seat())], w->track(), 7);
                    if (!w->racing()) in.accelerate = false;
                }
                if (b.game->step(in)) ++b.steps;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    for (size_t i = 0; i < bots.size(); ++i) {
        const Bot& b = bots[i];
        if (!b.game) continue;
        const bool raced = b.game->joined() && b.game->world() && b.steps > 60;
        log_info("bot {}: seat {}, {} steps, rtt {} ms, input lead {}, max correction {:.3f} m{}", i + 1,
                 b.game->seat(), b.steps, b.link->rtt_ms(), b.game->input_lead(), b.game->max_correction(),
                 raced ? "" : "  (did not race)");
        ok &= raced;
    }
    for (Bot& b : bots) b.link.reset();
    net::transport_shutdown();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
