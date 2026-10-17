#pragma once
// Time Trial ghosts. The simulation is deterministic, so a ghost is just the
// inputs of a run: replaying them in a world of its own reproduces the run
// exactly, with no positions stored.
#include "ai.h"
#include "stats.h"
#include "world.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gk::sim {

// Steering and pitch on a 1/127 grid, as stored in a ghost. Recorded runs
// drive with quantised input so the replay sees exactly the same numbers.
KartInput quantize(const KartInput& in);

struct Ghost {
    std::string track;
    std::string name;
    EngineClass engine = EngineClass::cc150;
    int laps = 3;
    Build build;
    int finish_tick = 0;           // race time in ticks, 0 if it never finished
    std::vector<KartInput> inputs; // one per world tick, countdown included

    std::vector<uint8_t> encode() const;
    static bool decode(std::span<const uint8_t> data, Ghost& out);
};

bool save_ghost(const std::string& path, const Ghost& g);
bool load_ghost(const std::string& path, Ghost& out);

// What a Time Trial world looks like: the track without item boxes, one
// kart, three mushrooms in hand.
World make_time_trial_world(Track track, const PhysicsTables& tables, const RaceConfig& rc, const KartParams& params);

// A ghost kart in a world of its own, ticked in step with the race. It
// replays a recorded ghost, or with none drives as a hard CPU.
class GhostRunner {
public:
    GhostRunner(Track track, const PhysicsTables& tables, const RaceConfig& rc, const KartParams& params,
                const Ghost* replay);
    void tick();
    const KartState& kart() const { return world_->karts()[0]; }
    const World& world() const { return *world_; }
    bool replaying() const { return !replay_.inputs.empty(); }

private:
    std::unique_ptr<World> world_;
    Ghost replay_;
};

// Replays a ghost to the end; true if it still finishes at its recorded
// time (the physics, parts or track have not changed since it was made).
bool ghost_still_valid(const Ghost& g, Track track, const PhysicsTables& tables, const KartParams& params);

} // namespace gk::sim
