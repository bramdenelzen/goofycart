#pragma once
// The master server's state: player accounts, the list of game servers that
// report in, matchmaking and online ratings. No networking here; the HTTP
// routes in master_http.h and the tests drive it directly.
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gk::online {

struct Account {
    std::string id;         // stable, e.g. "a1f3..."
    std::string name;       // shown in races, unique ignoring case
    std::string token_hash; // SHA-256 of the secret token; the token itself is never stored
    int rating = 1000;
    int races = 0;
    int wins = 0;
};

struct ServerEntry {
    std::string host;       // as players should connect to it
    uint16_t port = 0;
    std::string name;
    std::string region;     // e.g. "eu", "na", "asia"
    int protocol = 0;
    int players = 0;        // humans connected
    int max_players = 12;
    std::string phase;      // lobby, racing, results
    std::string track;
    double last_seen = 0.0; // seconds, master clock
    std::string key() const { return host + ":" + std::to_string(port); }
};

// One finished race, as a game server reports it: accounts in finishing order.
struct RaceReport {
    std::vector<std::string> accounts;
};

struct NewAccount {
    Account account;
    std::string token; // give to the player once; it is their login
};

class MasterState {
public:
    // Accounts are kept in `save_path` (JSON) when it is set.
    explicit MasterState(std::string save_path = {});

    // Names are 1-16 printable characters; a taken name gets a number added.
    std::optional<NewAccount> create_account(const std::string& name);
    std::optional<Account> account_for_token(const std::string& token) const;
    std::optional<Account> account(const std::string& id) const;

    // Game servers call in every few seconds; silent ones drop off.
    void heartbeat(const ServerEntry& s, double now);
    std::vector<ServerEntry> servers(double now, int protocol) const;
    // The best server for a player: same protocol, a free seat, the player's
    // region first, then one with people waiting in its lobby.
    std::optional<ServerEntry> match(const std::string& region, int protocol, double now) const;

    // Updates ratings (Elo between every pair of players in the race), race
    // and win counts. Returns the new ratings by account id.
    std::map<std::string, int> report(const RaceReport& r);

    // Keeps a crash report sent by a game or server, in crashes/ next to the
    // accounts file. False for anything that is not a report, is too big, or
    // once kMaxCrashes are stored (old ones are for an operator to clear).
    bool store_crash(const std::string& report);
    int crash_count() const;

    bool save() const;
    bool load();

    static constexpr double kServerTimeout = 30.0;
    static constexpr size_t kMaxCrashBytes = 64 * 1024;
    static constexpr int kMaxCrashes = 2000;

private:
    bool save_locked() const;

    mutable std::mutex mutex_;
    std::string path_;
    std::map<std::string, Account> accounts_;      // by id
    std::map<std::string, std::string> by_token_;  // token hash -> id
    std::map<std::string, ServerEntry> servers_;   // by host:port
    int crashes_ = 0;
};

std::string sha256_hex(const std::string& data);
std::string random_hex(int bytes);

} // namespace gk::online
