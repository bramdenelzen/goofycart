#include "gk/sim/world.h"

#include "gk/core/fixed_step.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace gk::sim {

World::World(Track track, PhysicsTables tables, RaceConfig config)
    : track_(std::move(track)), tables_(std::move(tables)), config_(config), countdown_left_(config.countdown_frames) {
    if (track_.point_to_point) config_.laps = track_.sections(); // an open road is raced once, in its own sections
    track_.laps = config_.laps;
    box_respawn_.assign(track_.item_boxes.size(), 0);
    props_.assign(track_.obstacles.size(), PropState{});
    switches_.assign(track_.switches.size(), 0);
    update_gates();
    rng_.state ^= config_.seed * 0xD1B54A32D192ED03ull;
}

int World::add_kart(const KartParams& params) {
    const int index = int(karts_.size());
    KartState s;
    kart_spawn(s, track_, index);
    karts_.push_back(s);
    params_.push_back(params);
    return index;
}

void World::restore(const WorldState& s) {
    tick_ = s.tick;
    track_.mover_time = double(tick_);
    countdown_left_ = s.countdown_left;
    race_ticks_ = s.race_ticks;
    for (size_t i = 0; i < karts_.size() && i < s.karts.size(); ++i) karts_[i] = s.karts[i];
    entities_ = s.entities;
    if (s.box_respawn.size() == box_respawn_.size()) box_respawn_ = s.box_respawn;
    if (s.props.size() == props_.size()) props_ = s.props;
    if (s.switches.size() == switches_.size()) switches_ = s.switches;
    rng_ = s.rng;
    spiked_cooldown_ = s.spiked_cooldown;
    lightning_cooldown_ = s.lightning_cooldown;
    ink_cooldown_ = s.ink_cooldown;
    update_gates();
}

int World::race_lap() const {
    int lap = 1;
    for (const KartState& k : karts_) lap = std::max(lap, k.lap);
    return std::min(lap, std::max(1, config_.laps));
}

// Which gates hold this tick: from the race clock, the leader's lap and the
// switches, all part of the shared world state.
void World::update_gates() {
    const auto& gates = track_.gates;
    track_.gate_state.assign(gates.size(), 1);
    const int lap = race_lap();
    for (size_t g = 0; g < gates.size(); ++g) {
        const Gate& gate = gates[g];
        bool holds = true;
        switch (gate.when) {
        case Gate::When::Always: break;
        case Gate::When::Laps: holds = lap >= gate.lap_lo && lap <= gate.lap_hi; break;
        case Gate::When::Cycle: {
            const int period = std::max(gate.period, 1);
            const int phase = ((race_ticks_ - gate.offset) % period + period) % period;
            holds = phase < gate.open;
            break;
        }
        case Gate::When::Switch:
            holds = gate.sw >= 0 && size_t(gate.sw) < switches_.size() && switches_[size_t(gate.sw)] > 0;
            break;
        }
        track_.gate_state[g] = uint8_t(holds != gate.invert);
    }
    // Roads that vanish wait for the karts on them to get off.
    track_.branch_busy.assign(track_.branches.size(), 0);
    for (const KartState& k : karts_) {
        if (k.respawn_frames > 0 || size_t(k.sample) >= track_.road.size()) continue;
        const int b = track_.road[size_t(k.sample)].branch;
        if (b >= 0) track_.branch_busy[size_t(b)] = 1;
    }
}

// A switch is pressed while any kart rolls over it, and stays pressed for its hold time.
void World::press_switches() {
    for (size_t i = 0; i < track_.switches.size(); ++i) {
        const Track::Switch& sw = track_.switches[i];
        bool pressed = false;
        for (const KartState& k : karts_) {
            if (!k.grounded || k.respawn_frames > 0) continue;
            const vec3 d = k.pos - sw.pos;
            const float up = glm::dot(d, sw.up);
            if (std::abs(up) < 1.5f && glm::length(d - sw.up * up) < sw.radius) pressed = true;
        }
        if (pressed) switches_[i] = uint16_t(std::min(sw.hold, 65535));
        else if (switches_[i] > 0) --switches_[i];
    }
}

