// Items: boxes and the roulette, the two item slots, everything that can be
// thrown or dropped, and what happens to a kart that gets hit. Part of the
// deterministic World step, so clients predict it exactly like the server.
#include "gk/sim/world.h"

#include "gk/core/fixed_step.h"

#include <algorithm>
#include <cmath>

namespace gk::sim {

const char* item_name(Item item) {
    switch (item) {
    case Item::None: return "";
    case Item::Banana: return "Banana";
    case Item::TripleBanana: return "Triple Bananas";
    case Item::GreenShell: return "Green Shell";
    case Item::TripleGreen: return "Triple Green Shells";
    case Item::RedShell: return "Red Shell";
    case Item::TripleRed: return "Triple Red Shells";
    case Item::SpikedShell: return "Spiked Shell";
    case Item::Bomb: return "Bomb";
    case Item::Mushroom: return "Mushroom";
    case Item::TripleMushroom: return "Triple Mushrooms";
    case Item::GoldenMushroom: return "Golden Mushroom";
    case Item::Bullet: return "Bullet";
    case Item::Ink: return "Ink";
    case Item::Lightning: return "Lightning";
    case Item::Star: return "Star";
    case Item::Fireball: return "Fireball";
    case Item::Boomerang: return "Boomerang";
    case Item::Chomper: return "Chomper";
    case Item::Horn: return "Horn";
    case Item::LuckyEight: return "Lucky Eight";
    case Item::Coin: return "Coin";
    case Item::Ghost: return "Ghost";
    case Item::OilSlick: return "Oil Slick";
    case Item::Count: break;
    }
    return "?";
}

int item_uses(Item item) {
    switch (item) {
    case Item::None: return 0;
    case Item::TripleBanana:
    case Item::TripleGreen:
    case Item::TripleRed:
    case Item::TripleMushroom:
    case Item::Boomerang: return 3;
    case Item::LuckyEight: return 8;
    case Item::Fireball: return 10;
    default: return 1;
    }
}

bool item_trails(Item item) {
    return item == Item::Banana || item == Item::GreenShell || item == Item::RedShell || item == Item::Bomb || item == Item::OilSlick;
}

namespace {

// Roulette weights per place band: 1st, near the front, middle, behind, far
// behind. Provisional: shaped like MK8 Deluxe's tables, not copied from them.
constexpr int kBands = 5;
struct Odds {
    Item item;
    int w[kBands];
};
constexpr Odds kOdds[] = {
    {Item::Banana, {30, 14, 7, 2, 0}},        {Item::TripleBanana, {5, 8, 6, 2, 0}},
    {Item::GreenShell, {28, 18, 11, 5, 2}},   {Item::TripleGreen, {2, 8, 10, 7, 3}},
    {Item::RedShell, {0, 15, 15, 11, 7}},     {Item::TripleRed, {0, 4, 10, 12, 11}},
    {Item::SpikedShell, {0, 1, 3, 4, 4}},     {Item::Bomb, {0, 4, 6, 4, 2}},
    {Item::Mushroom, {5, 14, 14, 11, 7}},     {Item::TripleMushroom, {0, 5, 10, 14, 14}},
    {Item::GoldenMushroom, {0, 0, 4, 8, 11}}, {Item::Bullet, {0, 0, 1, 6, 13}},
    {Item::Ink, {0, 3, 5, 3, 1}},             {Item::Lightning, {0, 0, 1, 3, 6}},
    {Item::Star, {0, 1, 4, 8, 11}},           {Item::Fireball, {2, 6, 6, 4, 2}},
    {Item::Boomerang, {2, 6, 6, 4, 2}},       {Item::Chomper, {0, 3, 6, 6, 4}},
    {Item::Horn, {6, 6, 4, 2, 1}},            {Item::LuckyEight, {0, 0, 2, 4, 5}},
    {Item::Coin, {22, 9, 4, 1, 0}},           {Item::Ghost, {0, 3, 4, 3, 2}},
    {Item::OilSlick, {12, 9, 6, 2, 0}},
};
static_assert(sizeof(kOdds) / sizeof(kOdds[0]) == kItemCount);

// What a Lucky Eight hands out, in order.
constexpr Item kLuckyEight[8] = {Item::Banana, Item::GreenShell, Item::RedShell, Item::Mushroom,
                                 Item::Bomb,   Item::Ink,        Item::Star,     Item::Coin};

constexpr int kMaxEntities = 64;
constexpr float kHitRadius = 1.25f;      // entity centre to kart centre
constexpr float kBoxRadius = 1.7f;
constexpr float kEntityRadius = 0.35f;

void emit(std::vector<KartEvent>& ev, KartEventType t, int kart, int value = 0) { ev.push_back({t, kart, value}); }

bool solid(EntityType t) {
    return t == EntityType::Banana || t == EntityType::GreenShell || t == EntityType::RedShell || t == EntityType::Bomb ||
           t == EntityType::Fireball;
}

} // namespace

Item roll_item(Rng& rng, int place, int karts, uint32_t blocked) {
    int band = 0;
    if (place > 1 && karts > 1) band = std::clamp(1 + (place - 2) * (kBands - 1) / std::max(1, karts - 1), 1, kBands - 1);
    // Blocked items leave the table; the rest keep their weights. One draw,
    // always, so prediction and the server stay in step.
    auto weight = [&](const Odds& o) { return blocked & item_bit(o.item) ? 0 : o.w[band]; };
    int total = 0;
    for (const Odds& o : kOdds) total += weight(o);
    if (total == 0) return band == 0 ? Item::Coin : Item::Mushroom;
    int r = rng.range(total);
    for (const Odds& o : kOdds) {
        if (r < weight(o)) return o.item;
        r -= weight(o);
    }
    return Item::Banana;
}

uint32_t World::blocked_items(int kart) const {
    const auto& t = item_tuning_;
    int spiked = 0, lightning = 0, ink = 0;
    for (const KartState& k : karts_) {
        spiked += int(k.item == Item::SpikedShell) + int(k.item_next == Item::SpikedShell);
        lightning += int(k.item == Item::Lightning) + int(k.item_next == Item::Lightning);
        ink += int(k.item == Item::Ink) + int(k.item_next == Item::Ink);
    }
    for (const ItemEntity& e : entities_) spiked += int(e.type == EntityType::SpikedShell);
    uint32_t blocked = 0;
    if (spiked >= t.spiked_limit || race_ticks_ < t.spiked_start_gate || spiked_cooldown_ > 0) blocked |= item_bit(Item::SpikedShell);
    const KartState& me = karts_[size_t(kart)];
    const bool holds_lightning = me.item == Item::Lightning || me.item_next == Item::Lightning;
    if (lightning >= t.lightning_limit || holds_lightning || race_ticks_ < t.lightning_start_gate || lightning_cooldown_ > 0)
        blocked |= item_bit(Item::Lightning);
    if (ink >= t.ink_limit || race_ticks_ < t.ink_start_gate || ink_cooldown_ > 0) blocked |= item_bit(Item::Ink);
    return blocked;
}

void World::give_item(int kart, Item item) {
    KartState& k = karts_[size_t(kart)];
    k.item = item;
    k.item_uses = uint8_t(item_uses(item));
    k.trailing = false;
}

int World::kart_in_place(int place) const {
    const auto order = standings();
    return place >= 1 && size_t(place) <= order.size() ? order[size_t(place - 1)] : -1;
}

namespace {
void consume(KartState& k) {
    if (k.item_uses > 1) {
        --k.item_uses;
        return;
    }
    k.item = k.item_next;
    k.item_uses = uint8_t(item_uses(k.item_next));
    k.item_next = Item::None;
    k.trailing = false;
}
} // namespace

void World::items_before_step(std::vector<KartInput>& inputs) {
    const auto& t = item_tuning_;
    for (size_t i = 0; i < karts_.size(); ++i) {
        KartState& k = karts_[i];
        KartInput& in = inputs[i];
        const bool pressed = in.item && !k.item_button_prev;
        const bool released = !in.item && k.item_button_prev;
        k.item_button_prev = in.item;
        if (k.finished || k.respawn_frames > 0) continue;

        if (k.knocked()) {
            in = KartInput{}; // spinning or tumbling: no control
            continue;
        }
        if (k.bullet_frames > 0) {
            // The bullet flies itself along the road, fast, through everything.
            in = autopilot(k, track_, 12.0f);
            kart_give_boost(k, BoostKind::Bullet, 2, t.bullet_mult, true);
            continue;
        }
        if (k.star_frames > 0) kart_give_boost(k, BoostKind::Star, 2, t.star_mult, true);
        if (k.golden_frames > 0 && pressed) {
            kart_give_boost(k, BoostKind::Mushroom, tables_.constants.mushroom_frames, tables_.constants.mushroom_mult, true);
            emit(events_, KartEventType::Boost, int(i), int(BoostKind::Mushroom));
            continue;
        }
        if (k.item != Item::None) use_item(int(i), in, pressed, released);
    }
}

void World::use_item(int i, const KartInput& in, bool pressed, bool released) {
    KartState& k = karts_[size_t(i)];
    const auto& t = item_tuning_;
    const auto& c = tables_.constants;
    const bool backwards = in.item_back;
    const bool forwards = in.pitch < -0.5f; // stick pushed forward

    if (item_trails(k.item)) {
        if (k.trailing && released) {
            throw_item(i, k.item, backwards, forwards);
            emit(events_, KartEventType::ItemUse, i, int(k.item));
            consume(k);
        } else if (!k.trailing && pressed) {
            k.trailing = true; // hold to keep it behind you, let go to throw
        }
        return;
    }
    if (!pressed) return;

    const Item used = k.item;
    switch (used) {
    case Item::TripleBanana: throw_item(i, Item::Banana, backwards, forwards); break;
    case Item::TripleGreen: throw_item(i, Item::GreenShell, backwards, forwards); break;
    case Item::TripleRed: throw_item(i, Item::RedShell, backwards, forwards); break;
    case Item::SpikedShell:
        throw_item(i, Item::SpikedShell, false, true);
        spiked_cooldown_ = t.spiked_cooldown;
        break;
    case Item::Mushroom:
    case Item::TripleMushroom:
        kart_give_boost(k, BoostKind::Mushroom, c.mushroom_frames, c.mushroom_mult, true);
        emit(events_, KartEventType::Boost, i, int(BoostKind::Mushroom));
        break;
    case Item::GoldenMushroom:
        k.golden_frames = t.golden_frames;
        kart_give_boost(k, BoostKind::Mushroom, c.mushroom_frames, c.mushroom_mult, true);
        emit(events_, KartEventType::Boost, i, int(BoostKind::Mushroom));
        break;
    case Item::Bullet:
        k.bullet_frames = t.bullet_frames;
        k.drift = DriftState::None;
        k.mt_charge = 0.0f;
        break;
    case Item::Ink: {
        ink_cooldown_ = t.ink_cooldown;
        const int me = place_of(i);
        for (size_t j = 0; j < karts_.size(); ++j)
            if (int(j) != i && place_of(int(j)) < me && !karts_[j].untouchable()) karts_[j].ink_frames = t.ink_frames;
        break;
    }
    case Item::Lightning: {
        lightning_cooldown_ = t.lightning_cooldown;
        const int n = int(karts_.size());
        for (int j = 0; j < n; ++j) {
            if (j == i || karts_[size_t(j)].untouchable() || karts_[size_t(j)].finished) continue;
            apply_hit(j, HitKind::Shrink, i);
            // The leader stays small longest.
            const int place = place_of(j);
            karts_[size_t(j)].shrink_frames = t.shrink_frames_max * (n - place + 2) / (n + 1);
        }
        break;
    }
    case Item::Star:
        k.star_frames = t.star_frames;
        break;
    case Item::Fireball:
        if (k.fire_frames == 0) k.fire_frames = t.fire_frames;
        throw_item(i, Item::Fireball, backwards, true);
        break;
    case Item::Boomerang: throw_item(i, Item::Boomerang, backwards, true); break;
    case Item::Chomper:
        k.chomper_frames = t.chomper_frames;
        k.chomper_cooldown = 20;
        break;
    case Item::Horn: {
        for (size_t j = 0; j < karts_.size(); ++j) {
            if (int(j) == i || karts_[j].untouchable()) continue;
            if (glm::length(karts_[j].pos - k.pos) < t.horn_radius) apply_hit(int(j), HitKind::Tumble, i);
        }
        // The horn is the one thing that stops a spiked shell.
        std::erase_if(entities_, [&](const ItemEntity& e) {
            return e.type != EntityType::Explosion && glm::length(e.pos - k.pos) < t.horn_radius;
        });
        emit(events_, KartEventType::Explosion, i);
        break;
    }
    case Item::LuckyEight: {
        const Item next = kLuckyEight[k.lucky_index % 8];
        ++k.lucky_index;
        switch (next) {
        case Item::Mushroom:
            kart_give_boost(k, BoostKind::Mushroom, c.mushroom_frames, c.mushroom_mult, true);
            emit(events_, KartEventType::Boost, i, int(BoostKind::Mushroom));
            break;
        case Item::Ink: {
            ink_cooldown_ = t.ink_cooldown;
            const int me = place_of(i);
            for (size_t j = 0; j < karts_.size(); ++j)
                if (int(j) != i && place_of(int(j)) < me && !karts_[j].untouchable()) karts_[j].ink_frames = t.ink_frames;
            break;
        }
        case Item::Star: k.star_frames = t.star_frames; break;
        case Item::Coin: k.coins = std::min(k.coins + 2, c.max_coins); break;
        default: throw_item(i, next, backwards, forwards); break;
        }
        break;
    }
    case Item::Coin: k.coins = std::min(k.coins + 2, c.max_coins); break;
    case Item::Ghost: {
        k.ghost_frames = t.ghost_frames;
        // Steal from a random racer holding something, preferring those ahead.
        std::vector<int> ahead, any;
        const int me = place_of(i);
        for (size_t j = 0; j < karts_.size(); ++j) {
            if (int(j) == i || karts_[j].item == Item::None || karts_[j].roulette > 0) continue;
            any.push_back(int(j));
            if (place_of(int(j)) < me) ahead.push_back(int(j));
        }
        const auto& pool = ahead.empty() ? any : ahead;
        emit(events_, KartEventType::ItemUse, i, int(used));
        consume(k);
        if (!pool.empty()) {
            KartState& v = karts_[size_t(pool[size_t(rng_.range(int(pool.size())))])];
            const Item stolen = v.item;
            const uint8_t uses = v.item_uses;
            consume(v);
            v.item_uses = v.item != Item::None ? uint8_t(item_uses(v.item)) : 0;
            if (k.item == Item::None) {
                k.item = stolen;
                k.item_uses = uses;
            } else if (k.item_next == Item::None) {
                k.item_next = stolen;
            }
            emit(events_, KartEventType::ItemGet, i, int(stolen));
        }
        return;
    }
    default: break;
    }
    emit(events_, KartEventType::ItemUse, i, int(used));
    consume(k);
}

void World::throw_item(int i, Item item, bool backwards, bool forwards) {
    const KartState& k = karts_[size_t(i)];
    const auto& t = item_tuning_;
    ItemEntity e;
    e.owner = int8_t(i);
    e.sample = int16_t(k.sample);
    // Along the road under the kart: up a slope, round a steep bank on grip road.
    const vec3 up = k.grounded ? k.up() : track_.grip_up(k.sample);
    vec3 fwd = k.forward();
    fwd -= up * glm::dot(fwd, up);
    fwd = glm::length(fwd) > 0.1f ? glm::normalize(fwd) : vec3(0, 0, 1);
    const float kart_speed = std::max(0.0f, glm::dot(k.velocity(), fwd));
    const vec3 ahead = k.pos + fwd * 1.8f + up * 0.4f;
    const vec3 behind = k.pos - fwd * 1.8f + up * 0.4f;

    switch (item) {
    case Item::Banana:
        e.type = EntityType::Banana;
        if (forwards) {
            e.pos = ahead;
            e.vel = fwd * (kart_speed + 12.0f) + up * 8.0f;
        } else {
            e.pos = behind;
            e.vel = -fwd * 2.0f;
        }
        break;
    case Item::OilSlick: // poured out behind: a puddle that stays a while
        e.type = EntityType::OilSlick;
        e.pos = behind;
        e.vel = -fwd * 1.5f;
        e.life = 900;
        break;
    case Item::GreenShell:
    case Item::Fireball:
        e.type = item == Item::Fireball ? EntityType::Fireball : EntityType::GreenShell;
        e.pos = backwards ? behind : ahead;
        e.vel = (backwards ? -fwd : fwd) *
                (item == Item::Fireball ? std::max(t.fireball_speed, kart_speed + 8.0f) : std::max(t.green_speed, kart_speed + 10.0f));
        e.life = int16_t(item == Item::Fireball ? 150 : 600);
        break;
    case Item::RedShell:
        e.type = backwards ? EntityType::GreenShell : EntityType::RedShell;
        e.pos = backwards ? behind : ahead;
        e.vel = (backwards ? -fwd : fwd) * std::max(t.red_speed, kart_speed + 8.0f);
        e.life = int16_t(backwards ? 600 : 900);
        if (!backwards) {
            const int place = place_of(i);
            e.target = int8_t(place > 1 ? kart_in_place(place - 1) : -1);
        }
        break;
    case Item::SpikedShell:
        e.type = EntityType::SpikedShell;
        e.pos = k.pos + up * 3.0f;
        e.vel = fwd * t.spiked_speed;
        e.target = int8_t(kart_in_place(1));
        e.life = int16_t(t.spiked_life);
        break;
    case Item::Bomb:
        e.type = EntityType::Bomb;
        if (backwards) {
            e.pos = behind;
            e.vel = -fwd * 2.0f;
        } else {
            e.pos = ahead;
            e.vel = fwd * (kart_speed + 10.0f) + up * 8.0f;
        }
        break;
    case Item::Boomerang:
        e.type = EntityType::Boomerang;
        e.pos = (backwards ? behind : ahead) + up * 0.4f;
        e.vel = (backwards ? -fwd : fwd) * t.boomerang_speed;
        e.life = 240;
        break;
    default: return;
    }
    if (entities_.size() >= size_t(kMaxEntities)) {
        // Make room: the oldest banana goes first.
        auto it = std::find_if(entities_.begin(), entities_.end(), [](const ItemEntity& x) { return x.type == EntityType::Banana; });
        entities_.erase(it != entities_.end() ? it : entities_.begin());
    }
    entities_.push_back(e);
}

bool World::shield_blocks(int kart, vec3 from_pos, bool any_side) {
    KartState& k = karts_[size_t(kart)];
    const bool from_behind = glm::dot(from_pos - k.pos, k.forward()) < 0.0f;
    const bool orbiting = k.item == Item::TripleBanana || k.item == Item::TripleGreen || k.item == Item::TripleRed;
    if ((k.trailing && (from_behind || any_side)) || orbiting) {
        consume(k);
        k.trailing = false;
        emit(events_, KartEventType::Blocked, kart);
        return true;
    }
    return false;
}

void World::apply_hit(int kart, HitKind kind, int from) {
    KartState& k = karts_[size_t(kart)];
    const auto& t = item_tuning_;
    if (k.finished || k.respawn_frames > 0) return;
    if (kind != HitKind::Shrink && k.untouchable()) return;
    if (k.trailing) consume(k); // the held item is knocked away
    k.trailing = false;
    if (k.gliding || k.glide_armed) {
        // Knocked out of the air: the glider folds and the kart drops, onto
        // the road or off it. Flying on with no control would carry it off
        // the landing and leave it sinking under the track until the kill
        // height, a long dead glide.
        k.gliding = false;
        k.glide_armed = false;
        k.glide_bank = k.glide_pitch = 0.0f;
        emit(events_, KartEventType::GliderClose, kart);
    }
    k.drift = DriftState::None;
    k.mt_charge = 0.0f;
    k.boost_frames = 0;
    k.boost = BoostKind::None;
    k.boost_mult = 1.0f;
    k.coins = std::max(0, k.coins - t.coins_lost_on_hit);
    switch (kind) {
    case HitKind::Spin:
        k.spin_frames = t.spin_frames;
        k.invincible_frames = t.spin_frames + 20;
        break;
    case HitKind::Tumble:
        k.tumble_frames = t.tumble_frames;
        k.invincible_frames = t.tumble_frames + 20;
        k.speed *= 0.3f;
        break;
    case HitKind::Shrink:
        k.spin_frames = t.spin_frames;
        k.item = k.item_next = Item::None; // lightning makes you drop everything
        k.item_uses = 0;
        k.roulette = 0;
        break;
    }
    emit(events_, KartEventType::Hit, kart, int(kind) | ((from + 1) << 8));
}

bool World::over_oil(int kart) const {
    if (kart < 0 || size_t(kart) >= karts_.size()) return false;
    const KartState& k = karts_[size_t(kart)];
    if (!k.grounded) return false;
    for (const ItemEntity& e : entities_) {
        if (e.type != EntityType::OilSlick || !e.grounded) continue;
        const vec3 d = k.pos - e.pos;
        if (std::abs(d.y) < 1.5f && glm::length(vec2(d.x, d.z)) < 2.1f) return true;
    }
    return false;
}

void World::spawn_explosion(vec3 pos, int owner) {
    ItemEntity e;
    e.type = EntityType::Explosion;
    e.owner = int8_t(owner);
    e.pos = pos;
    e.life = 30;
    entities_.push_back(e);
    emit(events_, KartEventType::Explosion, owner);
}

void World::items_after_step() {
    const auto& t = item_tuning_;
    const int n = int(karts_.size());
    if (spiked_cooldown_ > 0) --spiked_cooldown_;
    if (lightning_cooldown_ > 0) --lightning_cooldown_;
    if (ink_cooldown_ > 0) --ink_cooldown_;
    for (int i = 0; i < n; ++i) {
        KartState& k = karts_[size_t(i)];
        const KartParams& p = params_[size_t(i)];
        for (int* f : {&k.star_frames, &k.bullet_frames, &k.ghost_frames, &k.ink_frames, &k.golden_frames,
                       &k.chomper_frames, &k.chomper_cooldown, &k.shrink_frames})
            if (*f > 0) --*f;
        // Spinning or tumbling sheds speed for a moment, then the kart slides
        // on with what's left. In the air it keeps shedding: a kart knocked
        // out of a glide drops near where it was hit instead of flying on.
        if (k.spin_frames > 0) {
            const bool shed = t.spin_frames - k.spin_frames < t.spin_shed_frames;
            --k.spin_frames;
            if (shed) k.speed *= 0.93f, k.slide *= 0.9f;
            if (!k.grounded) k.air_vel.x *= 0.93f, k.air_vel.z *= 0.93f;
        }
        if (k.tumble_frames > 0) {
            const bool shed = t.tumble_frames - k.tumble_frames < t.tumble_shed_frames;
            --k.tumble_frames;
            if (shed) k.speed *= 0.85f, k.slide *= 0.8f;
            if (!k.grounded) k.air_vel.x *= 0.85f, k.air_vel.z *= 0.85f;
        }
        if (k.shrink_frames > 0 && k.boost_frames == 0) k.speed = std::min(k.speed, p.top_speed * t.shrink_speed);
        if (k.fire_frames > 0 && --k.fire_frames == 0 && k.item == Item::Fireball) {
            k.item_uses = 1;
            consume(k);
        }

        // Roulette
        if (k.roulette > 0 && --k.roulette == 0) {
            const Item got = roll_item(rng_, place_of(i), n, blocked_items(i));
            if (k.item == Item::None) {
                k.item = got;
                k.item_uses = uint8_t(item_uses(got));
            } else {
                k.item_next = got;
            }
            emit(events_, KartEventType::ItemGet, i, int(got));
        }

        // Chomper: bites whatever is just ahead.
        if (k.chomper_frames > 0 && k.chomper_cooldown == 0 && !k.knocked()) {
            const vec3 f = k.forward();
            bool bit = false;
            for (int j = 0; j < n && !bit; ++j) {
                const KartState& o = karts_[size_t(j)];
                if (j == i || o.untouchable() || o.finished) continue;
                const vec3 d = o.pos - k.pos;
                if (glm::length(d) < 5.0f && glm::dot(glm::normalize(d), f) > 0.4f) {
                    apply_hit(j, HitKind::Spin, i);
                    bit = true;
                }
            }
            for (size_t e = 0; e < entities_.size() && !bit; ++e) {
                const vec3 d = entities_[e].pos - k.pos;
                if (solid(entities_[e].type) && glm::length(d) < 5.0f && glm::dot(glm::normalize(d), f) > 0.4f) {
                    entities_.erase(entities_.begin() + long(e));
                    bit = true;
                }
            }
            if (bit) {
                kart_give_boost(k, BoostKind::Mushroom, 25, 1.15f, true);
                k.chomper_cooldown = t.chomper_cooldown;
            }
        }
    }

    // Item boxes
    for (size_t b = 0; b < track_.item_boxes.size(); ++b) {
        if (box_respawn_[b] > 0) {
            --box_respawn_[b];
            continue;
        }
        for (int i = 0; i < n; ++i) {
            KartState& k = karts_[size_t(i)];
            if (k.respawn_frames > 0 || k.finished) continue;
            if (glm::length(k.pos + k.up() * 0.5f - track_.item_boxes[b]) > kBoxRadius) continue;
            box_respawn_[b] = t.box_respawn_frames;
            const int free = int(k.item == Item::None) + int(k.item_next == Item::None) - int(k.roulette > 0);
            // A golden mushroom window or a fire flower still in use blocks the slot.
            if (free > 0 && !(k.item == Item::None && (k.golden_frames > 0))) {
                k.roulette = t.roulette_frames;
                emit(events_, KartEventType::ItemBox, i);
            }
            break;
        }
    }

    update_entities();
}

void World::update_entities() {
    // Homing shells follow the road to their target: onto the branch it's on.
    auto shell_route = [&](const KartState& target) {
        const int b = track_.sample(target.sample).branch;
        return b >= 0 && b < 32 ? 1u << b : 0u;
    };
    const auto& t = item_tuning_;
    const float g = tables_.constants.gravity;
    const int nk = int(karts_.size());
    std::vector<bool> dead(entities_.size(), false);
    std::vector<WallContact> contacts;
    std::vector<ItemEntity> spawned;

    auto explode = [&](size_t idx) {
        dead[idx] = true;
        ItemEntity e;
        e.type = EntityType::Explosion;
        e.owner = entities_[idx].owner;
        e.pos = entities_[idx].pos;
        e.life = 30;
        spawned.push_back(e);
        emit(events_, KartEventType::Explosion, entities_[idx].owner);
    };

    for (size_t idx = 0; idx < entities_.size(); ++idx) {
        ItemEntity& e = entities_[idx];
        ++e.age;
        if (e.life > 0 && --e.life == 0) {
            if (e.type == EntityType::Bomb) explode(idx);
            else dead[idx] = true;
            continue;
        }

        switch (e.type) {
        case EntityType::Explosion:
            for (int j = 0; j < nk; ++j)
                if (glm::length(karts_[size_t(j)].pos - e.pos) < t.explosion_radius)
                    apply_hit(j, HitKind::Tumble, e.owner);
            continue;
        case EntityType::SpikedShell: {
            // Flies above the road along the centre line, then drops on the leader.
            e.target = int8_t(kart_in_place(1));
            if (e.target < 0) {
                dead[idx] = true;
                continue;
            }
            const KartState& tk = karts_[size_t(e.target)];
            const vec3 to_target = tk.pos - e.pos;
            vec3 dest;
            if (glm::length(to_target) < 14.0f) {
                dest = tk.pos + tk.up() * 0.5f;
            } else {
                // Advance along the centre line towards the leader's sample.
                const uint32_t route = shell_route(tk);
                for (int step = 0; step < 6 && e.sample != tk.sample; ++step) {
                    const vec3 c = track_.sample(e.sample).pos + track_.sample(e.sample).up * 3.0f;
                    if (glm::length(c - e.pos) > 4.0f) break;
                    e.sample = int16_t(track_.next(e.sample, route));
                }
                dest = track_.sample(e.sample).pos + track_.sample(e.sample).up * 3.0f;
            }
            const vec3 d = dest - e.pos;
            const float dist = glm::length(d);
            const float stepd = t.spiked_speed * kDt;
            e.vel = dist > 1e-3f ? d / dist * t.spiked_speed : vec3(0.0f);
            e.pos = dist <= stepd ? dest : e.pos + d / dist * stepd;
            if (glm::length(tk.pos - e.pos) < 1.5f) explode(idx);
            continue;
        }
        case EntityType::RedShell:
            if (e.target >= 0 && e.target < nk) {
                const KartState& tk = karts_[size_t(e.target)];
                // Steers in the plane of the road under it, so a slope or a
                // bank costs it neither aim nor speed.
                const vec3 ru = track_.sample(e.sample).up;
                const float speed = std::max(t.red_speed, glm::length(e.vel - ru * glm::dot(e.vel, ru)));
                vec3 dest;
                if (glm::length(tk.pos - e.pos) < 22.0f) {
                    dest = tk.pos;
                } else {
                    const uint32_t route = shell_route(tk);
                    for (int step = 0; step < 6; ++step) {
                        const vec3 c = track_.sample(e.sample).pos;
                        const vec3 off = c - e.pos;
                        if (glm::length(off - ru * glm::dot(off, ru)) > 5.0f) break;
                        e.sample = int16_t(track_.next(e.sample, route));
                    }
                    dest = track_.sample(e.sample).pos;
                }
                vec3 d = dest - e.pos;
                d -= ru * glm::dot(d, ru);
                if (glm::length(d) > 1e-3f) e.vel = glm::normalize(d) * speed + ru * glm::dot(e.vel, ru);
            }
            break;
        case EntityType::Boomerang:
            if (!e.returning && e.age > 40) e.returning = true;
            if (e.returning && e.owner >= 0) {
                const vec3 d = karts_[size_t(e.owner)].pos + kUp * 0.6f - e.pos;
                if (glm::length(d) < 1.5f) {
                    dead[idx] = true;
                    continue;
                }
                e.vel = glm::normalize(d) * t.boomerang_speed;
            }
            e.pos += e.vel * kDt; // flies, no ground contact
            break;
        default: break;
        }

        if (e.type != EntityType::Boomerang) {
            // Ground: shells and fireballs skim along it, bananas and bombs
            // land and stay. On grip road "down" is into the road. Resting and
            // riding go by the road's own normal, so a slope or a bank holds
            // an item as it holds a kart.
            if (e.type != EntityType::RedShell && e.type != EntityType::SpikedShell) // those keep their progress in `sample`
                e.sample = int16_t(track_.nearest_sample(e.pos, e.sample));
            const vec3 up = track_.grip_up(e.sample);
            const auto hit = track_.raycast(e.pos + up * 1.0f, -up, 3.0f);
            const vec3 n = hit ? hit->normal : up;
            const bool on_ground = hit && glm::dot(n, up) > 0.5f && glm::dot(e.pos - hit->point, n) < 0.5f && glm::dot(e.vel, n) <= 0.5f;
            if (on_ground) {
                e.pos -= n * (glm::dot(e.pos - hit->point, n) - kEntityRadius); // out along the normal: no creep down a slope
                // Riding along: over a change of slope it keeps its speed.
                const float was = glm::length(e.vel);
                e.vel -= n * glm::dot(e.vel, n);
                if (e.grounded && glm::length(e.vel) > 1e-3f) e.vel *= was / glm::length(e.vel);
                if (!e.grounded && e.type == EntityType::Bomb && e.life == 0) e.life = int16_t(t.bomb_fuse);
                e.grounded = true;
                if (e.type == EntityType::Banana || e.type == EntityType::Bomb || e.type == EntityType::OilSlick) e.vel = vec3(0.0f);
            } else {
                e.grounded = false;
                e.vel -= up * (g * kDt);
            }
            e.pos += e.vel * kDt;
            if (e.pos.y < track_.kill_y || (hit && hit->surface == Surface::OutOfBounds && on_ground)) {
                dead[idx] = true;
                continue;
            }
            // Walls: shells bounce, fireballs fizzle.
            contacts.clear();
            track_.collide_sphere(e.pos, kEntityRadius, contacts);
            for (const WallContact& w : contacts) {
                if (std::abs(glm::dot(w.normal, up)) > 0.5f) continue;
                e.pos += w.normal * w.depth;
                const float into = glm::dot(e.vel, w.normal);
                if (into < 0.0f) {
                    e.vel -= 2.0f * into * w.normal;
                    if (++e.bounces > 8 || e.type == EntityType::Fireball) dead[idx] = true;
                }
            }
            if (dead[idx]) continue;
        }

        // Karts
        for (int j = 0; j < nk; ++j) {
            KartState& k = karts_[size_t(j)];
            if (j == e.owner && (e.age < 25 || e.type == EntityType::Boomerang)) continue;
            if (k.respawn_frames > 0 || k.ghost_frames > 0 || k.finished) continue;
            if (e.type == EntityType::RedShell && e.target >= 0 && j != e.target && e.age < 25) continue;
            if (glm::length(k.pos + k.up() * 0.4f - e.pos) > (e.type == EntityType::OilSlick ? 2.0f : kHitRadius)) continue;
            if (e.type == EntityType::OilSlick) continue; // the puddle stays; it makes the road slippery (over_oil)
            if (k.star_frames > 0 || k.bullet_frames > 0) {
                dead[idx] = e.type != EntityType::Boomerang;
                break;
            }
            if (k.invincible_frames > 0) continue;
            if (e.type == EntityType::Bomb) {
                explode(idx);
                break;
            }
            if (shield_blocks(j, e.pos, false)) {
                dead[idx] = true;
                break;
            }
            const HitKind kind = e.type == EntityType::Banana || e.type == EntityType::Fireball ? HitKind::Spin : HitKind::Tumble;
            apply_hit(j, kind, e.owner);
            if (e.type != EntityType::Boomerang) {
                dead[idx] = true;
                break;
            }
        }
    }

    // Items knock each other out.
    for (size_t a = 0; a < entities_.size(); ++a) {
        if (dead[a] || !solid(entities_[a].type)) continue;
        for (size_t b = a + 1; b < entities_.size(); ++b) {
            if (dead[b] || !solid(entities_[b].type)) continue;
            if (glm::length(entities_[a].pos - entities_[b].pos) > 0.9f) continue;
            if (entities_[a].type == EntityType::Bomb) explode(a);
            else dead[a] = true;
            if (entities_[b].type == EntityType::Bomb) explode(b);
            else dead[b] = true;
            break;
        }
    }

    size_t w = 0;
    for (size_t r = 0; r < entities_.size(); ++r)
        if (!dead[r]) entities_[w++] = entities_[r];
    entities_.resize(w);
    for (const ItemEntity& e : spawned)
        if (entities_.size() < size_t(kMaxEntities)) entities_.push_back(e);
}

} // namespace gk::sim
