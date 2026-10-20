#pragma once
#include "gk/core/math.h"
#include "gk/sim/input.h"
#include "gk/sim/items.h"
#include "gk/sim/stats.h"
#include "gk/sim/surface.h"
#include "gk/sim/track.h"

#include <cstdint>
#include <vector>

namespace gk::sim {

enum class DriftState : uint8_t { None, Hop, Drifting };
enum class BoostKind : uint8_t { None, MiniTurbo, SuperMiniTurbo, UltraMiniTurbo, Trick, BoostPad, GlideRamp, RocketStart, Mushroom, Star, Bullet, Slipstream };

// Things the client turns into sounds, sparks and rumble. The server sends
// gameplay-relevant ones over the reliable lane.
enum class KartEventType : uint8_t {
    Boost, Trick, Land, WallHit, Respawn, Lap, Finish, GliderOpen, Burnout, Bump,
    ItemBox,    // drove through a box: the roulette starts
    ItemGet,    // value = Item
    ItemUse,    // value = Item
    Hit,        // value = HitKind
    Explosion,  // value unused; kart = owner
    Blocked,    // a held item absorbed a hit
    Smash,      // value = obstacle index: a crate, barrel or crystal broke
    TireKnock,  // value = obstacle index: a tire stack toppled
    TireBounce, // value = obstacle index: a loose tire launched the kart
    GliderClose,// the glider folded away (dived into deep water)
    WaterEnter, // went under in deep water
    WaterExit,  // came up out of it
    VentLift,   // a vent's bubbles threw the kart upwards
    Squash,     // value = mover index: a press came down on the kart
};
struct KartEvent {
    KartEventType type;
    int kart;
    int value = 0; // boost kind, lap number, ...
};

// Complete simulation state of one kart. Plain data so it can be copied for
// prediction and rollback, and serialised for snapshots.
struct KartState {
    vec3 pos{0.0f};
    quat rot{1.0f, 0.0f, 0.0f, 0.0f}; // forward = +Z, up = +Y in kart space

    float speed = 0.0f;  // engine speed along the heading while grounded
    vec3 slide{0.0f};    // sideways velocity while grounded (ice, drifts, bumps)
    vec3 air_vel{0.0f};  // full velocity while airborne
    bool grounded = true;
    int air_frames = 0;
    vec3 ground_normal{0.0f, 1.0f, 0.0f};
    Surface surface = Surface::Road;
    Surface last_ground_surface = Surface::Road;

    DriftState drift = DriftState::None;
    int8_t drift_dir = 0;          // -1 left, +1 right
    float mt_charge = 0.0f;
    bool drift_button_prev = false;
    float hop_steer = 0.0f;
    uint8_t drift_frames = 0;      // frames since the drift started: the entry ramp
    float drift_turn = 0.0f;       // how hard the drift turns now, eased towards the stick (drift_turn_target)

    BoostKind boost = BoostKind::None;
    int boost_frames = 0;
    float boost_mult = 1.0f;
    bool boost_ignores_offroad = false;

    int trick_window = 0;          // frames left to trick after leaving a ramp
    bool trick_done = false;       // tricked this time in the air: a boost on landing
    uint8_t trick_gap = 0;         // frames before the next trick of a chain
    float trick_anim = 0.0f;       // 0..1 visual spin
    bool glide_armed = false;
    bool gliding = false;
    float glide_bank = 0.0f;       // radians, + right wing down; it is what turns the glider
    float glide_pitch = 0.0f;      // radians, + nose up
    bool underwater = false;       // in deep water: the water stats, slow falls, pitch control
    uint8_t water_frames = 0;      // frames the water test has disagreed with `underwater` (hysteresis)
    uint8_t grip_carry = 0;        // frames left of gentle overspeed decay after grip road

    int respawn_frames = 0;        // > 0 while the kart is carried back to the track
    int stuck_frames = 0;
    uint8_t draft = 0;             // frames tucked in behind another kart: a slipstream boost when full          // trying to drive but not moving: wedged, rescued after a while
    int invincible_frames = 0;
    int stun_frames = 0;           // burnout or spin-out: no driving
    int last_safe_sample = 0;

    // Race progress
    int sample = 0;
    int checkpoint = 0;
    int lap = 1;
    bool finished = false;
    int finish_tick = 0;
    int coins = 0;
    int accel_hold_frames = 0;     // for the rocket start