void World::tick(std::span<const KartInput> inputs) {
    events_.clear();
    ++tick_;
    const bool frozen = countdown_left_ > 0;
    step_inputs_.assign(karts_.size(), KartInput{});
    for (size_t i = 0; i < karts_.size() && i < inputs.size(); ++i) step_inputs_[i] = inputs[i];
    if (config_.items && !frozen) items_before_step(step_inputs_);
    // Movers step to this tick; karts riding a platform go along with it
    // (and turn with a swing); a kart under a press as it comes down is
    // squashed: a tumble, and the press shoves it out of the way.
    for (size_t m = 0; m < track_.movers.size(); ++m) {
        const Track::Mover& mv = track_.movers[m];
        const vec3 before = track_.mover_pos(m, double(tick_ - 1)), after = track_.mover_pos(m, double(tick_));
        const Track::Frame f0 = track_.mover_frame(m, double(tick_ - 1)), f1 = track_.mover_frame(m, double(tick_));
        if (mv.kind == Track::Mover::Kind::Press) {
            if (frozen || !track_.press_falling(mv, double(tick_))) continue;
            for (size_t ki = 0; ki < karts_.size(); ++ki) {
                KartState& k = karts_[ki];
                if (k.respawn_frames > 0 || k.tumble_frames > 0) continue;
                const vec3 rel = k.pos - after;
                const float x = glm::dot(rel, f1.right), y = glm::dot(rel, f1.up), z = glm::dot(rel, f1.forward);
                if (std::abs(x) > mv.half.x || std::abs(z) > mv.half.z || y > 0.2f || y < -1.6f) continue;
                apply_hit(int(ki), HitKind::Tumble, -1);
                events_.push_back({KartEventType::Squash, int(ki), int(m)});
            }
            continue;
        }
        if (mv.kind == Track::Mover::Kind::Block) continue;
        for (KartState& k : karts_) {
            if (!k.grounded || k.respawn_frames > 0) continue;
            const vec3 rel = k.pos - before;
            const float x = glm::dot(rel, f0.right), y = glm::dot(rel, f0.up), z = glm::dot(rel, f0.forward);
            const float top = track_.mover_top(mv, x, z);
            if (top > 0.05f && std::abs(y - top) < 0.35f) k.pos = after + f1.right * x + f1.up * y + f1.forward * z;
        }
    }
    track_.mover_time = double(tick_);
    // Rubber band: CPU karts close up on the leading player who is still racing.
    float lead = -1e9f;
    bool any_player = false;
    for (size_t i = 0; i < karts_.size(); ++i)
        if (params_[i].rubber_band <= 0.0f && !karts_[i].finished) {
            lead = std::max(lead, kart_race_distance(karts_[i], track_));
            any_player = true;
        }
    for (size_t i = 0; i < karts_.size(); ++i) {
        KartStepContext ctx{track_, tables_.constants, params_[i], int(i), frozen, &events_};
        ctx.oil = over_oil(int(i));
        const float rb = params_[i].rubber_band;
        if (any_player && rb > 0.0f && !karts_[i].finished) {
            const float gap = std::clamp((lead - kart_race_distance(karts_[i], track_)) / 120.0f, -0.5f, 1.0f);
            ctx.top_speed_scale = 1.0f + rb * (gap > 0.0f ? 0.09f : 0.06f) * gap;
        }
        kart_step(karts_[i], step_inputs_[i], ctx);
    }
    if (countdown_left_ > 0 && --countdown_left_ == 0) {
        for (size_t i = 0; i < karts_.size(); ++i) {
            KartStepContext ctx{track_, tables_.constants, params_[i], int(i), false, &events_};
            ctx.oil = over_oil(int(i));
            kart_on_go(karts_[i], ctx);
        }
    }
    if (!frozen) update_slipstream();
    if (!frozen) collide_props();
    if (!frozen) press_switches();
    collide_karts();
    if (config_.items && !frozen) items_after_step();
    if (!frozen) ++race_ticks_;
    update_gates(); // for the next tick (and for drawing this one): switches, the clock and laps have moved on
    for (size_t i = 0; i < karts_.size(); ++i) {
        KartState& k = karts_[i];
        if (k.respawn_frames > 0) continue;
        if (kart_update_progress(k, track_) && !k.finished) {
            if (k.lap > config_.laps) {
                k.finished = true;
                k.finish_tick = race_ticks_;
                events_.push_back({KartEventType::Finish, int(i), place_of(int(i))});
            } else {
                events_.push_back({KartEventType::Lap, int(i), k.lap});
            }
        }
    }
}

