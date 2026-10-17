#include "gk/sim/kart.h"

#include "gk/core/fixed_step.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

// Kart controller. The structure follows the plain-language reference notes
// in the spec: arcade speed is set by rules along the heading, steering is
// not a physics torque, and physics only adds gravity, landings and
// knock-back. Numbers come from data/physics.json.

namespace gk::sim {
namespace {

constexpr float kProbeHeight = 0.7f;    // ray origins sit this far above the kart's base
constexpr float kSuspension = 0.55f;    // wheels still count as touching this far below the base
constexpr float kLandTolerance = 0.15f;
constexpr float kLaunchDrop = 0.08f;      // metres per frame of ground falling away
constexpr float kSphereRadius = 0.55f;  // wall collision sphere
constexpr float kSphereHeight = 0.6f;
constexpr float kMinFloorDot = 0.5f;    // steeper than 60 degrees is a wall
const vec3 kWheels[4] = {{0.55f, 0, 0.6f}, {-0.55f, 0, 0.6f}, {0.55f, 0, -0.6f}, {-0.55f, 0, -0.6f}};

struct Probe {
    int hits = 0;
    vec3 normal{0, 1, 0};
    float avg_dist = 0.0f;           // ground distance below the base (negative = sunk in)
    Surface surface = Surface::Road; // under the kart's centre (or most wheels)
    bool touched[int(Surface::Count)] = {};
};

Probe probe(const KartState& s, const Track& t, float max_below) {
    Probe pr;
    const vec3 up = s.up();
    vec3 nsum(0.0f);
    float dsum = 0.0f;
    int counts[int(Surface::Count)] = {};
    for (const vec3& w : kWheels) {
        const vec3 origin = s.pos + s.rot * w + up * kProbeHeight;
        auto hit = t.raycast(origin, -up, kProbeHeight + max_below);
        if (!hit || glm::dot(hit->normal, up) < kMinFloorDot) continue;
        ++pr.hits;
        nsum += hit->normal;
        dsum += hit->distance - kProbeHeight;
        pr.touched[int(hit->surface)] = true;
        counts[int(hit->surface)]++;
    }
    if (pr.hits) {
        pr.normal = glm::normalize(nsum);
        pr.avg_dist = dsum / float(pr.hits);
    }
    auto center = t.raycast(s.pos + up * kProbeHeight, -up, kProbeHeight + max_below + 0.5f);
    if (center && glm::dot(center->normal, up) >= kMinFloorDot) {
        pr.surface = center->surface;
        pr.touched[int(center->surface)] = true;
    } else {
        int best = 0;
        for (int i = 1; i < int(Surface::Count); ++i)
            if (counts[i] > counts[best]) best = i;
        pr.surface = Surface(best);
    }
    return pr;
}

quat align_up(const quat& rot, vec3 target, float t) {
    const vec3 up = rot * vec3(0, 1, 0);
    const vec3 new_up = glm::normalize(glm::mix(up, target, t));
    if (glm::dot(up, new_up) > 0.99999f) return rot;
    return glm::normalize(glm::rotation(up, new_up) * rot);
}

quat basis(vec3 forward, vec3 up) {
    forward = glm::normalize(forward - up * glm::dot(forward, up));
    const vec3 left = glm::normalize(glm::cross(up, forward));
    return glm::normalize(glm::quat_cast(glm::mat3(left, glm::cross(forward, left), forward)));
}

void emit(const KartStepContext& ctx, KartEventType type, int value = 0) {
    if (ctx.events) ctx.events->push_back({type, ctx.kart_index, value});
}

float terrain_multiplier(const KartState& s, const KartParams& p, const PhysicsConstants& k) {
    switch (s.surface) {
    case Surface::OffroadLight: return k.offroad_light;
    case Surface::OffroadMedium: return k.offroad_medium;
    case Surface::OffroadHeavy: return k.offroad_heavy;
    case Surface::Sand: return p.sand_grip;
    case Surface::Ice: return p.ice_grip;
    case Surface::Water: return p.top_speed_water / p.top_speed;
    default: return 1.0f;
    }
}

void release_drift(KartState& s, const KartStepContext& ctx, bool give_boost) {
    const int tier = s.mt_tier(ctx.k);
    if (give_boost && tier > 0) {
        const BoostKind kind = tier == 1 ? BoostKind::MiniTurbo : tier == 2 ? BoostKind::SuperMiniTurbo : BoostKind::UltraMiniTurbo;
        const int frames = int(std::round(float(ctx.k.mt_frames[size_t(tier - 1)]) * ctx.p.mt_boost));
        kart_give_boost(s, kind, frames, ctx.k.mt_boost_mult, true);
        emit(ctx, KartEventType::Boost, int(kind));
    }
    s.drift = DriftState::None;
    s.drift_dir = 0;
    s.mt_charge = 0.0f;
}

void start_respawn(KartState& s, const KartStepContext& ctx) {
    s.respawn_frames = ctx.k.respawn_frames;
    s.drift = DriftState::None;
    s.drift_dir = 0;
    s.mt_charge = 0;
    s.boost_frames = 0;
    s.boost = BoostKind::None;
    s.boost_mult = 1.0f;
    s.gliding = false;
    s.glide_armed = false;
    s.trick_done = false;
    s.trick_window = 0;
    emit(ctx, KartEventType::Respawn);
}

void step_respawn(KartState& s, const KartStepContext& ctx) {
    --s.respawn_frames;
    if (s.respawn_frames == ctx.k.respawn_frames / 2) {
        const int idx = ctx.track.respawn_sample_before(s.last_safe_sample);
        const CenterSample& c = ctx.track.center[size_t(idx)];
        s.pos = c.pos + c.up * 0.05f;
        s.rot = basis(c.tangent, c.up);
        s.speed = 0.0f;
        s.slide = vec3(0.0f);
        s.air_vel = vec3(0.0f);
        s.grounded = true;
        s.sample = idx;
    }
    if (s.respawn_frames == 0) s.invincible_frames = 60;
}

void take_off(KartState& s, const KartStepContext& ctx, float extra_up) {
    s.air_vel = s.forward() * s.speed + s.slide + s.up() * extra_up;
    s.grounded = false;
    s.air_frames = 0;
    if (s.glide_armed) {
        s.gliding = true;
        s.air_vel.y = std::max(s.air_vel.y, ctx.k.glide_launch_velocity);
        emit(ctx, KartEventType::GliderOpen);
    }
}

void land(KartState& s, const Probe& pr, const KartInput& in, const KartStepContext& ctx) {
    s.rot = align_up(s.rot, pr.normal, 0.8f);
    s.pos -= s.up() * pr.avg_dist;
    const vec3 f = s.forward(), u = s.up();
    s.speed = glm::dot(s.air_vel, f);
    vec3 lateral = s.air_vel - f * s.speed;
    lateral -= u * glm::dot(lateral, u);
    s.slide = lateral * 0.3f;
    s.grounded = true;
    s.ground_normal = pr.normal;
    s.surface = pr.surface;
    if (s.air_frames > 20) emit(ctx, KartEventType::Land, s.air_frames);
    s.gliding = false;
    s.glide_armed = false;
    s.trick_window = 0;
    if (s.trick_done) {
        s.trick_done = false;
        kart_give_boost(s, BoostKind::Trick, int(float(ctx.k.trick_frames) * ctx.p.mt_boost), ctx.k.trick_mult, true);
        emit(ctx, KartEventType::Boost, int(BoostKind::Trick));
    }
    if (s.drift == DriftState::Hop) {
        const float steer = std::abs(in.steer) > 0.3f ? in.steer : s.hop_steer;
        if (in.drift && std::abs(steer) > 0.3f && s.speed > ctx.k.min_drift_speed) {
            s.drift = DriftState::Drifting;
            s.drift_dir = steer > 0 ? 1 : -1;
            s.mt_charge = 0.0f;
        } else {
            s.drift = DriftState::None;
        }
    }
    s.air_frames = 0;
}

void step_air(KartState& s, const KartInput& in, bool drift_pressed, const KartStepContext& ctx) {
    const auto& k = ctx.k;
    const auto& p = ctx.p;
    ++s.air_frames;
    if (s.trick_window > 0) {
        if (drift_pressed && !s.trick_done && s.drift != DriftState::Hop) {
            s.trick_done = true;
            s.trick_anim = 1.0f;
            emit(ctx, KartEventType::Trick);
        }
        --s.trick_window;
    }

    if (s.gliding) {
        vec3 h(s.air_vel.x, 0.0f, s.air_vel.z);
        float hs = glm::length(h);
        vec3 dir = hs > 0.1f ? h / hs : glm::normalize(vec3(s.forward().x, 0.0f, s.forward().z));
        const float pitch = in.pitch; // +1 nose up, -1 nose down
        const float target_hs = p.glide_speed * (1.0f - k.glide_pitch_speed * pitch);
        hs = approach(hs, target_hs, 6.0f * kDt);
        const float target_vy = -k.glide_sink * (1.0f - 0.6f * pitch);
        const float vy = approach(s.air_vel.y, target_vy, 12.0f * kDt);
        const float yaw = -in.steer * p.turn_rate_air * kDt;
        const quat q = glm::angleAxis(yaw, kUp);
        dir = q * dir;
        s.rot = glm::normalize(q * s.rot);
        s.air_vel = dir * hs + kUp * vy;
    } else {
        s.air_vel.y -= k.gravity * kDt;
        float rate = in.steer * p.turn_rate * 0.35f;
        if (s.drift == DriftState::Drifting) rate = float(s.drift_dir) * p.turn_rate * 0.5f;
        const quat q = glm::angleAxis(-rate * kDt, kUp);
        s.rot = glm::normalize(q * s.rot);
        const vec3 h = q * vec3(s.air_vel.x, 0.0f, s.air_vel.z);
        s.air_vel = vec3(h.x, s.air_vel.y, h.z);
        // Bikes keep charging mini-turbos in the air while drift is held.
        if (s.drift == DriftState::Drifting && p.is_bike && in.drift) s.mt_charge += k.charge_slow * p.mt_charge;
    }
    s.rot = align_up(s.rot, kUp, 0.06f);
    s.pos += s.air_vel * kDt;

    if (s.air_frames > 2 && s.air_vel.y <= 1.0f) {
        const Probe pr = probe(s, ctx.track, kLandTolerance + std::max(0.0f, -s.air_vel.y) * kDt);
        if (pr.hits >= 2) land(s, pr, in, ctx);
    }
}

void step_ground(KartState& s, const KartInput& in, bool drift_pressed, const KartStepContext& ctx) {
    const auto& k = ctx.k;
    const auto& p = ctx.p;
    const Probe pr = probe(s, ctx.track, kSuspension);
    // Leave the ground when it falls away faster than the kart can follow,
    // e.g. at a ramp lip or a sharp crest; gentle hills keep it planted.
    const bool falls_away = pr.hits >= 2 && pr.avg_dist > kLaunchDrop && std::abs(s.speed) > 3.0f;
    if (pr.hits < 2 || falls_away) {
        take_off(s, ctx, 0.0f);
        step_air(s, in, drift_pressed, ctx);
        return;
    }
    s.ground_normal = pr.normal;
    s.rot = align_up(s.rot, pr.normal, 0.5f);
    s.pos -= s.up() * pr.avg_dist;
    s.surface = pr.surface;
    s.last_ground_surface = pr.surface;

    // Special floors
    if (pr.touched[int(Surface::BoostPad)]) {
        if (s.boost != BoostKind::BoostPad) emit(ctx, KartEventType::Boost, int(BoostKind::BoostPad));
        kart_give_boost(s, BoostKind::BoostPad, k.boost_pad_frames, k.boost_pad_mult, true);
    }
    const bool on_glide = pr.touched[int(Surface::GlideRamp)];
    if (on_glide) {
        s.glide_armed = true;
        if (s.boost != BoostKind::GlideRamp) emit(ctx, KartEventType::Boost, int(BoostKind::GlideRamp));
        kart_give_boost(s, BoostKind::GlideRamp, k.boost_pad_frames, k.boost_pad_mult, true);
    } else {
        s.glide_armed = false;
    }
    const bool on_launch = on_glide || pr.touched[int(Surface::TrickRamp)] || pr.touched[int(Surface::BoostPad)];
    s.trick_window = on_launch ? k.trick_window_frames : 0;

    const vec3 fwd = s.forward();
    const vec3 up = s.up();

    // Engine speed
    float terrain = terrain_multiplier(s, p, k);
    if (s.boost_frames > 0 && s.boost_ignores_offroad) terrain = std::max(terrain, 1.0f);
    const float top = p.top_speed * ctx.top_speed_scale * terrain * (1.0f + k.coin_speed_bonus * float(std::min(s.coins, k.max_coins)));
    if (s.boost_frames > 0) {
        const float boosted = top * s.boost_mult;
        if (s.speed < boosted) s.speed = std::min(boosted, s.speed + k.boost_accel * kDt);
    } else if (in.accelerate && !in.brake) {
        if (s.speed < -0.5f) s.speed = std::min(0.0f, s.speed + k.brake_decel * kDt); // rolling backwards: brake first
        else if (s.speed < top) {
            const float x = std::clamp(s.speed / top, 0.0f, 1.0f);
            const float a = p.accel * (1.0f - 0.75f * x * x);
            s.speed = std::min(top, s.speed + a * kDt);
        } else {
            s.speed = approach(s.speed, top, k.overspeed_decel * kDt);
        }
    } else if (in.brake) {
        if (s.speed > 0.5f) s.speed = std::max(0.0f, s.speed - k.brake_decel * kDt);
        else s.speed = std::max(-k.reverse_top, s.speed - k.reverse_accel * kDt);
    } else {
        s.speed = approach(s.speed, 0.0f, k.coast_decel * kDt);
        if (s.speed > top) s.speed = approach(s.speed, top, k.overspeed_decel * kDt);
    }
    s.speed -= fwd.y * k.gravity * k.slope_factor * kDt;

    // Hop: the drift button lifts the kart; landing with the stick held starts a drift.
    if (drift_pressed && s.drift == DriftState::None && s.speed > 1.0f) {
        s.drift = DriftState::Hop;
        s.hop_steer = in.steer;
        if (on_launch) {
            s.trick_done = true;
            s.trick_anim = 1.0f;
            emit(ctx, KartEventType::Trick);
        }
        take_off(s, ctx, k.hop_velocity);
        s.pos += s.air_vel * kDt;
        return;
    }
    if (s.drift == DriftState::Drifting) {
        if (!in.drift) release_drift(s, ctx, true);
        else if (s.speed < k.min_drift_speed * 0.7f) release_drift(s, ctx, false);
        else {
            const float rel = in.steer * float(s.drift_dir);
            s.mt_charge += (rel >= 0.5f ? k.charge_fast : k.charge_slow) * p.mt_charge;
        }
    }

    // Steering turns the heading directly; it is not a physics torque.
    float yaw_rate;
    if (s.drift == DriftState::Drifting) {
        const float rel = in.steer * float(s.drift_dir); // +1 = into the turn
        const bool outward = p.drift == DriftType::Outward;
        const float base = outward ? 0.62f : 0.82f, mod = outward ? 0.38f : 0.28f;
        yaw_rate = float(s.drift_dir) * p.turn_rate * (base + mod * rel);
    } else {
        const float at_speed = 1.0f - 0.25f * std::clamp(std::abs(s.speed) / p.top_speed, 0.0f, 1.0f);
        yaw_rate = in.steer * p.turn_rate * at_speed;
        if (s.speed < 0.0f) yaw_rate = -yaw_rate;
    }
    yaw_rate *= std::clamp(std::abs(s.speed) / 4.0f, 0.0f, 1.0f);

    const vec3 v = fwd * s.speed + s.slide;
    s.rot = glm::normalize(glm::angleAxis(-yaw_rate * kDt, up) * s.rot);
    const vec3 nf = s.forward();
    s.speed = glm::dot(v, nf);
    vec3 lateral = v - nf * s.speed;
    lateral -= up * glm::dot(lateral, up);
    float retention = s.drift == DriftState::Drifting ? k.drift_slip_retention : k.road_slip_retention;
    if (s.surface == Surface::Ice) retention = glm::mix(0.97f, 0.85f, p.slip_traction);
    else if (s.surface == Surface::Sand) retention = glm::mix(0.85f, 0.6f, p.slip_traction);
    s.slide = lateral * retention;

    s.pos += (nf * s.speed + s.slide) * kDt;

    const float target_yaw = s.drift == DriftState::Drifting
                                 ? float(s.drift_dir) * (p.drift == DriftType::Outward ? 0.45f : 0.18f)
                                 : 0.0f;
    s.visual_drift_yaw = approach(s.visual_drift_yaw, target_yaw, 3.0f * kDt);
}

void collide_walls(KartState& s, const KartStepContext& ctx) {
    static thread_local std::vector<WallContact> contacts;
    contacts.clear();
    const vec3 up_ref = s.grounded ? s.up() : kUp;
    ctx.track.collide_sphere(s.pos + s.up() * kSphereHeight, kSphereRadius, contacts);
    vec3 push(0.0f);
    vec3 wall_n(0.0f);
    for (const auto& c : contacts) {
        if (std::abs(glm::dot(c.normal, up_ref)) >= 0.6f) continue; // floors are handled by the wheel probes
        vec3 n = c.normal - up_ref * glm::dot(c.normal, up_ref);
        if (glm::dot(n, n) < 1e-6f) continue;
        n = glm::normalize(n);
        const float along = glm::dot(push, n);
        if (c.depth > along) push += n * (c.depth - along);
        wall_n += n;
    }
    if (glm::dot(wall_n, wall_n) < 1e-8f) return;
    s.pos += push;
    const vec3 n = glm::normalize(wall_n);
    vec3 v = s.velocity();
    const float vn = glm::dot(v, n);
    if (vn >= 0.0f) return;
    const float speed = glm::length(v);
    v -= n * vn * 1.3f; // small bounce
    if (-vn > 0.5f * speed) v *= 0.75f;
    if (-vn > 5.0f) {
        s.wall_bump = 1.0f;
        emit(ctx, KartEventType::WallHit, int(-vn));
    }
    if (s.grounded) {
        // Glancing hits turn the kart to run along the wall.
        const vec3 f = s.forward();
        const float fd = glm::dot(f, n);
        if (fd < 0.0f && fd > -0.75f) {
            const vec3 tangent = glm::normalize(f - n * fd);
            s.rot = glm::normalize(glm::rotation(f, glm::normalize(glm::mix(f, tangent, 0.35f))) * s.rot);
        }
        const vec3 nf = s.forward(), u = s.up();
        s.speed = glm::dot(v, nf);
        vec3 lat = v - nf * s.speed;
        s.slide = lat - u * glm::dot(lat, u);
    } else {
        s.air_vel = v;
    }
}

} // namespace

int KartState::mt_tier(const PhysicsConstants& k) const {
    if (drift != DriftState::Drifting) return 0;
    if (mt_charge >= k.mt_thresholds[2]) return 3;
    if (mt_charge >= k.mt_thresholds[1]) return 2;
    if (mt_charge >= k.mt_thresholds[0]) return 1;
    return 0;
}

void kart_spawn(KartState& s, const Track& track, int grid_slot) {
    s = KartState{};
    const Track::Slot slot = track.grid_slot(grid_slot);
    s.pos = slot.pos;
    s.rot = basis(slot.forward, kUp);
    s.sample = track.nearest_sample(s.pos);
    s.last_safe_sample = s.sample;
    s.checkpoint = 0;
    s.lap = 1;
}

void kart_give_boost(KartState& s, BoostKind kind, int frames, float mult, bool ignores_offroad) {
    if (s.boost_frames > 0) {
        s.boost_mult = std::max(s.boost_mult, mult);
        s.boost_ignores_offroad = s.boost_ignores_offroad || ignores_offroad;
    } else {
        s.boost_mult = mult;
        s.boost_ignores_offroad = ignores_offroad;
    }
    s.boost_frames = std::max(s.boost_frames, frames);
    s.boost = kind;
}

void kart_on_go(KartState& s, const KartStepContext& ctx) {
    const int held = s.accel_hold_frames;
    if (held > ctx.k.rocket_window_early + 20) {
        s.stun_frames = ctx.k.burnout_frames;
        emit(ctx, KartEventType::Burnout);
    } else if (held >= ctx.k.rocket_window_late && held <= ctx.k.rocket_window_early) {
        kart_give_boost(s, BoostKind::RocketStart, ctx.k.rocket_frames, ctx.k.rocket_mult, true);
        emit(ctx, KartEventType::Boost, int(BoostKind::RocketStart));
    }
    s.accel_hold_frames = 0;
}

void kart_step(KartState& s, const KartInput& raw, const KartStepContext& ctx) {
    if (s.invincible_frames > 0) --s.invincible_frames;
    s.trick_anim = std::max(0.0f, s.trick_anim - 2.5f * kDt);
    s.wall_bump = std::max(0.0f, s.wall_bump - 4.0f * kDt);
    if (s.respawn_frames > 0) {
        step_respawn(s, ctx);
        return;
    }
    if (ctx.frozen) {
        s.accel_hold_frames = raw.accelerate ? s.accel_hold_frames + 1 : 0;
        s.drift_button_prev = raw.drift;
        return;
    }
    KartInput in = raw;
    if (s.stun_frames > 0) {
        --s.stun_frames;
        in = KartInput{};
    }
    if (s.finished) in = KartInput{.accelerate = true}; // coast through the finish; bots take over later
    const bool drift_pressed = in.drift && !s.drift_button_prev;
    s.drift_button_prev = in.drift;

    if (s.boost_frames > 0 && --s.boost_frames == 0) {
        s.boost = BoostKind::None;
        s.boost_mult = 1.0f;
        s.boost_ignores_offroad = false;
    }

    if (s.grounded) step_ground(s, in, drift_pressed, ctx);
    else step_air(s, in, drift_pressed, ctx);
    collide_walls(s, ctx);

    if (s.pos.y < ctx.track.kill_y || (s.grounded && s.surface == Surface::OutOfBounds)) {
        start_respawn(s, ctx);
        return;
    }
    if (s.grounded && s.surface != Surface::OutOfBounds) {
        const CenterSample& c = ctx.track.center[size_t(s.sample)];
        if (c.respawn_ok && c.has_floor) s.last_safe_sample = s.sample;
    }
}

bool kart_update_progress(KartState& s, const Track& track) {
    if (track.center.empty() || track.checkpoints.empty()) return false;
    s.sample = track.nearest_sample(s.pos, s.sample);
    const int ncp = int(track.checkpoints.size());
    const int region = track.checkpoint_of(s.sample);
    const int next = (s.checkpoint + 1) % ncp;
    if (region == next) {
        s.checkpoint = next;
        if (next == 0) {
            ++s.lap;
            return true;
        }
    }
    return false;
}

float kart_race_distance(const KartState& s, const Track& track) {
    if (track.center.empty()) return 0.0f;
    float along = track.center[size_t(s.sample)].s;
    if (s.checkpoint == 0 && along > track.length * 0.5f) along -= track.length; // still before the line
    return float(s.lap - 1) * track.length + along;
}

} // namespace gk::sim
