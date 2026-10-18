#pragma once
#include "gk/core/math.h"

#include <array>
#include <cstdint>

// Items. The 22 race items of MK8 Deluxe, under generic names: the mechanics
// follow the game, the names and looks are our own.
namespace gk::sim {

enum class Item : uint8_t {
    None,
    Banana,
    TripleBanana,
    GreenShell,
    TripleGreen,
    RedShell,
    TripleRed,
    SpikedShell,    // seeks the leader
    Bomb,
    Mushroom,
    TripleMushroom,
    GoldenMushroom,
    Bullet,         // autopilot rocket
    Ink,            // squirts ink at everyone ahead
    Lightning,
    Star,
    Fireball,
    Boomerang,
    Chomper,        // plant that lunges at whatever is ahead
    Horn,           // shockwave
    LuckyEight,
    Coin,
    Ghost,          // turn invisible and steal an item
    OilSlick,       // dropped behind: whoever drives through it spins out
    Count
};
inline constexpr int kItemCount = int(Item::Count) - 1; // 22 race items

const char* item_name(Item item);
// Items used one at a time from a stack (triples, Lucky Eight, Boomerang, Fireball).
int item_uses(Item item);
// Items that can be held behind the kart as a shield.
bool item_trails(Item item);

// Things on the track: thrown and dropped items, and explosions.
enum class EntityType : uint8_t { Banana, GreenShell, RedShell, SpikedShell, Bomb, Fireball, Boomerang, Explosion, OilSlick };

struct ItemEntity {
    EntityType type = EntityType::Banana;
    int8_t owner = -1;     // kart that threw it
    int8_t target = -1;    // kart it seeks (red and spiked shells)
    uint8_t bounces = 0;
    bool grounded = false;
    bool returning = false;// boomerang on its way back
    vec3 pos{0.0f};
    vec3 vel{0.0f};
    int16_t sample = 0;    // centre-line progress (red and spiked shells)
    int16_t age = 0;       // ticks since spawned
    int16_t life = 0;      // ticks left; 0 = forever
};

// How a kart reacts to a hit.
enum class HitKind : uint8_t { Spin, Tumble, Shrink };

// Item timings in 60 Hz frames. Provisional until measured from captures.
struct ItemTuning {
    int roulette_frames = 150;
    int box_respawn_frames = 150;
    int spin_frames = 70;
    int tumble_frames = 95;
    int star_frames = 450;
    int bullet_frames = 330;
    int golden_frames = 450;
    int ghost_frames = 210;
    int ink_frames = 240;
    int shrink_frames_max = 480;   // for the kart in 1st; less further back
    int fire_frames = 600;
    int chomper_frames = 480;
    int chomper_cooldown = 45;
    int bomb_fuse = 150;
    float green_speed = 38.0f;     // m/s
    float red_speed = 36.0f;
    float spiked_speed = 55.0f;
    float fireball_speed = 30.0f;
    float boomerang_speed = 34.0f;
    float star_mult = 1.15f;
    float bullet_mult = 1.55f;
    float shrink_speed = 0.8f;
    float explosion_radius = 6.5f;
    float horn_radius = 9.0f;
    int coins_lost_on_hit = 3;
    // Hidden limits on the strongest items, in frames. Same idea as MK8
    // Deluxe's measured rules (one spiked shell in play, a few lightning bolts,
    // none in the first 30 s or the 30 s after one is used); our own numbers.
    int spiked_limit = 1;          // in slots and in flight together
    int spiked_start_gate = 1800;  // none before this race tick
    int spiked_cooldown = 1800;    // none for this long after one is thrown
    int spiked_life = 1800;        // a shell in flight gives up after this
    int lightning_limit = 2;       // held, across all karts
    int lightning_start_gate = 1800;
    int lightning_cooldown = 1800; // after one strikes
};

// Pseudo-random numbers stored in the world state so that prediction and
// replays draw the same items as the server.
struct Rng {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    uint32_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return uint32_t((z ^ (z >> 31)) >> 32);
    }
    int range(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }
};

// The roulette: odds by place, as a share of the pack (MK8 Deluxe weighs by
// distance to the leader; place bands are the provisional stand-in).
// `blocked` is a mask of items (bit = 1 << Item) taken out of the odds; the
// draw is always a single one, and if nothing is left the kart gets a
// Mushroom (a Coin in 1st).
Item roll_item(Rng& rng, int place, int karts, uint32_t blocked = 0);
inline constexpr uint32_t item_bit(Item item) { return 1u << unsigned(item); }

} // namespace gk::sim