namespace {
// Sets a kart's velocity, split into its grounded speed and slide.
void set_velocity(KartState& k, vec3 v) {
    if (k.grounded) {
        const vec3 f = k.forward(), u = k.up();
        k.speed = glm::dot(v, f);
        vec3 lat = v - f * k.speed;
        k.slide = lat - u * glm::dot(lat, u);
    } else {
        k.air_vel = v;
    }
}
} // namespace

vec3 World::loose_tire_pos(int obstacle, int i) const {
    const Track::Obstacle& o = track_.obstacles[size_t(obstacle)];
    const PropState& p = props_[size_t(obstacle)];
    const float ang = float(p.dir) / 256.0f * 2.0f * kPi + (float(i) - 1.0f) * 0.55f;
    const vec3 dir = o.right * std::cos(ang) + o.forward * std::sin(ang);
    const float t = std::min(1.0f, float(p.down) / 32.0f);
    const float out = (1.6f + 1.3f * float(i)) * (1.0f - (1.0f - t) * (1.0f - t)); // rolls out and settles
    return o.pos + o.up * 0.3f + dir * out;
}

void World::update_slipstream() {
    constexpr int kFull = 75;            // 1.25 s in the tow
    constexpr int kBoostFrames = 75;
    constexpr float kMult = 1.12f;
    for (size_t i = 0; i < karts_.size(); ++i) {
        KartState& k = karts_[i];
        bool towed = false;
        if (k.grounded && k.speed > 15.0f && k.boost_frames == 0 && k.respawn_frames == 0 && !k.knocked()) {
            const vec3 f = k.forward();
            for (size_t j = 0; j < karts_.size() && !towed; ++j) {
                if (j == i || karts_[j].respawn_frames > 0) continue;
                const vec3 d = karts_[j].pos - k.pos;
                const float along = glm::dot(d, f);
                if (along < 2.5f || along > 16.0f || std::abs(d.y) > 1.5f) continue;
                const float side = glm::length(d - f * along - k.up() * glm::dot(d, k.up()));
                towed = side < 1.2f + along * 0.05f; // the wake widens a little behind a kart
            }
        }
        if (towed) {
            if (++k.draft >= kFull) {
                k.draft = 0;
                kart_give_boost(k, BoostKind::Slipstream, kBoostFrames, kMult, false);
                events_.push_back({KartEventType::Boost, int(i), int(BoostKind::Slipstream)});
            }
        } else {
            k.draft = uint8_t(std::max(0, int(k.draft) - 3));
        }
    }
}

