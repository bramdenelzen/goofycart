// gk_master: accounts, the game server list, matchmaking and ratings over
// HTTP(S). One per deployment; game servers report to it and players find
// races through it.
//
//   gk_master [--port 7780] [--bind 0.0.0.0] [--data accounts.json]
//             [--server-key KEY] [--cert cert.pem --key key.pem] [--seconds S]
//
// Without --cert it speaks plain HTTP, for running behind a TLS-terminating
// proxy (see docs/ONLINE.md) or on a LAN. The server key (or GK_SERVER_KEY)
// is the shared secret game servers send; without one any caller may
// register servers and report results.
#include "gk/core/log.h"
#include "gk/online/master_http.h"

#include <httplib.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using namespace gk;

namespace {
httplib::Server* g_server = nullptr;
void on_signal(int) {
    if (g_server) g_server->stop();
}
} // namespace

int main(int argc, char** argv) {
    int port = 7780;
    std::string bind = "0.0.0.0", data = "accounts.json", cert, key_file, server_key;
    double seconds = 0.0;
    if (const char* k = std::getenv("GK_SERVER_KEY")) server_key = k;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (std::strcmp(argv[i], "--port") == 0) port = std::atoi(next().c_str());
        else if (std::strcmp(argv[i], "--bind") == 0) bind = next();
        else if (std::strcmp(argv[i], "--data") == 0) data = next();
        else if (std::strcmp(argv[i], "--server-key") == 0) server_key = next();
        else if (std::strcmp(argv[i], "--cert") == 0) cert = next();
        else if (std::strcmp(argv[i], "--key") == 0) key_file = next();
        else if (std::strcmp(argv[i], "--seconds") == 0) seconds = std::atof(next().c_str());
    }

    online::MasterState state(data);
    if (state.load()) log_info("Loaded accounts from {}", data);

    std::unique_ptr<httplib::Server> http;
    if (!cert.empty()) {
#ifdef GK_HAVE_HTTPS
        http = std::make_unique<httplib::SSLServer>(cert.c_str(), key_file.c_str());
        if (!http->is_valid()) {
            log_error("Could not load the TLS certificate {} / key {}", cert, key_file);
            return EXIT_FAILURE;
        }
#else
        log_error("This build has no HTTPS (OpenSSL was not found); run behind a TLS proxy instead");
        return EXIT_FAILURE;
#endif
    } else {
        http = std::make_unique<httplib::Server>();
    }
    const auto start = std::chrono::steady_clock::now();
    auto clock = [start] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    online::install_master_routes(*http, state, server_key, clock);
    if (server_key.empty()) log_warn("No --server-key: anyone can register servers and report results");

    g_server = http.get();
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::thread stopper;
    if (seconds > 0.0)
        stopper = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            http->stop();
        });
    log_info("gk_master listening on {}://{}:{}", cert.empty() ? "http" : "https", bind, port);
    const bool ok = http->listen(bind, port);
    if (stopper.joinable()) stopper.join();
    state.save();
    if (!ok && seconds <= 0.0) {
        log_error("Could not listen on {}:{}", bind, port);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
