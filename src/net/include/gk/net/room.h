#pragma once
#include "gk/net/protocol.h"
#include "gk/sim/track_def.h"
#include "gk/sim/world.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <utility>
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
    sim::CpuLevel cpu_level = sim::CpuLevel::Normal;
    // Tracks: a Grand Prix runs the tracks of one cup in order, then the next
    // cup. `track` set: every race is on that track. No catalog: the greybox.
    sim::TrackCatalog tracks;
    int first_cup = 0;
    std::string track;
    int seats = kMaxSeats;
    int races_per_gp = 4;
    int lobby_ticks = 60 * 20;         // kart selection before each race
    int all_ready_ticks = 60 * 2;      // once every player is ready
    int results_ticks = 60 * 10;       // results screen after a race
    int finish_timeout_ticks = 60 * 30;// after the first player finishes, the rest get this long
    int after_humans_ticks = 60 * 4;   // once every player has finished
    int max_race_ticks = 60 * 60 * 10;
    bool idle_without_humans = true;   // stay in the lobby when nobody is connected
};

// One race room on the dedicated server. It owns the authoritative World and
// runs the loop lobby -> countdown and race -> results -> lobby, over a
// Grand Prix of several races with MK8 points. There are always 12 karts:
// players take seats from the back of the grid and bots fill the rest; a bot
// takes over the kart of a player who leaves. Clients only send inputs and
// their kart choice.
class Room {
public:
    Room(sim::PhysicsTables tables, sim::PartsCatalog parts, RoomConfig config, MessageSink& sink);

    void on_connect(ClientId id);
    void on_disconnect(ClientId id);
    void on_message(ClientId id, std::span<const uint8_t> data);
    void tick();

    RoomPhase phase() const { return phase_; }
    const sim::World* world() const { return world_.get(); }
    uint32_t race_id() const { return race_id_; }
    int race_number() const { return race_number_; }
    // The track for the current race (or the next one, from the lobby).
    std::string current_track() const;
    std::string current_cup() const;
    int human_count() const;
    int late_inputs() const { return late_inputs_; }
    bool racing() const { return phase_ == RoomPhase::Racing; }
    const ResultsMsg& last_results() const { return results_; }

    // Accounts (with a master server). Players who sent a token wait here
    // until the server has asked the master who they are.
    struct AccountCheck {
        ClientId client;
        std::string token;
    };
    std::vector<AccountCheck> take_account_checks() { return std::exchange(account_checks_, {}); }
    // The master vouched for this player: race under the account's name.
    void set_account(ClientId id, const std::string& account, const std::string& name);
    // Finished races with signed-in players, accounts in finishing order.
    std::vector<std::vector<std::string>> take_reports() { return std::exchange(reports_, {}); }

private:
    struct Client {
        std::string name = "Player";
        sim::Build build{};
        bool hello = false;
        bool ready = false;
        int seat = -1;
        int points = 0;                            // Grand Prix total
        std::map<uint32_t, sim::KartInput> inputs; // by world tick
        sim::KartInput last_input{};
        uint32_t used_tick = 0;
        float lead = 0.0f;                         // smoothed ticks of input headroom
        std::string account;                       // verified master server account, if any
    };
    struct Seat {
        bool human = false;
        ClientId client = 0;
        std::string name;
        sim::Build build{};
        sim::KartParams params{};
        sim::KartInput last_input{};
    };

    void enter_lobby();
    void start_race();
    void finish_race();
    void assign_seat(ClientId id, Client& c);
    void send_lobby();
    int lobby_ticks_left() const;
    void send_lobby_to(ClientId to);
    void broadcast_setup();
    RaceSetup make_setup() const;
    sim::KartParams params_for(const sim::Build& b) const;
    sim::Build clamp_build(sim::Build b) const;
    void send_snapshots();
    void step_race();

    sim::PhysicsTables tables_;
    sim::PartsCatalog parts_;
    RoomConfig config_;
    MessageSink& sink_;
    uint32_t data_hash_ = 0;

    std::map<ClientId, Client> clients_;
    std::vector<Seat> seats_;
    std::array<int, kMaxSeats> bot_points_{};
    std::unique_ptr<sim::World> world_;
    std::vector<sim::KartInput> step_inputs_;
    ResultsMsg results_;

    RoomPhase phase_ = RoomPhase::Lobby;
    int phase_ticks_ = 0;        // ticks left in the lobby or results phase
    int ready_ticks_ = -1;       // short countdown once every player is ready
    bool lobby_timer_running_ = false;
    int race_number_ = 1;
    int cup_ = 0;
    int first_finish_for_ = -1;  // ticks since the first player finished
    int humans_done_for_ = -1;   // ticks since every player finished
    uint32_t race_id_ = 0;
    uint32_t server_tick_ = 0;
    int late_inputs_ = 0;
    std::vector<AccountCheck> account_checks_;
    std::vector<std::vector<std::string>> reports_;
};

} // namespace gk::net