void World::collide_props() {
    constexpr float kKartRadius = 0.55f;
    using B = Track::Breakable;
    for (size_t pi = 0; pi < props_.size(); ++pi) {
        const Track::Obstacle& o = track_.obstacles[pi];
        if (o.breakable == B::None) continue;
        PropState& p = props_[pi];
        const bool round = o.breakable == B::Tires || o.breakable == B::Barrel || o.sides > 4;
        if (p.down == 0) {
            for (size_t ki = 0; ki < karts_.size(); ++ki) {
                KartState& k = karts_[ki];
                if (k.respawn_frames > 0) continue;
                const vec3 rel = k.pos - o.pos;
                const float y = glm::dot(rel, o.up);
                if (y < -0.5f || y > 2.0f * o.half.y + 0.6f) continue;
                const float x = glm::dot(rel, o.right), z = glm::dot(rel, o.forward);
                const float ax = o.half.x + kKartRadius, az = o.half.z + kKartRadius;
                const bool inside = round ? (x * x) / (ax * ax) + (z * z) / (az * az) < 1.0f : std::abs(x) < ax && std::abs(z) < az;
                if (!inside) continue;
                vec3 n = o.right * x + o.forward * z;
                n = glm::length(n) > 1e-3f ? glm::normalize(n) : -k.forward();
                if (o.breakable == B::Tires) {
                    // The stack topples away from the kart and bounces it back.
                    if (!k.untouchable()) {
                        vec3 v = k.velocity();
                        const float vn = glm::dot(v, n);
                        if (vn < 0.0f) v -= n * vn * 1.55f;
                        set_velocity(k, v * 0.6f);
                        k.wall_bump = 1.0f;
                        k.pos += n * 0.25f;
                    }
                    const vec3 away = -n;
                    float ang = std::atan2(glm::dot(away, o.forward), glm::dot(away, o.right));
                    if (ang < 0.0f) ang += 2.0f * kPi;
                    p.dir = uint8_t(int(ang / (2.0f * kPi) * 256.0f) & 255);
                    p.used = 0;
                    events_.push_back({KartEventType::TireKnock, int(ki), int(pi)});
                } else {
                    // Smashed to bits: it costs a little speed, unless you're a star.
                    if (!k.untouchable()) set_velocity(k, k.velocity() * 0.72f);
                    events_.push_back({KartEventType::Smash, int(ki), int(pi)});
                }
                p.down = 1;
                break;
            }
            continue;
        }
        if (p.down < 65535) ++p.down;
        if (o.breakable == B::Tires) {
            // Driving over a loose tire springs the kart into the air.
            for (int i = 0; i < kLooseTires; ++i) {
                if (p.used >> i & 1) continue;
                const vec3 tp = loose_tire_pos(int(pi), i);
                for (size_t ki = 0; ki < karts_.size(); ++ki) {
                    KartState& k = karts_[ki];
                    if (k.respawn_frames > 0 || !k.grounded || std::abs(k.speed) < 4.0f || p.down < 8) continue;
                    vec3 d = k.pos - tp;
                    if (std::abs(glm::dot(d, o.up)) > 1.2f) continue;
                    d -= o.up * glm::dot(d, o.up);
                    if (glm::length(d) > 0.95f) continue;
                    k.air_vel = k.velocity() + kUp * 7.5f;
                    k.grounded = false;
                    k.air_frames = 0;
                    k.trick_window = tables_.constants.trick_window_frames; // a trick off a tire!
                    p.used = uint8_t(p.used | 1u << i);
                    events_.push_back({KartEventType::TireBounce, int(ki), int(pi)});
                    break;
                }
            }
        }
        // Back again once nobody is in the way.
        const int respawn = o.breakable == B::Tires ? kTiresRespawnTicks : kCrateRespawnTicks;
        if (p.down >= respawn) {
            const float clear = std::max(o.half.x, o.half.z) + 2.0f;
            bool blocked = false;
            for (const KartState& k : karts_)
                if (glm::distance(k.pos, o.pos) < clear) blocked = true;
            if (!blocked) p = PropState{};
        }
    }
}

