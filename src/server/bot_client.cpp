// gk_botclient: headless clients that join a gk_server over UDP and drive the
// line follower through the real client prediction code. For smoke tests
// (CI) and for loading a server with several players.
//
//   gk_botclient [--connect host[:port]] [--clients N] [--seconds S]
//                [--lag ms] [--loss percent] [--jitter ms]
//                [--master URL [--region R]] [--voices]
//
// With --master each bot makes an account and finds its server through
// matchmaking instead of --connect, as the game client does. With --voices
// each bot sends an engine sound of its own, as a player who recorded one
// does, and must hear every other bot's on the karts they drive.
//
// Exits non-zero if any client failed to join and race.
#include "gk/audio/voice.h"
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/net/client_game.h"
#include "gk/net/transport.h"
#include "gk/online/master_client.h"
#include "gk/sim/data.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace gk;

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = net::kDefaultPort;
    int count = 1;
    double seconds = 10.0;
    bool until_results = false;
    bool voices = false;
    net::LinkConditions link;
    std::string master_url, region;
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
        else if (std::strcmp(argv[i], "--master") == 0) master_url = next();
        else if (std::strcmp(argv[i], "--region") == 0) region = next();
        else if (std::strcmp(argv[i], "--until-results") == 0) until_results = true; // stop after one race
        else if (std::strcmp(argv[i], "--voices") == 0) voices = true;
    }

    sim::PhysicsTables tables;
    std::string err;
    sim::TrackCatalog tracks;
    if (!tables.load(sim::data_path("physics.json"), &err) || !tracks.load(sim::data_path("tracks/tracks.json"), &err)) {
        log_error("Could not load game data: {}", err);
        return EXIT_FAILURE;
    }
    if (!net::transport_init()) return EXIT_FAILURE;

    struct Bot {
        std::unique_ptr<net::NetClient> link;
        std::unique_ptr<net::ClientGame> game;
        double accum = 0.0;
        int steps = 0;
        bool sent_ready = false;
    };
    std::vector<Bot> bots(static_cast<size_t>(count));
    bool ok = true;
    std::vector<std::string> tokens(static_cast<size_t>(count));
    if (!master_url.empty()) {
        // Sign up, then let matchmaking pick the server (it may take a heartbeat to appear).
        online::MasterClient master(master_url);
        for (int i = 0; i < count; ++i) {
            const auto a = master.create_account("Bot " + std::to_string(i + 1));
            if (!a) {
                log_error("master: {}", master.error());
                return EXIT_FAILURE;
            }
            tokens[size_t(i)] = a->token;
        }
        std::optional<online::ServerEntry> found;
        for (int tries = 0; tries < 20 && !found; ++tries) {
            found = master.match(region, net::kProtocolVersion);
            if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (!found) {
            log_error("master: no server to join ({})", master.error());
            return EXIT_FAILURE;
        }
        host = found->host;
        port = found->port;
        log_info("matchmaking: {} at {}:{}", found->name, host, port);
    }
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
        b.game->set_tracks(tracks);
        b.game->join("Bot " + std::to_string(i + 1), {i % 4, i % 4, 0, 0}, tokens[size_t(i)]);
        if (voices) b.game->set_voice(audio::encode_clip(audio::make_bot_clip(uint32_t(100 + i), 48000)));
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
            // Ready up as soon as each lobby opens.
            const bool in_lobby = b.game->has_lobby() && b.game->phase() == net::RoomPhase::Lobby;
            if (in_lobby && !b.sent_ready) {
                b.game->set_build({int(&b - bots.data()) % 4, int(&b - bots.data()) % 4, 0, 0}, true);
                b.sent_ready = true;
            }
            if (!in_lobby) b.sent_ready = false;
            b.accum += dt * b.game->time_scale();
            while (b.accum >= kTickSeconds) {
                b.accum -= kTickSeconds;
                sim::KartInput in;
                const sim::World* w = b.game->world();
                if (w && b.game->seat() >= 0) {
                    in = net::bot_input(*w, b.game->seat(), gk::sim::CpuLevel::Hard);
                    if (!w->racing()) in.accelerate = false;
                }
                if (b.game->step(in)) ++b.steps;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (until_results && std::all_of(bots.begin(), bots.end(), [](const Bot& b) { return b.game->results(); })) {
            log_info("every bot has its race results after {:.1f} s", std::chrono::duration<double>(clock::now() - start).count());
            break;
        }
    }

    for (size_t i = 0; i < bots.size(); ++i) {
        const Bot& b = bots[i];
        if (!b.game) continue;
        const bool raced = b.game->joined() && b.game->world() && b.steps > 60;
        // Every other bot's engine sound, found through the kart it drives.
        int heard = 0;
        const auto& seats = b.game->setup().seats;
        for (size_t s = 0; s < seats.size(); ++s)
            if (seats[s].human && int(s) != b.game->seat() && b.game->voice(seats[s].voice)) ++heard;
        const bool heard_all = !voices || heard == int(bots.size()) - 1;
        log_info("bot {}: seat {}, {} steps, rtt {} ms, input lead {}, max correction {:.3f} m{}{}", i + 1,
                 b.game->seat(), b.steps, b.link->rtt_ms(), b.game->input_lead(), b.game->max_correction(),
                 voices ? ", heard " + std::to_string(heard) + " engine voices" : std::string(), raced ? "" : "  (did not race)");
        ok &= raced && heard_all;
    }
    for (Bot& b : bots) b.link.reset();
    net::transport_shutdown();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
