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
enum class BoostKind : uint8_t { None, MiniTurbo, SuperMiniTurbo, UltraMiniTurbo, Trick, BoostPad, GlideRamp, RocketStart, Mushroom, Star, Bullet };

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

    BoostKind boost = BoostKind::None;
    int boost_frames = 0;
    float boost_mult = 1.0f;
    bool boost_ignores_offroad = false;

    int trick_window = 0;          // frames left to trick after leaving a ramp
    bool trick_done = false;
    float trick_anim = 0.0f;       // 0..1 visual spin
    bool glide_armed = false;
    bool gliding = false;

    int respawn_frames = 0;        // > 0 while Lakitu carries the kart back
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
};

void kart_spawn(KartState& s, const Track& track, int grid_slot);
void kart_step(KartState& s, const KartInput& in, const KartStepContext& ctx);
void kart_give_boost(KartState& s, BoostKind kind, int frames, float mult, bool ignores_offroad);
// Called once when the countdown ends: resolves the rocket start.
void kart_on_go(KartState& s, const KartStepContext& ctx);
// Updates sample, checkpoints and laps; returns true when a lap was completed.
bool kart_update_progress(KartState& s, const Track& track);
// Distance raced, for placings: laps plus distance along the spline.
float kart_race_distance(const KartState& s, const Track& track);

} // namespace gk::sim
