#pragma once
// A game server's connection to the master server, on a thread of its own so
// slow HTTP never stalls the 60 Hz loop: heartbeats with the room's status,
// account checks for players who join with a token, and race reports.
#include "gk/net/room.h"
#include "gk/online/master_client.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gk {

class MasterLink {
public:
    struct Verified {
        net::ClientId client;
        std::string account, name;
    };

    // `self`: how players reach this server (host may be empty: the master
    // uses the address the heartbeat comes from) and its region and name.
    MasterLink(std::string url, std::string server_key, online::ServerEntry self);
    ~MasterLink();

    // Main thread: hand over work and collect answers.
    void sync(net::Room& room);

private:
    void run();

    online::MasterClient client_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool quit_ = false;
    online::ServerEntry status_;
    std::deque<net::Room::AccountCheck> checks_;
    std::deque<std::vector<std::string>> reports_;
    std::vector<Verified> verified_;
    std::thread thread_;
};

} // namespace gk
