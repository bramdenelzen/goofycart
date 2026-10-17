#pragma once
#include "gk/sim/input.h"
#include "gk/sim/kart.h"
#include "gk/sim/stats.h"
#include "gk/sim/track.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gk::sim {

struct RaceConfig {
    EngineClass engine = EngineClass::cc150;
    int laps = 3;
    int countdown_frames = 180; // "3", "2", "1", then GO
};

// Everything needed to rewind the simulation (client-side reconciliation).
struct WorldState {
    uint64_t tick = 0;
    int countdown_left = 0;
    int race_ticks = 0;
    std::vector<KartState> karts;
};

// The authoritative race simulation: track, karts and race clock, stepped at
// a fixed 60 Hz. The server runs it for real; clients run it to predict.
class World {
public:
    World(Track track, PhysicsTables tables, RaceConfig config = {});

    int add_kart(const KartParams& params);
    void set_kart_params(int kart, const KartParams& params) { params_[size_t(kart)] = params; }
    WorldState save() const { return {tick_, countdown_left_, race_ticks_, karts_}; }
    void restore(const WorldState& s);
    void tick(std::span<const KartInput> inputs);

    const Track& track() const { return track_; }
    const PhysicsTables& tables() const { return tables_; }
    const RaceConfig& config() const { return config_; }
    const std::vector<KartState>& karts() const { return karts_; }
    std::vector<KartState>& karts_mut() { return karts_; }
    const KartParams& params(int kart) const { return params_[size_t(kart)]; }
    const std::vector<KartEvent>& events() const { return events_; }

    uint64_t tick_count() const { return tick_; }
    int countdown_left() const { return countdown_left_; }
    bool racing() const { return countdown_left_ <= 0; }
    int race_ticks() const { return race_ticks_; }
    // Kart indices ordered by place (1st first).
    std::vector<int> standings() const;
    int place_of(int kart) const;

private:
    void collide_karts();

    Track track_;
    PhysicsTables tables_;
    RaceConfig config_;
    std::vector<KartState> karts_;
    std::vector<KartParams> params_;
    std::vector<KartEvent> events_;
    uint64_t tick_ = 0;
    int countdown_left_;
    int race_ticks_ = 0;
};

// Simple line follower: steers towards a point ahead on the centre spline.
// Tests use it to drive laps; the CPU drivers build on it later.
KartInput autopilot(const KartState& kart, const Track& track, float lookahead_base = 7.0f);

} // namespace gk::sim
