// CPU drivers: follow the racing line with pure pursuit, drift through
// corners for mini-turbos, trick off ramps, pick up item boxes, dodge
// what's ahead and use items roughly the way a decent player would.
//
// Everything here reads only the world state (no memory between ticks, no
// randomness outside the world), so a client predicting a CPU kart computes
// the same input the server does.
#include "gk/sim/ai.h"

#include "gk/sim/world.h"

#include <algorithm>
#include <cmath>

namespace gk::sim {
namespace {

float sign(float x) { return x < 0.0f ? -1.0f : 1.0f; }

// Total heading change of the racing line over the next `metres` (radians, + = right).
float turn_ahead(const Track& t, int from, float metres, float spacing) {
    const int steps = std::max(1, int(metres / spacing));
    float sum = 0.0f;
    for (int i = 0; i < steps; ++i) sum += t.line[size_t(t.wrap(from + i))].curvature * spacing;
    return sum;
}

// A point in the kart's frame: x to the right, z ahead, y up.
struct Local {
    float x, y, z;
};
Local to_local(const KartState& k, vec3 p) {
    const vec3 d = p - k.pos;
    return {glm::dot(d, k.right()), glm::dot(d, k.up()), glm::dot(d, k.forward())};
}

bool hostile_projectile(EntityType t) {
    return t == EntityType::GreenShell || t == EntityType::RedShell || t == EntityType::SpikedShell ||
           t == EntityType::Fireball || t == EntityType::Boomerang;
}

enum class Act { Nothing, Hold, Fire, FireBack };

} // namespace

CpuSkill cpu_skill(CpuLevel level, int seat) {
    CpuSkill s;
    const float lane = float(seat % 3 - 1);
    const float jitter = 0.25f * float(seat % 4);
    switch (level) {
    case CpuLevel::Easy:
        s.lookahead = 9.0f + jitter;
        s.line_follow = 0.4f;
        s.lane = lane * 1.2f;
        s.drifts = false;
        s.max_tier = 1;
        s.tricks = false;
        s.reaction = 3;
        s.throttle_lift = 9;
        s.avoids = false;
        s.rocket_start = false;
        break;
    case CpuLevel::Normal:
        s.lookahead = 7.5f + jitter;
        s.line_follow = 0.8f;
        s.lane = lane * 0.8f;
        s.max_tier = 2;
        s.tricks = seat % 2 == 0;
        s.reaction = 6;
        s.rocket_start = seat % 3 != 2;
        break;
    case CpuLevel::Hard:
        s.lookahead = 7.0f + jitter;
        s.lane = lane * 0.4f;
        break;
    }
    return s;
}

float cpu_rubber_band(CpuLevel level) {
    switch (level) {
    case CpuLevel::Easy: return 0.3f;
    case CpuLevel::Normal: return 0.6f;
    case CpuLevel::Hard: return 1.0f;
    }
    return 0.0f;
}

KartInput cpu_input(const World& world, int kart, const CpuSkill& skill) {
    const KartState& k = world.karts()[size_t(kart)];
    const Track& t = world.track();
    if (t.center.empty() || t.line.size() != t.center.size()) return autopilot(k, t, skill.lookahead);

    KartInput in;
    in.accelerate = true;
    const KartParams& p = world.params(kart);
    const PhysicsConstants& c = world.tables().constants;
    const uint32_t tick = uint32_t(world.tick_count());
    const int n = int(t.center.size());
    const float spacing = t.length > 0.0f ? t.length / float(n) : 2.0f;
    const float speed = std::max(k.speed, 0.0f);

    if (!world.racing()) {
        // Countdown: hold the accelerator for the middle of the rocket-start window.
        in.accelerate = skill.rocket_start &&
                        world.countdown_left() <= (c.rocket_window_late + c.rocket_window_early) / 2;
        return in;
    }

    // ---- where to aim: racing line, nudged for item boxes and obstacles
    const float look = skill.lookahead + speed * 0.4f;
    const int target_sample = t.wrap(k.sample + std::max(1, int(std::ceil(look / spacing))));
    const CenterSample& tc = t.center[size_t(target_sample)];
    const Track::LinePoint& lp = t.line[size_t(target_sample)];
    float offset = lp.offset * skill.line_follow + skill.lane;

    const bool wants_box = k.item == Item::None && k.item_next == Item::None && k.roulette == 0;
    if (wants_box) {
        float best = 1e9f, best_off = offset;
        for (size_t b = 0; b < t.item_boxes.size(); ++b) {
            if (world.box_respawn()[b] > 0) continue;
            const Local l = to_local(k, t.item_boxes[b]);
            if (l.z < 4.0f || l.z > 35.0f || std::abs(l.x) > 10.0f || std::abs(l.y) > 4.0f) continue;
            const float box_off = glm::dot(t.item_boxes[b] - tc.pos, tc.right);
            const float cost = std::abs(box_off - offset);
            if (cost < best) best = cost, best_off = box_off;
        }
        offset = best_off;
    }
    if (skill.avoids) {
        float shift = 0.0f;
        const auto& karts = world.karts();
        for (size_t j = 0; j < karts.size(); ++j) {
            if (int(j) == kart) continue;
            const Local l = to_local(k, karts[j].pos);
            if (l.z > 0.0f && l.z < 12.0f && std::abs(l.x) < 1.8f && std::abs(l.y) < 2.0f && karts[j].speed < k.speed + 2.0f)
                shift += -sign(l.x) * (1.8f - std::abs(l.x)) * 1.2f;
        }
        for (const ItemEntity& e : world.entities()) {
            if (e.type != EntityType::Banana && e.type != EntityType::Bomb) continue;
            const Local l = to_local(k, e.pos);
            if (l.z > 0.0f && l.z < 25.0f && std::abs(l.x) < 2.4f && std::abs(l.y) < 3.0f)
                shift += -sign(l.x) * (2.4f - std::abs(l.x)) * 1.5f;
        }
        offset += shift;
    }
    offset = std::clamp(offset, lp.lo, lp.hi);
    const vec3 target = tc.pos + tc.right * offset;

    // ---- pure pursuit: the arc through the target gives the yaw rate we need.
    // Measured along the direction of travel, not the nose: in a drift the
    // nose points well inside the path.
    Local to = to_local(k, target);
    if (k.grounded && speed > 3.0f) {
        vec3 v = k.velocity();
        v -= k.up() * glm::dot(v, k.up());
        if (glm::length(v) > 3.0f) {
            const vec3 vf = glm::normalize(v), vr = glm::cross(vf, k.up());
            const vec3 d = target - k.pos;
            to.x = glm::dot(d, vr);
            to.z = glm::dot(d, vf);
        }
    }
    const float dist2 = std::max(to.x * to.x + to.z * to.z, 1.0f);
    const float yaw_needed = 2.0f * to.x / dist2 * std::max(speed, 4.0f); // rad/s, + = right
    const float speed_fac = std::clamp(std::abs(k.speed) / 4.0f, 0.2f, 1.0f);
    const float at_speed = 1.0f - 0.25f * std::clamp(std::abs(k.speed) / p.top_speed, 0.0f, 1.0f);
    float steer = yaw_needed / (p.turn_rate * at_speed * speed_fac);
    if (to.z < 0.5f) steer = sign(to.x); // target behind: turn round
    steer = std::clamp(steer, -1.0f, 1.0f);
    in.steer = steer;

    // ---- drifting
    // Drift into any corner that keeps turning the same way for a while, even
    // a gentle one: chaining mini-turbos is where the time is.
    const float turn = turn_ahead(t, k.sample + int(4.0f / spacing), 30.0f, spacing);
    const float turn_long = turn_ahead(t, k.sample + int(4.0f / spacing), 55.0f, spacing);
    const bool going_right_way = to.z > 0.5f;
    const bool outward = p.drift == DriftType::Outward;
    const float drift_base = outward ? 0.62f : 0.82f, drift_mod = outward ? 0.38f : 0.28f;
    // A drift can't turn gentler than this; only drift where the corner needs most of it.
    const float min_drift_yaw = p.turn_rate * (drift_base - drift_mod) * speed_fac;
    const float near_turn = turn_ahead(t, k.sample + 1, 14.0f, spacing);
    const float corner_yaw = std::abs(near_turn) / 14.0f * speed;
    const bool want_drift = skill.drifts && k.grounded && going_right_way && speed > c.min_drift_speed + 3.0f &&
                            std::abs(turn) > 0.3f && near_turn * turn > 0.0f && turn_long * sign(turn) > 0.45f && corner_yaw > 0.85f * min_drift_yaw &&
                            !is_offroad(k.surface) && k.bullet_frames == 0 && k.boost_frames == 0;
    // Room left before the inside edge of the road, for a drift towards `dir`.
    auto room_inside = [&](float dir) {
        const CenterSample& here = t.center[size_t(t.wrap(k.sample))];
        const Track::LinePoint& band = t.line[size_t(t.wrap(k.sample))];
        const float lat = glm::dot(k.pos - here.pos, here.right);
        return dir < 0.0f ? lat - band.lo : band.hi - lat;
    };
    if (k.drift == DriftState::Drifting) {
        const float dir = float(k.drift_dir);
        float rel = (yaw_needed * dir / (p.turn_rate * speed_fac) - drift_base) / drift_mod; // +1 = into the turn
        const float rest = turn_ahead(t, k.sample, 12.0f, spacing) * dir;
        const int tier = k.mt_tier(c);
        // Let go once the wanted mini-turbo is ready (and drift again if the
        // corner goes on), when the drift turns tighter than the line, or
        // when the corner has ended.
        const bool cash_in = tier >= skill.max_tier;
        const bool losing_line = rel < (tier >= 1 ? -1.4f : -1.8f);
        const bool straight = rest < 0.05f;
        const bool edge = room_inside(dir) < -0.9f; // the band already keeps 1.6 m off the edge
        if (cash_in || losing_line || straight || edge || !going_right_way) {
            in.drift = false;
        } else {
            // Lean in to charge faster while there's room on the inside.
            if (room_inside(dir) > 2.0f && rel > -0.6f) rel = std::max(rel, 0.5f);
            in.drift = true;
            in.steer = std::clamp(rel, -1.0f, 1.0f) * dir;
        }
    } else if (k.drift == DriftState::Hop) {
        // Keep following the line in the air, with just enough stick to land in the drift.
        const float dir = std::abs(turn) > 0.05f ? sign(turn) : sign(k.hop_steer);
        in.drift = true;
        in.steer = dir * std::max(steer * dir, 0.4f);
    } else if (want_drift && k.grounded && room_inside(sign(turn)) > 2.0f) {
        in.drift = !k.drift_button_prev; // press: hop, then land into the drift
        in.steer = sign(turn) * std::max(steer * sign(turn), 0.4f);
    }
    // Tricks off ramps and boost pads.
    if (!k.grounded && k.drift != DriftState::Drifting && k.drift != DriftState::Hop) {
        in.drift = skill.tricks && k.trick_window > 0 && !k.trick_done && !k.drift_button_prev;
    }

    if (skill.throttle_lift > 0 && int((tick + uint32_t(kart) * 11u) % 32u) < skill.throttle_lift) in.accelerate = false;

    // ---- items
    if (!world.racing() || k.finished) return in;
    const bool react = int((tick / 6u + uint32_t(kart) * 5u) % 8u) < skill.reaction;
    const bool straight_ahead = std::abs(turn_ahead(t, k.sample, 25.0f, spacing)) < 0.35f;
    const bool can_boost = k.grounded && !k.gliding && k.boost_frames == 0 && (straight_ahead || is_offroad(k.surface));
    if (k.golden_frames > 0) {
        if (can_boost) in.item = !k.item_button_prev;
        return in;
    }
    if (k.item == Item::None || k.roulette > 0) return in;

    const auto& karts = world.karts();
    bool ahead = false, behind = false;
    int close_by = 0;
    for (size_t j = 0; j < karts.size(); ++j) {
        if (int(j) == kart || karts[j].finished || karts[j].untouchable()) continue;
        const Local l = to_local(k, karts[j].pos);
        if (std::abs(l.y) > 4.0f) continue;
        if (l.z > 2.0f && l.z < 40.0f && std::abs(l.x) < 2.0f + l.z * 0.05f) ahead = true;
        if (l.z < -1.0f && l.z > -16.0f && std::abs(l.x) < 3.0f) behind = true;
        if (glm::length(karts[j].pos - k.pos) < world.item_tuning().horn_radius * 0.9f) ++close_by;
    }
    bool threat = false;
    for (const ItemEntity& e : world.entities()) {
        if (!hostile_projectile(e.type) || e.owner == kart) continue;
        const vec3 d = k.pos - e.pos;
        const float dist = glm::length(d);
        if (dist < 14.0f && (e.target == kart || glm::dot(e.vel, d) > 0.0f)) threat = true;
    }
    const int place = world.place_of(kart);
    const bool idle_use = (tick + uint32_t(kart) * 37u) % 300u < 2u;

    Act act = Act::Nothing;
    switch (k.item) {
    case Item::Banana: act = behind ? Act::FireBack : Act::Hold; break;
    case Item::GreenShell:
    case Item::Bomb: act = ahead ? Act::Fire : behind ? Act::FireBack : Act::Hold; break;
    case Item::RedShell: act = place > 1 ? Act::Fire : behind ? Act::FireBack : Act::Hold; break;
    case Item::TripleBanana: act = behind ? Act::FireBack : Act::Nothing; break;
    case Item::TripleGreen: act = ahead ? Act::Fire : behind ? Act::FireBack : Act::Nothing; break;
    case Item::TripleRed: act = place > 1 ? Act::Fire : behind ? Act::FireBack : Act::Nothing; break;
    case Item::SpikedShell:
    case Item::Bullet:
    case Item::Ink:
    case Item::Lightning: act = place > 1 ? Act::Fire : Act::Nothing; break;
    case Item::Mushroom:
    case Item::TripleMushroom:
    case Item::GoldenMushroom: act = can_boost ? Act::Fire : Act::Nothing; break;
    case Item::Star:
    case Item::Coin:
    case Item::Ghost:
    case Item::Chomper: act = Act::Fire; break;
    case Item::Fireball:
    case Item::Boomerang: act = ahead || idle_use ? Act::Fire : Act::Nothing; break;
    case Item::Horn: act = threat || close_by >= 2 ? Act::Fire : Act::Nothing; break;
    case Item::LuckyEight: act = ahead || behind || idle_use ? Act::Fire : Act::Nothing; break;
    default: break;
    }
    const bool trails = item_trails(k.item);
    if ((act == Act::Fire || act == Act::FireBack) && !react) act = trails ? Act::Hold : Act::Nothing;

    switch (act) {
    case Act::Nothing: in.item = false; break;
    case Act::Hold: in.item = k.trailing || !k.item_button_prev; break;
    case Act::Fire:
    case Act::FireBack:
        in.item_back = act == Act::FireBack;
        // A held item flies when the button is let go; anything else on the press.
        in.item = trails ? !k.trailing && !k.item_button_prev : !k.item_button_prev;
        break;
    }
    return in;
}

Build cpu_build(const PartsCatalog& parts, int seat) {
    auto pick = [seat](size_t n, int mul, int add) { return n == 0 ? 0 : int((size_t(seat) * size_t(mul) + size_t(add)) % n); };
    return {pick(parts.drivers.size(), 7, 1), pick(parts.bodies.size(), 3, 0), pick(parts.tires.size(), 5, 0),
            pick(parts.gliders.size(), 1, 0)};
}

} // namespace gk::sim
