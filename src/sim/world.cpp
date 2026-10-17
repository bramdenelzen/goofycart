#include "gk/sim/world.h"

#include "gk/core/fixed_step.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace gk::sim {

World::World(Track track, PhysicsTables tables, RaceConfig config)
    : track_(std::move(track)), tables_(std::move(tables)), config_(config), countdown_left_(config.countdown_frames) {
    track_.laps = config_.laps;
    box_respawn_.assign(track_.item_boxes.size(), 0);
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
    countdown_left_ = s.countdown_left;
    race_ticks_ = s.race_ticks;
    for (size_t i = 0; i < karts_.size() && i < s.karts.size(); ++i) karts_[i] = s.karts[i];
    entities_ = s.entities;
    if (s.box_respawn.size() == box_respawn_.size()) box_respawn_ = s.box_respawn;
    rng_ = s.rng;
}

void World::tick(std::span<const KartInput> inputs) {
    events_.clear();
    ++tick_;
    const bool frozen = countdown_left_ > 0;
    step_inputs_.assign(karts_.size(), KartInput{});
    for (size_t i = 0; i < karts_.size() && i < inputs.size(); ++i) step_inputs_[i] = inputs[i];
    if (config_.items && !frozen) items_before_step(step_inputs_);
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
            kart_on_go(karts_[i], ctx);
        }
    }
    collide_karts();
    if (config_.items && !frozen) items_after_step();
    if (!frozen) ++race_ticks_;
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

void World::collide_karts() {
    constexpr float kRadius = 0.75f;
    for (size_t a = 0; a < karts_.size(); ++a) {
        for (size_t b = a + 1; b < karts_.size(); ++b) {
            KartState& ka = karts_[a];
            KartState& kb = karts_[b];
            if (ka.respawn_frames > 0 || kb.respawn_frames > 0) continue;
            if (ka.ghost_frames > 0 || kb.ghost_frames > 0) continue; // ghosts pass through
            vec3 d = kb.pos - ka.pos;
            if (std::abs(d.y) > 1.5f) continue;
            d.y = 0.0f;
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
            const vec3 n = d / dist;
            const float ma = params_[a].mass, mb = params_[b].mass;
            const float overlap = 2.0f * kRadius - dist;
            ka.pos -= n * overlap * (mb / (ma + mb));
            kb.pos += n * overlap * (ma / (ma + mb));
            vec3 va = ka.velocity(), vb = kb.velocity();
            const float rel = glm::dot(vb - va, n);
            if (rel >= 0.0f) continue;
            // Elastic-ish exchange along the contact normal, weighted by KartBound mass.
            const float j = -(1.0f + 0.6f) * rel / (1.0f / ma + 1.0f / mb);
            va -= n * (j / ma);
            vb += n * (j / mb);
            auto apply = [](KartState& k, vec3 v) {
                if (k.grounded) {
                    const vec3 f = k.forward(), u = k.up();
                    k.speed = glm::dot(v, f);
                    vec3 lat = v - f * k.speed;
                    k.slide = lat - u * glm::dot(lat, u);
                } else {
                    k.air_vel = v;
                }
            };
            apply(ka, va);
            apply(kb, vb);
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
    const vec3 target = track.center[size_t((kart.sample + ahead) % n)].pos;
    vec3 to = target - kart.pos;
    const vec3 f = kart.forward(), r = kart.right();
    const float x = glm::dot(to, r), z = glm::dot(to, f);
    const float angle = std::atan2(x, std::max(z, 0.1f));
    in.steer = std::clamp(angle * 2.2f, -1.0f, 1.0f);
    return in;
}

} // namespace gk::sim