bool World::adopt_motion(int kart, const KartState& c, std::span<const KartInput> replay) {
    if (kart < 0 || size_t(kart) >= karts_.size()) return false;
    KartState& k = karts_[size_t(kart)];
    // The server decides hits, respawns and the finish; while those play out
    // the client's view lags behind them, so keep the server's.
    if (k.finished || k.respawn_frames > 0 || c.respawn_frames > 0 || k.knocked() != c.knocked() || k.stun_frames > 0) return false;
    if (!std::isfinite(c.pos.x) || !std::isfinite(c.pos.y) || !std::isfinite(c.pos.z)) return false;
    if (glm::distance(k.pos, c.pos) > 45.0f) return false; // far beyond a stutter: not believable
    const float top = params_[size_t(kart)].top_speed * 2.2f;
    if (glm::length(c.velocity()) > top || std::abs(c.speed) > top) return false;
    k.pos = c.pos;
    k.rot = glm::normalize(c.rot);
    k.speed = c.speed;
    k.slide = c.slide;
    k.air_vel = c.air_vel;
    k.grounded = c.grounded;
    k.air_frames = c.air_frames;
    k.ground_normal = c.ground_normal;
    k.surface = c.surface;
    k.last_ground_surface = c.last_ground_surface;
    k.drift = c.drift;
    k.drift_dir = c.drift_dir;
    k.mt_charge = c.mt_charge;
    k.drift_button_prev = c.drift_button_prev;
    k.hop_steer = c.hop_steer;
    k.drift_frames = c.drift_frames;
    k.drift_turn = c.drift_turn;
    k.trick_window = c.trick_window;
    k.trick_done = c.trick_done;
    k.trick_gap = c.trick_gap;
    k.trick_anim = c.trick_anim;
    k.glide_armed = c.glide_armed;
    k.gliding = c.gliding;
    k.glide_bank = c.glide_bank;
    k.glide_pitch = c.glide_pitch;
    k.visual_drift_yaw = c.visual_drift_yaw;
    k.wall_bump = c.wall_bump;
    k.stuck_frames = c.stuck_frames;
    k.susp_y = c.susp_y, k.susp_vy = c.susp_vy;
    k.susp_pitch = c.susp_pitch, k.susp_pitch_v = c.susp_pitch_v;
    k.susp_roll = c.susp_roll, k.susp_roll_v = c.susp_roll_v;
    k.tilt = c.tilt, k.tilt_v = c.tilt_v;
    k.susp_wobble = c.susp_wobble;
    // Boosts the kart earned by how it was driven (drift, slipstream, pads)
    // go with the movement; item boosts are the server's (they cost an item).
    const auto driven = [](BoostKind b) {
        return b == BoostKind::None || b == BoostKind::MiniTurbo || b == BoostKind::SuperMiniTurbo || b == BoostKind::UltraMiniTurbo ||
               b == BoostKind::Trick || b == BoostKind::BoostPad || b == BoostKind::GlideRamp || b == BoostKind::Slipstream;
    };
    if (driven(k.boost) && driven(c.boost)) {
        k.boost = c.boost;
        k.boost_frames = c.boost_frames;
        k.boost_mult = std::clamp(c.boost_mult, 1.0f, 1.6f);
        k.boost_ignores_offroad = c.boost_ignores_offroad;
    }
    k.draft = c.draft;
    for (const KartInput& in : replay) {
        KartStepContext ctx{track_, tables_.constants, params_[size_t(kart)], kart, countdown_left_ > 0, nullptr};
        ctx.oil = over_oil(kart);
        kart_step(k, in, ctx);
    }
    return true;
}

