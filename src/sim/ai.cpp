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
float turn_ahead(const Track& t, int from, float metres, float spacing, uint32_t route) {
    const int steps = std::max(1, int(metres / spacing));
    float sum = 0.0f;
    for (int i = 0, s = from; i < steps; ++i, s = t.next(s, route)) sum += t.line[size_t(s)].curvature * spacing;
    return sum;
}

// Which side roads this CPU takes this lap: a fixed mix per branch, so the
// field spreads over them, and the same on the server and every client.
uint32_t cpu_route(const World& w, int kart) {
    const Track& t = w.track();
    const KartState& k = w.karts()[size_t(kart)];
    uint32_t route = 0;
    for (size_t b = 0; b < t.branches.size() && b < 32; ++b) {
        uint32_t h = uint32_t(kart + 1) * 2654435761u ^ uint32_t(k.lap) * 40503u ^ uint32_t(b + 1) * 2246822519u;
        h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
        if (float(h % 1000u) < t.branches[b].ai_share * 1000.0f) route |= 1u << b;
    }
    return route;
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
        s.edge_room = 2.4f;
        s.mistakes = 50;
        s.brushes = true;
        break;
    case CpuLevel::Normal:
        s.lookahead = 7.5f + jitter;
        s.line_follow = 0.8f;
        s.lane = lane * 0.8f;
        s.max_tier = 1; // settles for blue sparks; hard holds the drift for orange and purple
        s.tricks = seat % 2 == 0;
        s.reaction = 6;
        s.rocket_start = seat % 3 != 2;
        s.glide_dive = 0.4f;
        s.weave = 1.0f;
        s.overtakes = true;
        s.mistakes = 45; // about one a lap
        break;
    case CpuLevel::Hard:
        s.lookahead = 7.0f + jitter;
        s.lane = lane * 0.4f;
        s.glide_dive = 0.85f;
        s.edge_room = 1.0f;
        s.weave = 1.5f;
        s.overtakes = true;
        s.mistakes = 22; // half as often
        break;
    }
    return s;
}

// The nose while gliding: aim at the landing. With height to spare over
// the far side of the gap the CPU dives for speed (as hard as its skill
// says); with barely enough it pulls up and glides flat; in between it
// flies hands off at trim.
float glide_pitch(const KartState& k, const Track& t, const KartParams& p, const PhysicsConstants& c,
                  const CpuSkill& skill, uint32_t route) {
    const int n = int(t.road.size());
    if (n == 0) return 0.0f;
    const float spacing = t.length > 0.0f ? t.length / float(std::max<int>(1, int(t.center.size()))) : 2.0f;
    int i = k.sample, steps = 0;
    while (!t.sample(i).has_floor && steps < 200) i = t.next(i, route), ++steps;
    if (steps == 0) return 0.0f; // not over the gap yet (or past it): fly level
    const CenterSample& land = t.sample(i);
    const float ahead = float(steps) * spacing + 6.0f; // and a little past the lip
    const float height = k.pos.y - land.pos.y;
    if (height < 0.5f) return 0.6f; // under the far lip: pull up and hope
    const float need = ahead / height;   // metres forward per metre of drop to make it
    const float ratio = c.glide_ratio;
    if (need < ratio * 0.55f) return -skill.glide_dive;
    if (need > ratio * 0.9f) return 0.6f;
    return 0.0f;
}

float cpu_rubber_band(CpuLevel level) {
    switch (level) {
    case CpuLevel::Easy: return 0.3f;
    case CpuLevel::Normal: return 0.6f;
    case CpuLevel::Hard: return 1.0f;
    }
    return 0.0f;
}

namespace {
constexpr uint32_t kSpellTicks = 1200;  // a slip is rolled once per 20 s spell
constexpr uint32_t kSlipTicks = 90;     // and lasts 1.5 s
uint32_t mix_hash(uint32_t h) {
    h ^= h >> 15, h *= 2246822519u, h ^= h >> 13, h *= 3266489917u, h ^= h >> 16;
    return h;
}
} // namespace

