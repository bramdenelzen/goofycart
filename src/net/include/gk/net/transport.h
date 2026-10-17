#pragma once
#include "gk/net/room.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <span>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

// ENet (UDP) transport for the server and client. One connection per client
// with three channels (see protocol.h). ENet gives sequencing, reliability
// and fragmentation but no encryption; that is a known gap for later.
namespace gk::net {

bool transport_init();
void transport_shutdown();

class NetServer : public MessageSink {
public:
    ~NetServer() override;
    bool listen(uint16_t port, int max_clients = 16);
    // Drains network events into the room without blocking.
    void service(Room& room);
    void send(ClientId client, uint8_t channel, std::vector<uint8_t> data, bool reliable) override;
    void flush();
    int client_count() const { return int(peers_.size()); }

private:
    _ENetHost* host_ = nullptr;
    std::map<ClientId, _ENetPeer*> peers_;
    ClientId next_id_ = 1;
};

// Artificial network conditions applied on the client, in both directions,
// for testing play over a bad connection on a local server.
struct LinkConditions {
    int lag_ms = 0;      // extra round-trip time, split across both directions
    int jitter_ms = 0;   // random extra one-way delay, 0..jitter
    float loss = 0.0f;   // 0..1 chance each packet is lost (reliable ones arrive one RTT late)
};

class NetClient {
public:
    using MessageFn = std::function<void(std::span<const uint8_t>)>;
    ~NetClient();
    // Blocks until connected or timed out.
    bool connect(const std::string& host, uint16_t port, int timeout_ms = 3000);
    void disconnect();
    void send(uint8_t channel, std::vector<uint8_t> data, bool reliable);
    void service(const MessageFn& on_message);
    bool connected() const { return connected_; }
    int rtt_ms() const;
    void set_conditions(const LinkConditions& c) { cond_ = c; }

private:
    struct Delayed {
        std::chrono::steady_clock::time_point due;
        uint8_t channel;
        std::vector<uint8_t> data;
        bool reliable;
    };
    void enqueue(std::vector<Delayed>& q, uint8_t channel, std::vector<uint8_t> data, bool reliable);
    static std::vector<Delayed> take_due(std::vector<Delayed>& q);
    void send_now(uint8_t channel, const std::vector<uint8_t>& data, bool reliable);

    _ENetHost* host_ = nullptr;
    _ENetPeer* peer_ = nullptr;
    bool connected_ = false;
    LinkConditions cond_;
    std::vector<Delayed> outgoing_, incoming_;
    std::map<std::pair<const void*, uint8_t>, std::chrono::steady_clock::time_point> reliable_due_;
    std::mt19937 rng_{12345};
};

} // namespace gk::net