void World::collide_karts() {
    constexpr float kRadius = 0.75f;
    for (size_t a = 0; a < karts_.size(); ++a) {
        for (size_t b = a + 1; b < karts_.size(); ++b) {
            KartState& ka = karts_[a];
            KartState& kb = karts_[b];
            if (ka.respawn_frames > 0 || kb.respawn_frames > 0) continue;
            if (ka.ghost_frames > 0 || kb.ghost_frames > 0) continue; // ghosts pass through
            // In the road's plane, so two karts on a loop's wall push each
            // other along the road and not off it (on flat road that's xz).
            const vec3 up = track_.grip_at(ka.sample) ? track_.grip_up(ka.sample) : track_.grip_up(kb.sample);
            vec3 d = kb.pos - ka.pos;
            const float lift = glm::dot(d, up);
            if (std::abs(lift) > 1.5f) continue;
            d -= up * lift;
            const float dist = glm::length(d);
            if (dist >= 2.0f * kRadius || dist < 1e-4f) continue;
            // A star or a bullet knocks over whoever it touches; a full-size
            // kart flattens a shrunken one.
            if (config_.items) {
                const bool pa = ka.star_frames > 0 || ka.bullet_frames > 0;
                const bool pb = kb.star_frames > 0 || kb.bullet_frames > 0;
                if (pa != pb) {
                    const int victim = pa ? int(b) : int(a);
                    if (!karts_[size_t(victim)].untouchable()) apply_hit(victim, HitKind::Tumble, pa ? int(a) : int(b));
                    continue;
                }
                const bool sa = ka.shrink_frames > 0, sb = kb.shrink_frames > 0;
                if (sa != sb) {
                    const int victim = sa ? int(a) : int(b);
                    if (!karts_[size_t(victim)].untouchable()) apply_hit(victim, HitKind::Spin, sa ? int(b) : int(a));
                }
            }
            vec3 n = d / dist;
            const float ma = params_[a].mass, mb = params_[b].mass;
            const vec3 va0 = ka.velocity(), vb0 = kb.velocity();
            // Rear-ending someone: tilt the contact sideways so the two slide
            // past each other instead of the one behind getting stuck.
            {
                const vec3 fa = glm::normalize(ka.forward() - up * glm::dot(ka.forward(), up));
                const vec3 side = glm::cross(up, fa);
                const float along = glm::dot(n, fa);
                if (std::abs(along) > 0.7f) {
                    float s = glm::dot(n, side);
                    if (std::abs(s) < 0.15f) s = ((tick_ + a + b) & 1) ? 0.15f : -0.15f; // dead centre: pick a side
                    n = glm::normalize(n + side * (s > 0.0f ? 0.6f : -0.6f));
                }
            }
            const float overlap = 2.0f * kRadius - dist;
            ka.pos -= n * overlap * (mb / (ma + mb));
            kb.pos += n * overlap * (ma / (ma + mb));
            vec3 va = va0, vb = vb0;
            const float rel = glm::dot(vb - va, n);
            if (rel >= 0.0f) continue;
            // A soft exchange along the contact normal, weighted by mass.
            const float j = -(1.0f + 0.35f) * rel / (1.0f / ma + 1.0f / mb);
            va -= n * (j / ma);
            vb += n * (j / mb);
            auto apply = [](KartState& k, vec3 v0, vec3 v) {
                if (k.grounded) {
                    // Keep most of the speed along the heading: a bump shoves you
                    // sideways, it doesn't stop you.
                    const vec3 f = k.forward(), u = k.up();
                    const float f0 = glm::dot(v0, f), f1 = glm::dot(v, f);
                    float fwd = f0 + (f1 - f0) * 0.3f;
                    if (f0 > 0.0f) fwd = std::max(fwd, f0 * 0.85f);
                    k.speed = fwd;
                    vec3 lat = v - f * f1;
                    k.slide = lat - u * glm::dot(lat, u);
                } else {
                    k.air_vel = v;
                }
            };
            apply(ka, va0, va);
            apply(kb, vb0, vb);
            if (-rel > 2.0f) events_.push_back({KartEventType::Bump, int(a), int(b)});
        }
    }
}

std::vector<int> World::standings() const {
    std::vector<int> order(karts_.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const KartState& ka = karts_[size_t(a)];
        const KartState& kb = karts_[size_t(b)];
        if (ka.finished != kb.finished) return ka.finished;
        if (ka.finished) return ka.finish_tick < kb.finish_tick;
        return kart_race_distance(ka, track_) > kart_race_distance(kb, track_);
    });
    return order;
}

int World::place_of(int kart) const {
    const auto order = standings();
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == kart) return int(i) + 1;
    return int(order.size());
}

KartInput autopilot(const KartState& kart, const Track& track, float lookahead_base) {
    KartInput in;
    in.accelerate = true;
    if (track.center.empty()) return in;
    const int n = int(track.center.size());
    const float spacing = track.length / float(n);
    const float lookahead = lookahead_base + std::abs(kart.speed) * 0.45f;
    const int ahead = int(std::ceil(lookahead / spacing));
    const vec3 target = track.sample(track.advance(kart.sample, ahead)).pos;
    vec3 to = target - kart.pos;
    const vec3 f = kart.forward(), r = kart.right();
    const float x = glm::dot(to, r), z = glm::dot(to, f);
    const float angle = std::atan2(x, std::max(z, 0.1f));
    in.steer = std::clamp(angle * 2.2f, -1.0f, 1.0f);
    return in;
}

} // namespace gk::sim
