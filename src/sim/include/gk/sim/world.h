#pragma once
#include "gk/sim/input.h"
#include "gk/sim/kart.h"
#include "gk/sim/stats.h"
#include "gk/sim/track.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gk::sim {

struct RaceConfig {
    EngineClass engine = EngineClass::cc150;
    int laps = 3;
    int countdown_frames = 180; // "3", "2", "1", then GO
    bool items = true;
    uint64_t seed = 1;
};

// A breakable obstacle: standing (down == 0), or broken / knocked over for
// `down` ticks. Tire stacks scatter three loose tires in direction `dir`
// (1/256 turns in the road plane); `used` marks the ones already kicked away.
struct PropState {
    uint16_t down = 0;
    uint8_t dir = 0;
    uint8_t used = 0;
};
inline constexpr int kCrateRespawnTicks = 540; // 9 s
inline constexpr int kTiresRespawnTicks = 660; // 11 s
inline constexpr int kLooseTires = 3;

// Everything needed to rewind the simulation (client-side reconciliation).
struct WorldState {
    uint64_t tick = 0;
    int countdown_left = 0;
    int race_ticks = 0;
    std::vector<KartState> karts;
    std::vector<ItemEntity> entities;
    std::vector<int> box_respawn;
    Rng rng;
    std::vector<PropState> props;
    std::vector<uint16_t> switches; // ticks each track switch stays pressed
    int spiked_cooldown = 0;        // frames until the roulette may hand out a spiked shell again
    int lightning_cooldown = 0;     // the same for lightning
    int ink_cooldown = 0;           // and for ink
};

// The authoritative race simulation: track, karts and race clock, stepped at
// a fixed 60 Hz. The server runs it for real; clients run it to predict.
class World {
public:
    World(Track track, PhysicsTables tables, RaceConfig config = {});

    int add_kart(const KartParams& params);
    void set_kart_params(int kart, const KartParams& params) { params_[size_t(kart)] = params; }
    WorldState save() const { return {tick_, countdown_left_, race_ticks_, karts_, entities_, box_respawn_, rng_, props_, switches_, spiked_cooldown_, lightning_cooldown_, ink_cooldown_};
    }
    void restore(const WorldState& s);
    void tick(std::span<const KartInput> inputs);

    const Track& track() const { return track_; }
    const PhysicsTables& tables() const { return tables_; }
    const RaceConfig& config() const { return config_; }
    const std::vector<KartState>& karts() const { return karts_; }
    // Takes a kart's movement (position, velocity, drift, boost) from `from`,
    // keeping race progress, items and hits. Used by the server for the state
    // a player's own client reports. Returns false when it was not plausible.
    // `replay`: inputs to drive the kart on by itself afterwards, for a state
    // that arrived some ticks late.
    bool adopt_motion(int kart, const KartState& from, std::span<const KartInput> replay = {});
    std::vector<KartState>& karts_mut() { return karts_; }
    const KartParams& params(int kart) const { return params_[size_t(kart)]; }
    const std::vector<KartEvent>& events() const { return events_; }
    const std::vector<ItemEntity>& entities() const { return entities_; }
    std::vector<ItemEntity>& entities_mut() { return entities_; }
    // Frames until each item box is back (0 = there).
    const std::vector<int>& box_respawn() const { return box_respawn_; }
    const ItemTuning& item_tuning() const { return item_tuning_; }
    // Breakable obstacles, one per track obstacle (unused for solid ones).
    const std::vector<PropState>& props() const { return props_; }
    // Where loose tire `i` of a knocked-over stack lies now.
    vec3 loose_tire_pos(int obstacle, int i) const;
    // Items the roulette may not hand kart `kart` right now (item_bit mask):
    // the hidden spiked shell and lightning limits.
    uint32_t blocked_items(int kart) const;
    int spiked_cooldown() const { return spiked_cooldown_; }
    int lightning_cooldown() const { return lightning_cooldown_; }
    int ink_cooldown() const { return ink_cooldown_; }
    // Test and debug hook: put an item in a kart's hand.
    void give_item(int kart, Item item);
    // Testing: a hit or an explosion on demand.
    void spawn_explosion(vec3 pos, int owner);
    // `at` is where the hit came from, for its animation; by default the
    // hitting kart (no direction for a Shrink: lightning comes from above).
    void apply_hit(int kart, HitKind kind, int from, std::optional<vec3> at = std::nullopt);
    void throw_item(int kart, Item item, bool backwards, bool forwards);
    // Whether a kart is on a dropped oil slick (slippery, like the oil surface).
    bool over_oil(int kart) const;
    // Ticks left on each track switch (0 = not pressed).
    const std::vector<uint16_t>& switches() const { return switches_; }
    // The lap the race is on for the track's gates: the leader's.
    int race_lap() const;

    uint64_t tick_count() const { return tick_; }
    int countdown_left() const { return countdown_left_; }
    bool racing() const { return countdown_left_ <= 0; }
    int race_ticks() const { return race_ticks_; }
    // Kart indices ordered by place (1st first).
    std::vector<int> standings() const;
    int place_of(int kart) const;

private:
    void collide_karts();
    void update_gates();
    void press_switches();
    void collide_props();
    void update_slipstream();
    // items.cpp
    void items_before_step(std::vector<KartInput>& inputs);
    void items_after_step();
    void use_item(int kart, const KartInput& in, bool pressed, bool released);
    bool shield_blocks(int kart, vec3 from_pos, bool any_side);
    void update_entities();
    int kart_in_place(int place) const;

    Track track_;
    PhysicsTables tables_;
    RaceConfig config_;
    std::vector<KartState> karts_;
    std::vector<KartParams> params_;
    std::vector<KartEvent> events_;
    std::vector<ItemEntity> entities_;
    std::vector<int> box_respawn_;
    std::vector<PropState> props_;
    std::vector<uint16_t> switches_;
    std::vector<KartInput> step_inputs_;
    Rng rng_;
    ItemTuning item_tuning_;
    uint64_t tick_ = 0;
    int countdown_left_;
    int race_ticks_ = 0;
    int spiked_cooldown_ = 0;
    int lightning_cooldown_ = 0;
    int ink_cooldown_ = 0;
};

// Simple line follower: steers towards a point ahead on the centre spline.
// Tests use it to drive laps; the CPU drivers build on it later.
KartInput autopilot(const KartState& kart, const Track& track, float lookahead_base = 7.0f);

} // namespace gk::sim
