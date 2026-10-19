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
    float weight01 = 0.5f;          // the weight Level as 0..1: tunes the suspension (heavier settles slower, bounces lower)
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
    // Gliding: height and speed are one budget. The nose commands the path
    // angle; gravity along the path and a drag that grows with speed squared
    // set the airspeed, tuned so a hands-off glider holds its air speed stat
    // and sinks at 1/glide_ratio of it. Provisional, tuned by feel.
    float glide_gravity = 12.0f;     // m/s^2 pulling along the path (the wing carries the rest)
    float glide_ratio = 6.0f;        // metres forward per metre of height at trim
    float glide_dive_range = 0.37f;  // how far below trim a full dive points (sine of the angle)
    float glide_climb_range = 0.37f; // how far above trim a full climb points at trim speed
    float glide_path_rate = 0.8f;    // how fast the path follows the nose (sine units per second)
    float glide_floor = 0.6f;        // the slowest glide, as a fraction of the air speed stat
    float glide_floor_gain = 0.05f;  // per m/s under trim near the floor: the nose drops by itself
    float glide_cap_mult = 1.32f;    // fastest glide, as a multiple of top speed (the boost-pad speed)
    float glide_bank_drag = 0.25f;   // extra drag at full bank
    float glide_pitch_speed = 0.18f; // underwater: the stick's trade of height for speed
    // Underwater (deep water zones): a mode, not a surface. Provisional.
    float water_gravity_scale = 0.5f;   // gravity while airborne underwater
    float water_terminal_fall = 6.0f;   // m/s: falls no faster than this underwater
    float water_pitch_gain = 0.5f;      // the stick's pull on the sink, half the glider's
    float drift_turn_water_scale = 0.9f;// drifts turn a little wider underwater
    int water_hysteresis = 6;           // frames in (or out of) the water before the mode flips
    int trick_window_frames = 18;
    int trick_min_air_frames = 12; // a trick off a bump this short gives no boost
    int respawn_frames = 90;
    float fall_respawn_drop = 35.0f;    // m below its road a falling kart is carried back from (a mountain is a long way down)

    // Suspension: a sprung body over the kart's ground contact, visual apart
    // from a short landing wobble. Pairs are (lightest, heaviest) and the
    // weight Level blends between them. Provisional, tuned by feel.
    float susp_freq_light = 16.0f, susp_freq_heavy = 12.0f;      // rad/s, bounce
    float susp_damping_light = 0.38f, susp_damping_heavy = 0.40f; // damping ratio
    float susp_land_gain_light = 0.35f, susp_land_gain_heavy = 0.2f; // compression speed per m/s of fall
    float susp_angle_freq = 11.0f;     // rad/s, pitch and roll
    float susp_angle_damping = 0.35f;
    float susp_pitch_per_accel = 0.004f; // rad per m/s^2 along the heading (+ nose up)
    float susp_roll_per_lateral = 0.0022f; // rad per m/s^2 sideways
    float susp_slope_gain = 0.6f;      // compression speed per (m/s x rad) of slope change
    float susp_slope_lag = 0.5f;       // the body lags this much of a pitch or roll change in the ground
    float susp_max_compress = 0.18f, susp_max_extend = 0.12f; // metres
    float susp_max_pitch = 0.14f, susp_max_roll = 0.12f;      // radians
    float susp_air_droop = 0.06f;      // metres the body hangs while airborne
    int susp_wobble_frames = 10;       // after bottoming out on a landing
    float susp_wobble_steer = 0.45f;   // fraction of steering lost during the wobble
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
