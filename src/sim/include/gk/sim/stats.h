#pragma once
#include "gk/sim/surface.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gk::sim {

// The MK8 Deluxe stat system: each part adds points per stat, the sum is a
// Level from 0 to 20, and each Level is looked up in a table of engine
// parameters. The menu bar shows (Level + 3) / 4.
enum class Stat : uint8_t {
    SpeedGround,
    SpeedWater,
    SpeedAir,
    Acceleration,
    Weight,
    HandlingGround,
    HandlingWater,
    HandlingAir,
    Traction,      // off-road traction: only affects slipping on sand and ice
    MiniTurbo,
    Invincibility,
    Count
};
constexpr int kStatCount = static_cast<int>(Stat::Count);
constexpr int kMaxLevel = 20;
const char* stat_key(Stat s);

using StatArray = std::array<int, kStatCount>;

enum class DriftType : uint8_t { Outward, Inward }; // karts/ATVs/standard bikes vs sport bikes
enum class EngineClass : uint8_t { cc50, cc100, cc150, cc200 };

// How a part looks; the simulation ignores it.
struct PartLook {
    std::string shape;          // hat, body shape, tyre profile or glider style
    uint32_t color = 0xffffff;  // 0xRRGGBB
    uint32_t color2 = 0x404040;
    float size = 1.0f;
};

struct Part {
    std::string id;
    std::string name;
    StatArray points{};
    DriftType drift = DriftType::Outward; // bodies only
    bool is_bike = false;                 // bodies only: bikes keep charging mini-turbos in the air
    PartLook look;
};

struct Build {
    int driver = 0, body = 0, tires = 0, glider = 0;
};

class PartsCatalog {
public:
    bool load(const std::string& path, std::string* error = nullptr);
    static PartsCatalog builtin(); // tiny catalogue for tests and fallbacks

    std::vector<Part> drivers, bodies, tires, gliders;

    StatArray levels(const Build& b) const;
    const Part& body(const Build& b) const { return bodies.at(b.body); }
};

inline float menu_value(int level) { return float(level + 3) / 4.0f; }

// Per-kart engine parameters after the Level table lookup and engine class.
struct KartParams {
    float top_speed = 24.0f;        // m/s on road
    float top_speed_water = 22.0f;
    float glide_speed = 24.0f;      // m/s horizontal while gliding
    float accel = 8.0f;             // m/s^2 at standstill
    float mass = 20.0f;             // collision mass (KartBound)
    float turn_rate = 2.0f;         // rad/s at full lock
    float turn_rate_water = 1.8f;
    float turn_rate_air = 1.5f;
    float sand_grip = 0.98f;        // BrakeRt_SAND_L
    float ice_grip = 0.97f;         // BrakeRt_ICE_L
    float slip_traction = 0.5f;     // 0 = slides most on sand/ice, 1 = least
    float mt_charge = 1.0f;         // multiplier on mini-turbo charge rate
    float mt_boost = 1.0f;          // multiplier on mini-turbo and trick boost duration
    int invincibility_frames = 60;
    DriftType drift = DriftType::Outward;
    bool is_bike = false;
    // CPU karts only: how strongly they speed up when behind the leading player
    // and ease off when ahead (0 = never, players are always 0).
    float rubber_band = 0.0f;
};

// A piecewise-linear curve over Level 0..20 with a provenance tag.
struct LevelCurve {
    std::vector<std::pair<float, float>> points; // (level, value)
    float scale = 1.0f;
    std::string source;
    float at(int level) const;
};

// Fixed rules shared by all karts. Values tagged datamined come from MK8
// Deluxe data; the rest are provisional until measured from captures.
struct PhysicsConstants {
    float gravity = 32.0f;
    float hop_velocity = 4.2f;
    float min_drift_speed = 6.0f;
    float brake_decel = 22.0f;
    float coast_decel = 6.0f;
    float reverse_top = 6.0f;
    float reverse_accel = 8.0f;
    float overspeed_decel = 14.0f;   // when above the current top speed (off-road, after a boost)
    float slope_factor = 0.35f;
    float road_slip_retention = 0.2f;
    float drift_slip_retention = 0.6f;
    // Drift entry: for the first drift_entry_frames the kart keeps sliding
    // along its old direction (high slip retention).
    int drift_entry_frames = 18;
    float drift_entry_slip_retention = 0.94f;
    // Drift turn, as a fraction of the kart's turn rate: stick fully away
    // from the turn, centred, fully into it. The drift starts at
    // drift_turn_start and eases towards the stick's value at
    // drift_turn_response per second, so it tightens and widens gradually.
    float drift_turn_wide = 0.18f;
    float drift_turn_neutral = 0.4f;
    float drift_turn_tight = 0.78f;
    float drift_turn_start = 0.1f;
    float drift_turn_response = 0.9f;
    float drift_turn_response_settled = 3.2f; // once the entry is over: the stick has real authority
    float drift_inward_bonus = 1.15f; // inward-drifting parts turn this much tighter
    float coin_speed_bonus = 0.005f; // per coin; unpublished, to measure
    int max_coins = 10;

    // Mini-turbo charge (Super Mario Wiki: Mini-Turbo)
    float charge_fast = 5.0f, charge_slow = 2.0f;
    float spark_threshold = 135.0f;
    std::array<float, 3> mt_thresholds{270.0f, 570.0f, 870.0f}; // blue, orange, purple
    std::array<int, 3> mt_frames{37, 100, 158};                   // 0.621 s, 1.674 s, 2.633 s
    float mt_boost_mult = 1.22f;

    float offroad_light = 0.7f, offroad_medium = 0.5f, offroad_heavy = 0.3f; // BrakeRt_DIRT

    int boost_pad_frames = 70;   float boost_pad_mult = 1.32f;
    int mushroom_frames = 90;    float mushroom_mult = 1.32f;
    int trick_frames = 40;       float trick_mult = 1.18f;
    int rocket_frames = 60;      float rocket_mult = 1.25f;
    int rocket_window_early = 100, rocket_window_late = 60; // frames before GO
    int burnout_frames = 60;
    int stuck_respawn_frames = 300; // wedged this long (gas or brake held, barely moving): carried back
    float boost_accel = 40.0f;

    float glide_launch_velocity = 7.0f;
    float glide_sink = 4.0f;
    float glide_pitch_speed = 0.18f;
    // Underwater (deep water zones): a mode, not a surface. Provisional.
    float water_gravity_scale = 0.5f;   // gravity while airborne underwater
    float water_terminal_fall = 6.0f;   // m/s: falls no faster than this underwater
    float water_pitch_gain = 0.5f;      // the stick's pull on the sink, half the glider's
    float drift_turn_water_scale = 0.9f;// drifts turn a little wider underwater
    int water_hysteresis = 6;           // frames in (or out of) the water before the mode flips
    int trick_window_frames = 18;
    int respawn_frames = 90;
};

class PhysicsTables {
public:
    bool load(const std::string& path, std::string* error = nullptr);
    static PhysicsTables builtin();

    KartParams params(const StatArray& levels, const Part& body, EngineClass cc) const;

    PhysicsConstants constants;
    std::array<LevelCurve, kStatCount> curves;   // primary curve per stat
    LevelCurve sand_grip, ice_grip, mt_boost;    // extra curves
    std::array<float, 4> cc_speed{0.78f, 0.89f, 1.0f, 1.33f};
};

} // namespace gk::sim
