#pragma once
#include "gk/sim/input.h"
#include "gk/sim/stats.h"

#include <string_view>

namespace gk::sim {

class World;

// CPU driver difficulty, like the game's easy / normal / hard CPUs.
enum class CpuLevel : uint8_t { Easy, Normal, Hard };
inline CpuLevel parse_cpu_level(std::string_view s) {
    return s == "easy" ? CpuLevel::Easy : s == "hard" ? CpuLevel::Hard : CpuLevel::Normal;
}

struct CpuSkill {
    float lookahead = 7.0f;      // metres of pure-pursuit lookahead at standstill
    float line_follow = 1.0f;    // 0 = drives the centre, 1 = the full racing line
    float lane = 0.0f;           // personal lateral offset so CPUs don't stack up (m)
    bool drifts = true;
    int max_tier = 3;            // highest mini-turbo it waits for
    bool tricks = true;
    int reaction = 8;            // out of 8: how often it acts on an item chance
    int throttle_lift = 0;       // ticks out of 32 off the accelerator
    bool avoids = true;          // dodges karts and dropped items ahead
    bool rocket_start = true;    // times the accelerator on the countdown
    float glide_dive = 0.0f;     // how hard it dives for speed while gliding with height to spare (0..1)
    float edge_room = 1.6f;      // metres kept from the road's edge at the line's widest (the line itself keeps 1.6)
    float weave = 0.0f;          // sways this far either side of the line on straights (m)
    bool overtakes = false;      // commits to a side to pass a slower kart ahead
    int mistakes = 0;            // chance in 100 of a slip in each 20 s spell: a missed apex or an early drift release
    bool brushes = false;        // its slips are brushes against the wall instead
};

// The slip a CPU is making right now, if any. A pure function of the tick
// and the kart, so the server and every client agree.
enum class CpuMistake : uint8_t { None, MissedApex, EarlyRelease, WallBrush };
CpuMistake cpu_mistake(const World& world, int kart, const CpuSkill& skill);

CpuSkill cpu_skill(CpuLevel level, int seat);
// Speed-matching strength against the leading player (KartParams::rubber_band).
float cpu_rubber_band(CpuLevel level);

// The CPU driver. A pure function of the world state, so the server and every
// client produce the same input for the same state: clients predict CPU karts
// exactly as the server drives them.
KartInput cpu_input(const World& world, int kart, const CpuSkill& skill);

// The parts the CPU in a grid slot drives: every slot a different driver
// while the roster allows, and a spread of bodies, tyres and gliders.
Build cpu_build(const PartsCatalog& parts, int seat);

} // namespace gk::sim
