#pragma once
#include "gk/net/protocol.h"
#include "gk/sim/world.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gk::net {

using ClientId = uint32_t;

// Where the room's outgoing messages go: ENet in the real server, an
// in-memory link with simulated latency and loss in tests.
class MessageSink {
public:
    virtual ~MessageSink() = default;
    virtual void send(ClientId client, uint8_t channel, std::vector<uint8_t> data, bool reliable) = 0;
};

struct RoomConfig {
    sim::EngineClass engine = sim::EngineClass::cc150;
    int laps = 3;
    int countdown = 180;
    int seats = kMaxSeats;
    int results_ticks = 60 * 8;        // results screen before the next race
    int finish_grace_ticks = 60 * 15;  // after the last human finishes, bots get this long
    bool idle_without_humans = true;   // stop racing when nobody is connected
};

// One race room on the dedicated server: owns the authoritative World, the
// 12 seats (humans and line-following bots), input buffers, snapshots and
// race flow. The server is the authority for positions, laps and places;
// clients only send inputs.
class Room {
public:
    Room(sim::PhysicsTables tables, sim::PartsCatalog parts, RoomConfig config, MessageSink& sink);

    void on_connect(ClientId id);
    void on_disconnect(ClientId id);
    void on_message(ClientId id, std::span<const uint8_t> data);
    void tick();

    const sim::World* world() const { return world_.get(); }
    uint32_t race_id() const { return race_id_; }
    int human_count() const;
    int late_inputs() const { return late_inputs_; }
    bool racing() const { return world_ != nullptr; }

private:
    struct Client {
        std::string name = "Player";
        sim::Build build{};
        bool hello = false;
        int seat = -1;
        std::map<uint32_t, sim::KartInput> inputs; // by world tick
        sim::KartInput last_input{};
        uint32_t used_tick = 0;
        float lead = 0.0f;                         // smoothed ticks of input headroom
    };
    struct Seat {
        bool human = false;
        ClientId client = 0;
        std::string name;
        sim::Build build{};
        sim::KartParams params{};
        sim::KartInput last_input{};
    };

    void start_race();
    void end_race();
    void assign_seat(ClientId id, Client& c);
    void broadcast_setup();
    RaceSetup make_setup() const;
    sim::KartParams params_for(const sim::Build& b) const;
    void send_snapshots();

    sim::PhysicsTables tables_;
    sim::PartsCatalog parts_;
    RoomConfig config_;
    MessageSink& sink_;
    uint32_t data_hash_ = 0;

    std::map<ClientId, Client> clients_;
    std::vector<Seat> seats_;
    std::unique_ptr<sim::World> world_;
    std::vector<sim::KartInput> step_inputs_;
    uint32_t race_id_ = 0;
    uint32_t server_tick_ = 0;
    int ended_for_ = -1;        // ticks since the race ended (results screen)
    int humans_done_for_ = -1;  // ticks since every human finished
    int late_inputs_ = 0;
};

} // namespace gk::net
