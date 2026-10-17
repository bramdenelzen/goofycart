#pragma once
#include "gk/net/protocol.h"
#include "gk/sim/world.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gk::net {

// The client side of a networked race, independent of the transport.
//
// The client runs its own World ahead of the server (prediction): every 60 Hz
// step it applies the local input immediately and sends it, labelled with the
// world tick it is for, to the server. When a snapshot arrives it rewinds to
// the server's state and re-simulates up to the present with its stored
// inputs (reconciliation). Bots are predicted with the same line follower the
// server uses; other humans repeat their last known input. Corrections are
// hidden by a decaying visual offset rather than a snap.
//
// The client clock is kept about kTargetLead ticks ahead of the server using
// the input headroom the server reports back in each snapshot.
class ClientGame {
public:
    using SendFn = std::function<void(uint8_t channel, std::vector<uint8_t> data, bool reliable)>;
    static constexpr int kTargetLead = 2;

    ClientGame(sim::PhysicsTables tables, SendFn send);

    void join(const std::string& name, const sim::Build& build);
    // Kart choice and ready flag for the lobby (also allowed between races).
    void set_build(const sim::Build& build, bool ready);
    void on_message(std::span<const uint8_t> data);

    // One fixed step with the local player's input. Returns false while there
    // is nothing to simulate (not joined, waiting for the first snapshot).
    bool step(const sim::KartInput& input);

    // Scale for the caller's tick accumulator: above 1 when the client should
    // run ahead faster, below 1 when it is too far ahead.
    double time_scale() const { return time_scale_; }

    const sim::World* world() const { return world_.get(); }
    const RaceSetup& setup() const { return setup_; }
    // True once per new race: compares against and updates the caller's id.
    bool race_id_changed(uint32_t& seen) const {
        if (!world_ || seen == setup_.race_id) return false;
        seen = setup_.race_id;
        return true;
    }
    int seat() const { return seat_; }     // -1 while spectating
    RoomPhase phase() const { return lobby_.phase; }
    const LobbyMsg& lobby() const { return lobby_; }
    bool has_lobby() const { return has_lobby_; }
    // Results of the race that just ended, if they belong to the current race.
    const ResultsMsg* results() const { return world_ && results_.race_id == setup_.race_id ? &results_ : nullptr; }
    bool joined() const { return welcomed_; }
    bool data_mismatch() const { return data_mismatch_; }
    bool has_clock() const { return clock_started_; }
    uint32_t tick() const { return tick_; }
    uint32_t server_tick() const { return last_snapshot_tick_; }
    int input_lead() const { return last_lead_; }

    // Render offset for a kart: what is left of the last correction.
    vec3 visual_offset(int kart) const;
    // Gameplay events for presentation (sounds, rumble, HUD messages):
    // predicted ones for the local kart, the server's for everyone else.
    std::vector<sim::KartEvent> take_events();

    // Diagnostics for tests and the HUD.
    float last_correction() const { return last_correction_; }   // metres, local kart
    float max_correction() const { return max_correction_; }
    int snapshots_applied() const { return snapshots_applied_; }

private:
    struct InputSlot {
        uint32_t tick = 0;
        sim::KartInput input{};
    };
    static constexpr size_t kHistory = 256;

    void apply_setup(const RaceSetup& s);
    void apply_snapshot(const SnapshotMsg& m);
    void simulate_to(uint32_t target, bool forward);
    sim::KartInput input_at(uint32_t tick) const;
    void send_inputs();

    sim::PhysicsTables tables_;
    SendFn send_;
    uint32_t data_hash_ = 0;

    bool welcomed_ = false;
    bool data_mismatch_ = false;
    int seat_ = -1;
    RaceSetup setup_;
    LobbyMsg lobby_;
    bool has_lobby_ = false;
    ResultsMsg results_;
    std::unique_ptr<sim::World> world_;

    bool clock_started_ = false;
    uint32_t tick_ = 0;            // predicted world tick
    uint32_t last_snapshot_tick_ = 0;
    uint32_t ignore_lead_until_ = 0;
    int ticks_since_snapshot_ = 0;
    int last_lead_ = 0;
    double time_scale_ = 1.0;

    std::array<InputSlot, kHistory> history_{};
    std::vector<sim::KartInput> remote_inputs_; // last known input per kart
    std::vector<sim::KartInput> step_inputs_;
    std::vector<vec3> visual_offset_;
    std::vector<sim::KartEvent> events_;

    float last_correction_ = 0.0f;
    float max_correction_ = 0.0f;
    int snapshots_applied_ = 0;
};

} // namespace gk::net
