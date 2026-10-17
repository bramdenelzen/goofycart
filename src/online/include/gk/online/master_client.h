#pragma once
// Talks to the master server over HTTP or HTTPS (blocking; call it from a
// worker thread, not the frame loop).
#include "gk/online/master.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gk::online {

struct AccountInfo {
    std::string id, name, token;
    int rating = 1000, races = 0, wins = 0;
};

class MasterClient {
public:
    // base_url: "https://host[:port]" or "http://host[:port]".
    explicit MasterClient(std::string base_url, std::string server_key = {});
    ~MasterClient();
    MasterClient(MasterClient&&) noexcept;

    const std::string& error() const { return error_; }
    bool https() const;

    // Players
    std::optional<AccountInfo> create_account(const std::string& name);
    std::optional<AccountInfo> me(const std::string& token);
    std::optional<ServerEntry> match(const std::string& region, int protocol);
    std::vector<ServerEntry> servers(int protocol);

    // Game servers
    bool heartbeat(const ServerEntry& self);
    std::optional<AccountInfo> verify(const std::string& token);
    bool report(const RaceReport& r);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

} // namespace gk::online
