#include "gk/net/transport.h"

#include "gk/core/log.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstdint>

namespace gk::net {
namespace {

ENetPacket* make_packet(const std::vector<uint8_t>& data, bool reliable) {
    // Flag 0 is ENet's unreliable *sequenced* mode: late packets are dropped.
    return enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
}

ClientId peer_id(const ENetPeer* p) { return ClientId(reinterpret_cast<uintptr_t>(p->data)); }

} // namespace

bool transport_init() {
    if (enet_initialize() != 0) {
        log_error("enet_initialize failed");
        return false;
    }
    return true;
}

void transport_shutdown() { enet_deinitialize(); }

// ---------------------------------------------------------------- server

NetServer::~NetServer() {
    if (host_) enet_host_destroy(host_);
}

bool NetServer::listen(uint16_t port, int max_clients) {
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    host_ = enet_host_create(&addr, size_t(max_clients), kChannelCount, 0, 0);
    if (!host_) {
        log_error("could not listen on UDP port {}", port);
        return false;
    }
    log_info("listening on UDP port {}", port);
    return true;
}

void NetServer::service(Room& room) {
    ENetEvent ev;
    while (host_ && enet_host_service(host_, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            const ClientId id = next_id_++;
            ev.peer->data = reinterpret_cast<void*>(uintptr_t(id));
            enet_peer_timeout(ev.peer, 0, 3000, 8000);
            peers_[id] = ev.peer;
            char ip[64] = {};
            enet_address_get_host_ip(&ev.peer->address, ip, sizeof(ip));
            log_info("client {} connected from {}:{}", id, ip, ev.peer->address.port);
            room.on_connect(id);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            room.on_message(peer_id(ev.peer), std::span<const uint8_t>(ev.packet->data, ev.packet->dataLength));
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT: {
            const ClientId id = peer_id(ev.peer);
            log_info("client {} disconnected", id);
            peers_.erase(id);
            room.on_disconnect(id);
            break;
        }
        default:
            break;
        }
    }
}

void NetServer::send(ClientId client, uint8_t channel, std::vector<uint8_t> data, bool reliable) {
    auto it = peers_.find(client);
    if (it == peers_.end()) return;
    enet_peer_send(it->second, channel, make_packet(data, reliable));
}

uint64_t NetServer::bytes_sent() const { return host_ ? host_->totalSentData : 0; }
uint64_t NetServer::bytes_received() const { return host_ ? host_->totalReceivedData : 0; }

void NetServer::flush() {
    if (host_) enet_host_flush(host_);
}

// ---------------------------------------------------------------- client

NetClient::~NetClient() {
    disconnect();
    if (host_) enet_host_destroy(host_);
}

bool NetClient::connect(const std::string& host, uint16_t port, int timeout_ms) {
    host_ = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!host_) return false;
    ENetAddress addr{};
    if (enet_address_set_host(&addr, host.c_str()) != 0) {
        log_error("could not resolve {}", host);
        return false;
    }
    addr.port = port;
    peer_ = enet_host_connect(host_, &addr, kChannelCount, 0);
    if (!peer_) return false;
    ENetEvent ev;
    if (enet_host_service(host_, &ev, enet_uint32(timeout_ms)) > 0 && ev.type == ENET_EVENT_TYPE_CONNECT) {
        connected_ = true;
        enet_peer_timeout(peer_, 0, 3000, 8000);
        log_info("connected to {}:{}", host, port);
        return true;
    }
    enet_peer_reset(peer_);
    peer_ = nullptr;
    log_error("could not connect to {}:{}", host, port);
    return false;
}

void NetClient::disconnect() {
    if (peer_ && connected_) {
        enet_peer_disconnect(peer_, 0);
        enet_host_flush(host_);
    }
    connected_ = false;
    peer_ = nullptr;
}

int NetClient::rtt_ms() const { return peer_ ? int(peer_->roundTripTime) + cond_.lag_ms : 0; }

void NetClient::enqueue(std::vector<Delayed>& q, uint8_t channel, std::vector<uint8_t> data, bool reliable) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    int delay = cond_.lag_ms / 2;
    if (cond_.jitter_ms > 0) delay += int(u(rng_) * float(cond_.jitter_ms));
    if (u(rng_) < cond_.loss) {
        if (!reliable) return;
        delay += cond_.lag_ms + 30; // a resend after roughly one round trip
    }
    auto due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
    if (reliable) {
        // Reliable data stays ordered within its channel, like ENet's.
        auto& last = reliable_due_[{&q, channel}];
        due = (std::max)(due, last); // parenthesised: windows.h defines max
        last = due;
    }
    q.push_back({due, channel, std::move(data), reliable});
}

std::vector<NetClient::Delayed> NetClient::take_due(std::vector<Delayed>& q) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<Delayed> due;
    auto split = std::stable_partition(q.begin(), q.end(), [&](const Delayed& d) { return d.due <= now; });
    due.assign(std::make_move_iterator(q.begin()), std::make_move_iterator(split));
    q.erase(q.begin(), split);
    std::stable_sort(due.begin(), due.end(), [](const Delayed& a, const Delayed& b) { return a.due < b.due; });
    return due;
}

void NetClient::send_now(uint8_t channel, const std::vector<uint8_t>& data, bool reliable) {
    if (peer_ && connected_) enet_peer_send(peer_, channel, make_packet(data, reliable));
}

void NetClient::send(uint8_t channel, std::vector<uint8_t> data, bool reliable) {
    if (cond_.lag_ms <= 0 && cond_.loss <= 0.0f && cond_.jitter_ms <= 0) send_now(channel, data, reliable);
    else enqueue(outgoing_, channel, std::move(data), reliable);
}

void NetClient::service(const MessageFn& on_message) {
    if (!host_) return;
    const bool simulated = cond_.lag_ms > 0 || cond_.loss > 0.0f || cond_.jitter_ms > 0;
    for (const Delayed& d : take_due(outgoing_)) send_now(d.channel, d.data, d.reliable);
    ENetEvent ev;
    while (enet_host_service(host_, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_RECEIVE: {
            std::vector<uint8_t> data(ev.packet->data, ev.packet->data + ev.packet->dataLength);
            const bool reliable = (ev.packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0;
            enet_packet_destroy(ev.packet);
            if (simulated) enqueue(incoming_, ev.channelID, std::move(data), reliable);
            else on_message(data);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            log_warn("disconnected from server");
            connected_ = false;
            peer_ = nullptr;
            break;
        default:
            break;
        }
    }
    for (const Delayed& d : take_due(incoming_)) on_message(d.data);
    enet_host_flush(host_);
}

} // namespace gk::net