// Each kart's spells are staggered so the field doesn't slip in unison. The
// roll decides whether this spell has a slip, when in the spell, and which.
CpuMistake cpu_mistake(const World& world, int kart, const CpuSkill& skill) {
    if (skill.mistakes <= 0 || !world.racing()) return CpuMistake::None;
    const uint32_t tick = uint32_t(world.tick_count()) + uint32_t(kart) * 977u;
    const uint32_t spell = tick / kSpellTicks, at = tick % kSpellTicks;
    const uint32_t h = mix_hash(spell * 2654435761u ^ uint32_t(kart + 1) * 40503u);
    if (int(h % 100u) >= skill.mistakes) return CpuMistake::None;
    const uint32_t start = (h >> 8) % (kSpellTicks - kSlipTicks);
    if (at < start || at >= start + kSlipTicks) return CpuMistake::None;
    if (skill.brushes) return CpuMistake::WallBrush;
    return (h >> 20) & 1u ? CpuMistake::EarlyRelease : CpuMistake::MissedApex;
}

KartInput cpu_input(const World& world, int kart, const CpuSkill& skill) {
    const KartState& k = world.karts()[size_t(kart)];
    const Track& t = world.track();
    if (t.center.empty() || t.line.size() != t.road.size()) return autopilot(k, t, skill.lookahead);
    const uint32_t route = cpu_route(world, kart);
    // On a side road that's shut short of its barrier: drive as if on the
    // main road beside it. Past the barrier, or on a road that vanishes (it
    // stays while the kart is on it), the way on is clear: drive on.
    int cur = k.sample;
    if (const int b = t.sample(cur).branch; b >= 0 && !t.branch_open(b) && !t.branches[size_t(b)].vanish) {
        bool shut_ahead = false;
        for (const Track::Barrier& bar : t.barriers) shut_ahead |= bar.branch == b && bar.sample >= cur;
        if (shut_ahead) cur = t.main_sample(cur);
    }

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
    int target_sample = t.advance(cur, std::max(1, int(std::ceil(look / spacing))), route);
    // On the wrong side of an obstacle ahead: aim at the gap beside it.
    for (int i = t.next(cur, route), guard = 0; i != target_sample && guard < 200; i = t.next(i, route), ++guard) {
        if (!t.line[size_t(i)].blocked) continue;
        const float lat = glm::dot(k.pos - t.sample(i).pos, t.sample(i).right);
        if (lat < t.line[size_t(i)].lo - 0.3f || lat > t.line[size_t(i)].hi + 0.3f) target_sample = t.advance(i, 2, route);
        break;
    }
    // A side road this CPU wants, entered at its mouth: the kart is only on it
    // once it's further onto it than onto the main road, so for the first few
    // metres past the fork keep aiming down the side road, not back onto the main one.
    int side_road = -1;
    for (size_t b = 0; b < t.branches.size() && b < 32 && cur < n; ++b) {
        const Track::Branch& br = t.branches[b];
        if (!(route >> b & 1u) || !t.branch_open(int(b))) continue;
        const int past = ((cur - br.fork) % n + n) % n;
        if (past > 0 && float(past) * spacing <= 10.0f && float(br.side) * glm::dot(k.pos - t.sample(cur).pos, t.sample(cur).right) > 0.0f)
            target_sample = t.advance(br.first + std::min(past, br.count - 1), std::max(1, int(std::ceil(look / spacing))), route);
    }
    if (target_sample >= n)
        for (size_t b = 0; b < t.branches.size() && b < 32; ++b)
            if (target_sample >= t.branches[b].first && target_sample < t.branches[b].first + t.branches[b].count) side_road = int(b);
    const CenterSample& tc = t.sample(target_sample);
    const Track::LinePoint& lp = t.line[size_t(target_sample)];
    float offset = lp.offset * skill.line_follow + skill.lane;
    const CpuMistake slip = cpu_mistake(world, kart, skill);
    // How much of the road this CPU uses: the line keeps 1.6 m off the edge;
    // a harder CPU clips the apex closer to the inside edge, an easier one
    // stays further from it. A missed apex runs wide of the line.
    const float extra = 1.6f - skill.edge_room;
    const bool inside = lp.offset * lp.curvature > 0.0f; // the line is on the inside of its bend
    if (std::abs(lp.offset) > 0.1f && (inside || extra < 0.0f)) {
        const float side_band = std::max(lp.offset > 0.0f ? lp.hi : -lp.lo, 0.1f);
        offset += sign(lp.offset) * extra * std::min(1.0f, std::abs(lp.offset) / side_band);
    }
    if (slip == CpuMistake::MissedApex) offset -= lp.offset * skill.line_follow * 0.7f;
    // On a straight, sway across the road: harder to draft and to hit, and
    // it reads as a driver, not a rail. Not where the road is narrow.
    const float room = std::min(lp.hi - lp.lo, t.line[size_t(cur)].hi - t.line[size_t(cur)].lo);
    const float bend = std::abs(turn_ahead(t, cur, 40.0f, spacing, route));
    if (skill.weave > 0.0f && room > 5.0f) {
        if (bend < 0.15f) { // and fading out as a corner comes up
            const float phase = (float(tick % 360u) / 360.0f + 0.37f * float(kart)) * 2.0f * 3.14159265f;
            const float sway = skill.weave * std::min(1.0f, (room - 5.0f) / 3.0f); // full sway needs 8 m of road
            offset += sway * std::sin(phase) * std::min(1.0f, (0.15f - bend) / 0.08f);
        }
    }

    // Head for an item box when empty-handed, but not out of a drift (a
    // swerve across the road drops the mini-turbo, worth more than the box)
    // and only a box the kart can still reach: the last box of a row to pass
    // out of range must not fling the aim at the next one over.
    const bool wants_box = world.config().items && k.item == Item::None && k.item_next == Item::None && k.roulette == 0 &&
                           k.drift != DriftState::Drifting && k.drift != DriftState::Hop;
    if (wants_box) {
        float best = 1e9f, best_off = offset;
        for (size_t b = 0; b < t.item_boxes.size(); ++b) {
            if (world.box_respawn()[b] > 0) continue;
            const Local l = to_local(k, t.item_boxes[b]);
            if (l.z < 4.0f || l.z > 35.0f || std::abs(l.x) > 10.0f || std::abs(l.y) > 4.0f) continue;
            const float box_off = glm::dot(t.item_boxes[b] - tc.pos, tc.right);
            const float cost = std::abs(box_off - offset);
            if (cost > l.z * 0.5f) continue; // too far across for the distance left
            if (cost < best) best = cost, best_off = box_off;
        }
        offset = best_off;
    }
    // A side road this CPU wants that opens with a switch: drive over the switch.
    for (size_t b = 0; b < t.branches.size() && b < 32; ++b) {
        const int g = t.branches[b].gate;
        if (!(route >> b & 1u) || g < 0 || t.branch_open(int(b))) continue;
        const Gate& gate = t.gates[size_t(g)];
        if (gate.when != Gate::When::Switch || gate.invert || gate.sw < 0 || size_t(gate.sw) >= t.switches.size()) continue;
        const Track::Switch& sw = t.switches[size_t(gate.sw)];
        const Local l = to_local(k, sw.pos);
        if (l.z > 3.0f && l.z < 35.0f && std::abs(l.x) < 12.0f && std::abs(l.y) < 3.0f) offset = glm::dot(sw.pos - tc.pos, tc.right);
    }
    if (skill.avoids) {
        float shift = 0.0f;
        const auto& karts = world.karts();
        // A slower kart ahead: commit to the side with more room and aim a
        // kart's width past it, keeping the side once off its tail. Without
        // `overtakes`, just a nudge away from it.
        int slow = -1;
        float slow_z = 1e9f;
        for (size_t j = 0; j < karts.size(); ++j) {
            if (int(j) == kart) continue;
            const Local l = to_local(k, karts[j].pos);
            if (l.z > 0.0f && l.z < 12.0f && std::abs(l.x) < 1.8f && std::abs(l.y) < 2.0f && karts[j].speed < k.speed + 2.0f) {
                shift += -sign(l.x) * (1.8f - std::abs(l.x)) * 1.2f;
                if (skill.overtakes && l.z < slow_z && l.z < 16.0f && !karts[j].finished && karts[j].respawn_frames == 0) slow = int(j), slow_z = l.z;
            } else if (skill.overtakes && l.z > 0.0f && l.z < 16.0f && std::abs(l.x) < 2.6f && std::abs(l.y) < 2.0f && karts[j].speed < k.speed - 1.0f &&
                       !karts[j].finished && karts[j].respawn_frames == 0 && l.z < slow_z) {
                slow = int(j), slow_z = l.z;
            }
        }
        // Only on road wide enough here and at the aim point, and not into a
        // corner: leaving the line for the edge of the road mid-bend is how
        // a CPU meets the wall.
        if (slow >= 0 && room > 6.0f && bend < 0.12f) {
            const CenterSample& here = t.sample(cur);
            const float lat_them = glm::dot(karts[size_t(slow)].pos - here.pos, here.right);
            const float lat_me = glm::dot(k.pos - here.pos, here.right);
            const float lo_room = std::max(lp.lo, t.line[size_t(cur)].lo) + 0.4f, hi_room = std::min(lp.hi, t.line[size_t(cur)].hi) - 0.4f;
            float side = (hi_room - lat_them) >= (lat_them - lo_room) ? 1.0f : -1.0f;
            if (std::abs(lat_me - lat_them) > 0.6f) side = sign(lat_me - lat_them); // already going round one side: stay there
            offset = std::clamp(lat_them + side * 2.2f, lo_room, hi_room);
            shift = 0.0f;
        }
        for (const ItemEntity& e : world.entities()) {
            if (e.type != EntityType::Banana && e.type != EntityType::Bomb && e.type != EntityType::OilSlick) continue;
            const Local l = to_local(k, e.pos);
            if (l.z > 0.0f && l.z < 25.0f && std::abs(l.x) < 2.4f && std::abs(l.y) < 3.0f)
                shift += -sign(l.x) * (2.4f - std::abs(l.x)) * 1.5f;
        }
        offset += shift;
    }
    // Obstacles between here and the target: aim so the straight path there
    // passes them on the open side.
    float lo = lp.lo - extra, hi = lp.hi + extra;
    if (slip == CpuMistake::WallBrush) { // drifts out to the wall for a moment
        const float side = ((tick / kSpellTicks + uint32_t(kart)) & 1u) ? 1.0f : -1.0f;
        offset = side > 0.0f ? hi + 1.3f : lo - 1.3f;
        if (side > 0.0f) hi = offset;
        else lo = offset;
    }
    for (int i = t.next(cur, route), guard = 0; i != target_sample && guard < 200; i = t.next(i, route), ++guard) {
        const Track::LinePoint& b = t.line[size_t(i)];
        if (!b.blocked) continue;
        if (std::max(lo, b.lo) > std::min(hi, b.hi)) { // can't satisfy both: the nearer obstacle wins
            lo = b.lo, hi = b.hi;
            break;
        }
        lo = std::max(lo, b.lo), hi = std::min(hi, b.hi);
    }
    offset = std::clamp(offset, lo, hi);
    // Lava (or any out-of-bounds zone) that's on between here and the target:
    // keep to the side of the road it leaves clear.
    // One on a timer counts from a little before it comes on.
    auto lava_soon = [&](int gate) {
        if (t.gate_holds(gate)) return true;
        const Gate& g = t.gates[size_t(gate)];
        if (g.when != Gate::When::Cycle) return false;
        const int period = std::max(g.period, 1);
        const int phase = ((world.race_ticks() - g.offset) % period + period) % period;
        const int until = g.invert ? g.open - phase : period - phase;
        return until > 0 && until < 110;
    };
    bool dodging_lava = false;
    for (const Track::Zone& z : t.zones) {
        if (z.surface != Surface::OutOfBounds || !lava_soon(z.gate)) continue;
        bool ahead = false;
        for (int i = cur, guard = 0; guard < 30 && !ahead; i = t.next(i, route), ++guard) ahead = i >= z.first && i < z.first + z.count;
        if (!ahead) continue;
        const float here = glm::dot(k.pos - t.sample(cur).pos, t.sample(cur).right);
        const bool in_path = !(offset < z.lo - 3.0f || offset > z.hi + 3.0f) || !(here < z.lo - 2.0f || here > z.hi + 2.0f);
        if (!in_path) continue;
        dodging_lava = true;
        if (z.lo - 3.0f > lo - 1.5f && (z.hi + 3.0f > hi + 1.5f || offset - z.lo < z.hi - offset)) offset = z.lo - 3.0f;
        else if (z.hi + 3.0f < hi + 1.5f) offset = z.hi + 3.0f;
    }
    // Barriers standing up ahead (gates shut, a door down, hay bales): pass on
    // the open side. The kart cuts across towards its aim point, so test where
    // that path crosses the barrier, not where the aim point is: in a slalom
    // the next bale stands between here and there.
    const float target_z = std::max(to_local(k, tc.pos + tc.right * offset).z, 1.0f);
    for (const Track::Barrier& b : t.barriers) {
        if (!t.barrier_up(b)) continue;
        const Local l = to_local(k, b.pos);
        if (l.z < -2.0f || l.z > look + 12.0f || std::abs(l.y) > 4.0f) continue;
        const CenterSample& bs = t.sample(b.sample);
        const float here = glm::dot(k.pos - bs.pos, bs.right);
        const float f = std::clamp(l.z / target_z, 0.15f, 1.0f);
        const float path = here + (offset - here) * f;
        const float across = glm::dot(b.pos - bs.pos, bs.right);
        const float half = std::abs(glm::dot(b.right, bs.right)) * b.half.x + std::abs(glm::dot(b.forward, bs.right)) * b.half.z + 1.6f;
        if (path < across - half || path > across + half) continue;
        const float left = across - half, right = across + half;
        const bool left_ok = left > lo - 2.0f, right_ok = right < hi + 2.0f;
        float want = path;
        if (left_ok && (!right_ok || path - left < right - path)) want = left;
        else if (right_ok) want = right;
        offset = std::clamp(here + (want - here) / f, lo - 1.0f, hi + 1.0f);
    }
    // Heading for a side road whose fork is still ahead: the straight path
    // there must stay on the main road up to the fork, or it cuts across the
    // verge into the fence beside the mouth.
    if (side_road >= 0 && cur < n) {
        const Track::Branch& br = t.branches[size_t(side_road)];
        const int ahead = ((br.fork - cur) % n + n) % n;
        const CenterSample& fs = t.sample(br.fork);
        const vec3 aim = tc.pos + tc.right * offset;
        const float to_fork = glm::dot(fs.pos - k.pos, fs.tangent), to_aim = glm::dot(aim - k.pos, fs.tangent);
        const float facing = glm::dot(tc.right, fs.right);
        if (ahead > 0 && ahead < n / 2 && to_fork > 0.0f && to_aim > to_fork && facing > 0.3f) {
            const float here = glm::dot(k.pos - fs.pos, fs.right), there = glm::dot(aim - fs.pos, fs.right);
            const float f = to_fork / to_aim;
            const float edge = float(br.side) * std::max(fs.half_width - 1.0f, 0.0f);
            const float path = here + (there - here) * f;
            if (float(br.side) * (path - edge) > 0.0f) offset += (here + (edge - here) / f - there) / facing;
        }
    }
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

    // Pinned nose-first against a wall or an obstacle: back off, swinging the
    // nose towards the target, until there's room to drive round it.
    if (k.grounded && k.speed < 2.0f && !k.knocked()) {
        bool blocked = false;
        float side_hit = 0.0f; // > 0: the blockage is more on the right
        // Once backing off, keep going until there's real room ahead. Going
        // nowhere for a moment already (nudging a corner, pulling back a few
        // inches and nudging it again): back right off instead of dithering.
        const bool wedged = k.stuck_frames > 30;
        const float look = k.speed < -0.3f || wedged ? 5.0f : 2.2f;
        float nearest = 1e9f, near_side = 0.0f;
        for (float side : {-1.4f, -0.7f, 0.0f, 0.7f, 1.4f}) {
            const auto hit = t.raycast(k.pos + k.up() * 0.5f + k.right() * side, k.forward(), 5.0f);
            if (!hit || hit->surface != Surface::Wall) continue;
            if (std::abs(side) <= 0.7f && hit->distance <= look) {
                blocked = true;
                side_hit += side;
            }
            if (hit->distance < nearest) nearest = hit->distance, near_side = side;
        }
        if (blocked) {
            in.accelerate = false;
            in.brake = true;
            // Reversing turns the nose the other way to the stick: swing it
            // towards the target, or away from the side that's blocked.
            // The open side of the road: towards the middle of the CPU band.
            const CenterSample& here = t.sample(cur);
            const Track::LinePoint& band = t.line[size_t(t.advance(cur, 3, route))];
            const float lat = glm::dot(k.pos - here.pos, here.right), mid = 0.5f * (band.lo + band.hi);
            const float want = std::abs(mid - lat) > 0.8f ? sign(mid - lat) : side_hit > 0.0f ? -1.0f : 1.0f;
            in.steer = -want;
            return in;
        }
        if (wedged && near_side != 0.0f) {
            // Crawling clear of something at the nose: steer away from it
            // rather than back into it towards the line.
            in.steer = near_side > 0.0f ? -1.0f : 1.0f;
            in.accelerate = true;
            return in;
        }
    }

    // ---- drifting
    // Drift into any corner that keeps turning the same way for a while, even
    // a gentle one: chaining mini-turbos is where the time is.
    const int ahead4 = t.advance(cur, int(4.0f / spacing), route);
    const float turn = turn_ahead(t, ahead4, 30.0f, spacing, route);
    const float turn_long = turn_ahead(t, ahead4, 55.0f, spacing, route);
    const bool going_right_way = to.z > 0.5f;
    // The gentlest arc worth a drift, as a fraction of the kart's full lock
    // (a counter-steered drift can turn gentler still, since the snaking
    // tune, but a corner needing less than this isn't worth one).
    constexpr float kGentlestDrift = 0.12f;
    const float inward = drift_turn_target(c, p, 0.0f) / std::max(c.drift_turn_neutral, 1e-3f); // the body's inward bonus
    const float min_drift_yaw = p.turn_rate * kGentlestDrift * inward * speed_fac;
    const float near_turn = turn_ahead(t, t.next(cur, route), 14.0f, spacing, route);
    const float corner_yaw = std::abs(near_turn) / 14.0f * speed;
    const bool want_drift = skill.drifts && !dodging_lava && k.grounded && going_right_way && speed > c.min_drift_speed + 3.0f &&
                            std::abs(turn) > 0.3f && near_turn * turn > 0.0f && turn_long * sign(turn) > 0.45f && corner_yaw > 0.85f * min_drift_yaw &&
                            !is_offroad(k.surface) && k.bullet_frames == 0 && k.boost_frames == 0;
    // Room left before the inside edge of the road, for a drift towards `dir`.
    auto room_inside = [&](float dir) {
        const CenterSample& here = t.sample(cur);
        const Track::LinePoint& band = t.line[size_t(cur)];
        const float lat = glm::dot(k.pos - here.pos, here.right);
        return dir < 0.0f ? lat - (band.lo - extra) : band.hi + extra - lat;
    };
    if (k.drift == DriftState::Drifting) {
        const float dir = float(k.drift_dir);
        const float f_needed = yaw_needed * dir / (p.turn_rate * speed_fac * inward); // the arc the line wants, as a fraction of full lock
        float rel = drift_rel_for(c, p, yaw_needed * dir / (p.turn_rate * speed_fac));  // +1 = into the turn
        const float rest = turn_ahead(t, cur, 12.0f, spacing, route) * dir;
        const int tier = k.mt_tier(c);
        // Let go once the wanted mini-turbo is ready (and drift again if the
        // corner goes on), when the line bends the other way (further than
        // a little, while there's no spark yet), or when the corner has ended.
        const bool cash_in = tier >= skill.max_tier || slip == CpuMistake::EarlyRelease; // a slip: lets go before the sparks
        const bool losing_line = f_needed < (tier >= 1 ? 0.0f : -0.12f);
        const bool straight = rest < 0.05f;
        const bool edge = room_inside(dir) < -0.9f; // the band already keeps 1.6 m off the edge
        if (cash_in || losing_line || straight || edge || !going_right_way || dodging_lava) {
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
    } else if (want_drift && k.grounded && room_inside(sign(turn)) > 2.0f && slip != CpuMistake::EarlyRelease) {
        in.drift = !k.drift_button_prev; // press: hop, then land into the drift
        in.steer = sign(turn) * std::max(steer * sign(turn), 0.4f);
    }
    // Gliding turns by banking, which takes a moment: steer harder and
    // ease off as the bank builds, so the glider doesn't overshoot.
    if (k.gliding) {
        in.steer = std::clamp(in.steer * 1.7f - k.glide_bank * 0.5f, -1.0f, 1.0f);
        in.pitch = glide_pitch(k, t, p, c, skill, route);
    }
    // Tricks off ramps and boost pads.
    if (!k.grounded && k.drift != DriftState::Drifting && k.drift != DriftState::Hop) {
        in.drift = skill.tricks && k.trick_window > 0 && !k.trick_done && !k.drift_button_prev;
    }

    if (skill.throttle_lift > 0 && int((tick + uint32_t(kart) * 11u) % 32u) < skill.throttle_lift) in.accelerate = false;

    // ---- items
    if (!world.racing() || k.finished) return in;
    const bool react = int((tick / 6u + uint32_t(kart) * 5u) % 8u) < skill.reaction;
    const bool straight_ahead = std::abs(turn_ahead(t, cur, 25.0f, spacing, route)) < 0.35f;
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
    case Item::Banana:
    case Item::OilSlick: act = behind ? Act::FireBack : Act::Hold; break;
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