    // Items
    Item item = Item::None;        // in hand
    Item item_next = Item::None;   // reserve slot (MK8 Deluxe holds two)
    uint8_t item_uses = 0;         // left in the stack for the item in hand
    uint8_t lucky_index = 0;       // next item out of a Lucky Eight
    int roulette = 0;              // frames left before the box's item arrives
    bool item_button_prev = false;
    bool trailing = false;         // holding the item behind as a shield
    int spin_frames = 0;           // hit by a banana, fireball or plant
    int tumble_frames = 0;         // hit by a shell, bomb, horn or bullet
    int shrink_frames = 0;         // struck by lightning
    int star_frames = 0;
    int bullet_frames = 0;
    int ghost_frames = 0;
    int ink_frames = 0;
    int golden_frames = 0;
    int fire_frames = 0;
    int chomper_frames = 0;
    int chomper_cooldown = 0;

    // Suspension: the body's offset from the wheels. Compression is + down,
    // pitch + nose up, roll + right side down. Only the wobble affects driving.
    float susp_y = 0.0f, susp_vy = 0.0f;
    float susp_pitch = 0.0f, susp_pitch_v = 0.0f;
    float susp_roll = 0.0f, susp_roll_v = 0.0f;
    uint8_t susp_wobble = 0;       // frames of reduced steering after bottoming out
    // Two wheels: the whole kart tipped onto its outside wheels in a hard
    // corner, radians, + right side down (left wheels up). Visual only.
    float tilt = 0.0f, tilt_v = 0.0f;

    // Wheelie (bikes): a fresh pull back on the stick lifts the front wheel
    // for a little more top speed and weaker steering. Ends early on a drift,
    // the brakes, a bump or a hit, or leaving the ground; then a cooldown.
    uint8_t wheelie_frames = 0;    // left in this wheelie
    uint8_t wheelie_cooldown = 0;  // before the next one
    float wheelie_lift = 0.0f;     // 0..1: how far the front is up; scales the speed bonus
    bool pitch_prev = false;       // the stick was pulled back last tick

    // The last hit, for its animation: where it came from in the kart's
    // frame (0 ahead, +64 right, -64 left, +-127 behind) and how fast the
    // kart was going. `hit_aimed` is false when nothing gave a direction.
    int8_t hit_angle = 0;
    uint8_t hit_speed = 0;
    bool hit_aimed = false;

    // Visual-only helpers (the client may also derive these)
    float visual_drift_yaw = 0.0f;
    float wall_bump = 0.0f;

    vec3 forward() const { return rot * vec3(0, 0, 1); }
    vec3 up() const { return rot * vec3(0, 1, 0); }
    vec3 right() const { return rot * vec3(-1, 0, 0); }
    vec3 velocity() const { return grounded ? forward() * speed + slide : air_vel; }
    int mt_tier(const PhysicsConstants& k) const; // 0 none, 1 blue, 2 orange, 3 purple
    // Immune to items and bumps: star, bullet, ghost, respawn and the grace after a hit.
    bool untouchable() const {
        return star_frames > 0 || bullet_frames > 0 || ghost_frames > 0 || respawn_frames > 0 || invincible_frames > 0;
    }
    bool knocked() const { return spin_frames > 0 || tumble_frames > 0; }
};

struct KartStepContext {
    const Track& track;
    const PhysicsConstants& k;
    const KartParams& p;
    int kart_index;
    bool frozen;                       // countdown: no driving yet
    std::vector<KartEvent>* events;
    float top_speed_scale = 1.0f;      // CPU rubber band
    bool oil = false;                  // over a dropped oil slick this tick: the road is as slippery as a spill
};

void kart_spawn(KartState& s, const Track& track, int grid_slot);
// The drift turn (fraction of the kart's turn rate) the stick asks for:
// `rel` is the stick along the drift, +1 into the turn, -1 away from it.
float drift_turn_target(const PhysicsConstants& k, const KartParams& p, float rel);
// The stick that asks for a drift turn of `factor` (unclamped), for the CPUs.
float drift_rel_for(const PhysicsConstants& k, const KartParams& p, float factor);

void kart_step(KartState& s, const KartInput& in, const KartStepContext& ctx);
void kart_give_boost(KartState& s, BoostKind kind, int frames, float mult, bool ignores_offroad);
// Drops a bike's front wheel and starts the cooldown; nothing if not in a wheelie.
void kart_end_wheelie(KartState& s, const PhysicsConstants& k);
// A shove from another kart or a wall, as the change in velocity: the body
// rocks away from it and a hard one tips the kart onto two wheels.
void kart_shove(KartState& s, vec3 dv, const PhysicsConstants& k);
// Called once when the countdown ends: resolves the rocket start.
void kart_on_go(KartState& s, const KartStepContext& ctx);
// Updates sample, checkpoints and laps; returns true when a lap was completed.
bool kart_update_progress(KartState& s, const Track& track);
// Distance raced, for placings: laps plus distance along the spline.
float kart_race_distance(const KartState& s, const Track& track);

} // namespace gk::sim
