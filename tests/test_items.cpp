// Items: roulette odds, boxes, throwing, hits, shields and the special items,
// on a flat straight so each case is easy to set up.
#include "sim_fixture.h"

#include "gk/net/protocol.h"

#include <algorithm>
#include <map>

using namespace gk;
using namespace gk::sim;

namespace {

struct ItemBench {
    World world;
    std::vector<KartInput> inputs;

    explicit ItemBench(int karts, std::vector<vec3> boxes = {})
        : world(make_track(std::move(boxes)), test::tables(), RaceConfig{.laps = 1 << 30, .countdown_frames = 1}) {
        // (The flat straight has one checkpoint, so every tick counts as a lap.)
        const auto parts = test::parts();
        for (int i = 0; i < karts; ++i) world.add_kart(test::default_params(world.tables(), parts));
        inputs.assign(size_t(karts), KartInput{});
        world.tick(inputs); // countdown over
        // Line the karts up along the straight, 20 m apart, kart 0 in front.
        for (int i = 0; i < karts; ++i) place(i, 300.0f - 20.0f * float(i));
    }

    static Track make_track(std::vector<vec3> boxes) {
        Track t = test::flat_track();
        t.item_boxes = std::move(boxes);
        return t;
    }

    KartState& kart(int i) { return world.karts_mut()[size_t(i)]; }
    void place(int i, float z, float x = 0.0f) {
        KartState& k = kart(i);
        k.pos = {x, 0.0f, z};
        k.rot = quat(1, 0, 0, 0);
        k.speed = 0.0f;
        k.sample = world.track().nearest_sample(k.pos);
    }
    std::vector<KartEvent> run(int ticks) {
        std::vector<KartEvent> all;
        for (int t = 0; t < ticks; ++t) {
            world.tick(inputs);
            all.insert(all.end(), world.events().begin(), world.events().end());
        }
        return all;
    }
    // Press the item button for one tick, then release.
    std::vector<KartEvent> tap(int kart_index, KartInput extra = {}) {
        KartInput& in = inputs[size_t(kart_index)];
        const KartInput keep = in;
        in = extra;
        in.item = true;
        auto ev = run(1);
        in = keep;
        auto more = run(1);
        ev.insert(ev.end(), more.begin(), more.end());
        return ev;
    }
    static bool has(const std::vector<KartEvent>& ev, KartEventType t, int kart) {
        return std::any_of(ev.begin(), ev.end(), [&](const KartEvent& e) { return e.type == t && e.kart == kart; });
    }
};

} // namespace

TEST_CASE("roulette odds depend on place") {
    Rng rng;
    std::map<Item, int> first, last;
    for (int i = 0; i < 4000; ++i) {
        ++first[roll_item(rng, 1, 12)];
        ++last[roll_item(rng, 12, 12)];
    }
    CHECK(first[Item::Bullet] == 0);
    CHECK(first[Item::SpikedShell] == 0);
    CHECK(first[Item::Star] == 0);
    CHECK(first[Item::Banana] + first[Item::GreenShell] + first[Item::Coin] > 2000);
    CHECK(last[Item::Bullet] > 100);
    CHECK(last[Item::Banana] == 0);
    CHECK(last[Item::Coin] == 0);
    int kinds = 0;
    for (int i = 1; i <= kItemCount; ++i) {
        Rng r2;
        bool seen = false;
        for (int n = 0; n < 4000 && !seen; ++n)
            for (int place = 1; place <= 12 && !seen; ++place) seen = roll_item(r2, place, 12) == Item(i);
        kinds += seen;
    }
    CHECK(kinds == kItemCount); // every one of the 22 items can come out
}

TEST_CASE("driving through a box gives an item after the roulette") {
    ItemBench b(1, {vec3(0.0f, 1.0f, 310.0f)});
    b.inputs[0].accelerate = true;
    auto ev = b.run(90);
    REQUIRE(ItemBench::has(ev, KartEventType::ItemBox, 0));
    CHECK(b.world.box_respawn()[0] > 0);
    CHECK(b.kart(0).roulette > 0);
    ev = b.run(b.world.item_tuning().roulette_frames);
    CHECK(ItemBench::has(ev, KartEventType::ItemGet, 0));
    CHECK(b.kart(0).item != Item::None);
    CHECK(b.kart(0).item_uses >= 1);
}

TEST_CASE("a green shell knocks over the kart ahead") {
    ItemBench b(2);
    b.place(0, 330.0f);
    b.place(1, 300.0f);
    b.world.give_item(1, Item::GreenShell);
    b.tap(1);
    CHECK(b.kart(1).item == Item::None);
    REQUIRE(b.world.entities().size() == 1);
    CHECK(b.world.entities()[0].type == EntityType::GreenShell);
    auto ev = b.run(90);
    CHECK(ItemBench::has(ev, KartEventType::Hit, 0));
    CHECK(b.world.entities().empty());
    CHECK(b.kart(0).invincible_frames > 0);
}

TEST_CASE("a banana spins out the kart behind, and a held banana blocks a shell") {
    ItemBench b(2);
    b.place(0, 330.0f);
    b.place(1, 300.0f);
    b.world.give_item(0, Item::Banana);
    b.tap(0); // dropped behind
    REQUIRE(b.world.entities().size() == 1);
    CHECK(b.world.entities()[0].pos.z < 330.0f);
    b.inputs[1].accelerate = true;
    auto ev = b.run(240);
    CHECK(ItemBench::has(ev, KartEventType::Hit, 1));
    CHECK(b.world.entities().empty());

    // Shield: kart 0 holds a banana behind it; a shell from behind is absorbed.
    ItemBench s(2);
    s.place(0, 330.0f);
    s.place(1, 300.0f);
    s.world.give_item(0, Item::Banana);
    s.inputs[0].item = true; // hold
    s.run(2);
    CHECK(s.kart(0).trailing);
    s.world.give_item(1, Item::GreenShell);
    s.tap(1);
    ev = s.run(90);
    CHECK(ItemBench::has(ev, KartEventType::Blocked, 0));
    CHECK_FALSE(ItemBench::has(ev, KartEventType::Hit, 0));
    CHECK(s.kart(0).item == Item::None);
}

TEST_CASE("a red shell homes in on the kart in the place ahead") {
    ItemBench b(3);
    b.place(0, 380.0f, 4.0f);   // 1st, off to the side
    b.place(1, 340.0f, -5.0f);  // 2nd: the target
    b.place(2, 300.0f);         // 3rd: thrower
    b.world.give_item(2, Item::RedShell);
    b.tap(2);
    REQUIRE(b.world.entities().size() == 1);
    CHECK(b.world.entities()[0].target == 1);
    auto ev = b.run(120);
    CHECK(ItemBench::has(ev, KartEventType::Hit, 1));
    CHECK_FALSE(ItemBench::has(ev, KartEventType::Hit, 0));
}

TEST_CASE("mushrooms boost, the golden one repeatedly") {
    ItemBench b(1);
    b.inputs[0].accelerate = true;
    b.run(120);
    const float cruise = b.kart(0).speed;
    b.world.give_item(0, Item::TripleMushroom);
    b.tap(0, {.accelerate = true});
    CHECK(b.kart(0).boost == BoostKind::Mushroom);
    CHECK(b.kart(0).item == Item::TripleMushroom);
    CHECK(b.kart(0).item_uses == 2);
    b.run(30);
    CHECK(b.kart(0).speed > cruise * 1.1f);

    b.world.give_item(0, Item::GoldenMushroom);
    b.tap(0, {.accelerate = true});
    CHECK(b.kart(0).item == Item::None);
    CHECK(b.kart(0).golden_frames > 0);
    b.run(100);
    const auto ev = b.tap(0, {.accelerate = true});
    CHECK(ItemBench::has(ev, KartEventType::Boost, 0));
}

TEST_CASE("a star protects and knocks others over") {
    ItemBench b(2);
    b.place(0, 300.0f);
    b.place(1, 304.0f);
    b.world.give_item(0, Item::Star);
    b.tap(0);
    CHECK(b.kart(0).star_frames > 0);
    CHECK(b.kart(0).untouchable());
    b.inputs[0].accelerate = true;
    auto ev = b.run(60);
    CHECK(ItemBench::has(ev, KartEventType::Hit, 1));
    CHECK_FALSE(ItemBench::has(ev, KartEventType::Hit, 0));
}

TEST_CASE("a hit kart keeps some of its speed and slides on along its heading") {
    struct Case { HitKind kind; int frames; float keep; };
    const ItemTuning tuning;
    for (const Case c : {Case{HitKind::Spin, tuning.spin_frames, 0.35f}, Case{HitKind::Tumble, tuning.tumble_frames, 0.2f}}) {
        CAPTURE(int(c.kind));
        ItemBench b(1);
        b.kart(0).speed = 20.0f;
        const vec3 start = b.kart(0).pos, heading = b.kart(0).forward();
        b.world.apply_hit(0, c.kind, -1);
        b.run(c.frames / 2);
        REQUIRE(b.kart(0).knocked());
        CHECK(b.kart(0).speed == doctest::Approx(20.0f * c.keep).epsilon(0.15));
        b.run(c.frames / 2 - 1);
        REQUIRE(b.kart(0).knocked());
        CHECK(b.kart(0).speed == doctest::Approx(20.0f * c.keep).epsilon(0.15)); // still sliding, not rolling to a stop
        const vec3 moved = b.kart(0).pos - start;
        CHECK(glm::dot(moved, heading) > 20.0f * c.keep * float(c.frames) / 60.0f * 0.7f);
        CHECK(std::abs(glm::dot(moved, b.kart(0).right())) < 0.5f);
    }
}

TEST_CASE("lightning shrinks everyone else and takes their items") {
    ItemBench b(4);
    b.world.give_item(0, Item::Mushroom);
    b.world.give_item(1, Item::RedShell);
    b.world.give_item(3, Item::Lightning);
    b.tap(3);
    for (int i = 0; i < 3; ++i) {
        CHECK(b.kart(i).shrink_frames > 0);
        CHECK(b.kart(i).item == Item::None);
    }
    CHECK(b.kart(3).shrink_frames == 0);
    CHECK(b.kart(0).shrink_frames > b.kart(2).shrink_frames); // the leader stays small longest
}

TEST_CASE("a hit in the air folds the glider and the kart drops") {
    World w = test::racing_world();
    int frames = 0;
    while (!w.karts()[0].gliding && frames < 60 * 60) {
        KartInput in = autopilot(w.karts()[0], w.track());
        w.tick(std::span(&in, 1));
        ++frames;
    }
    REQUIRE(w.karts()[0].gliding);
    w.apply_hit(0, HitKind::Spin, -1);
    CHECK_FALSE(w.karts()[0].gliding);
    CHECK_FALSE(w.karts()[0].glide_armed);
    CHECK(ItemBench::has(w.events(), KartEventType::GliderClose, 0));
    // Falling, not flying: back on the road or respawned well inside the time a glide takes.
    int down = 0;
    for (; down < 60 * 8 && !w.karts()[0].grounded && w.karts()[0].respawn_frames == 0; ++down) {
        KartInput in = autopilot(w.karts()[0], w.track());
        w.tick(std::span(&in, 1));
    }
    CHECK(down < 60 * 8);
    CHECK_FALSE(w.karts()[0].gliding);
}

TEST_CASE("the spiked shell finds the leader and the horn stops it") {
    ItemBench b(3);
    b.place(0, 500.0f);
    b.place(1, 330.0f);
    b.place(2, 300.0f);
    b.world.give_item(2, Item::SpikedShell);
    b.tap(2);
    REQUIRE(b.world.entities().size() == 1);
    auto ev = b.run(400);
    CHECK(ItemBench::has(ev, KartEventType::Hit, 0));
    CHECK_FALSE(ItemBench::has(ev, KartEventType::Hit, 1));

    ItemBench h(2);
    h.place(0, 600.0f);
    h.place(1, 300.0f);
    h.world.give_item(1, Item::SpikedShell);
    h.tap(1);
    h.run(20);
    // Leader honks when the shell is close.
    h.world.give_item(0, Item::Horn);
    for (int t = 0; t < 400 && !h.world.entities().empty(); ++t) {
        if (glm::length(h.world.entities()[0].pos - h.kart(0).pos) < 8.0f) {
            h.tap(0);
            break;
        }
        h.run(1);
    }
    ev = h.run(60);
    CHECK(h.world.entities().empty());
    CHECK_FALSE(ItemBench::has(ev, KartEventType::Hit, 0));
}

TEST_CASE("a ghost turns intangible and steals an item") {
    ItemBench b(2);
    b.world.give_item(0, Item::Star);
    b.world.give_item(1, Item::Ghost);
    b.tap(1);
    CHECK(b.kart(1).ghost_frames > 0);
    CHECK(b.kart(1).item == Item::Star);
    CHECK(b.kart(0).item == Item::None);
}

TEST_CASE("item state rewinds exactly") {
    ItemBench b(3, {vec3(0, 1, 320), vec3(3, 1, 360)});
    b.inputs = {{.accelerate = true}, {.accelerate = true, .item = true}, {.accelerate = true}};
    b.world.give_item(1, Item::TripleGreen);
    b.world.give_item(2, Item::Bomb);
    b.run(30);
    const WorldState saved = b.world.save();
    b.run(200);
    const WorldState a = b.world.save();
    b.world.restore(saved);
    b.run(200);
    const WorldState c = b.world.save();
    REQUIRE(a.karts.size() == c.karts.size());
    for (size_t i = 0; i < a.karts.size(); ++i) CHECK(glm::length(a.karts[i].pos - c.karts[i].pos) == 0.0f);
    CHECK(a.entities.size() == c.entities.size());
    CHECK(a.rng.state == c.rng.state);
    CHECK(a.box_respawn == c.box_respawn);
}

// ---- hidden limits on the spiked shell and lightning

namespace {

// Four karts on the straight; the back three spin the roulette every tick so
// each tick is one draw for each of them.
struct LimitBench : ItemBench {
    LimitBench() : ItemBench(4) {}
    void set_race_ticks(int ticks) {
        WorldState s = world.save();
        s.race_ticks = ticks;
        world.restore(s);
    }
    // One roulette draw for each of karts 1..3; returns what they got.
    std::vector<Item> draw() {
        for (int i = 1; i < 4; ++i) {
            KartState& k = kart(i);
            if (k.item != Item::SpikedShell && k.item != Item::Lightning) k.item = Item::None;
            k.item_next = Item::None;
            k.roulette = 1;
        }
        run(1);
        std::vector<Item> got;
        for (int i = 1; i < 4; ++i) got.push_back(kart(i).item_next != Item::None ? kart(i).item_next : kart(i).item);
        // Keep the slots free of the limited items for the next draw unless a
        // test put them there on purpose.
        for (int i = 1; i < 4; ++i) {
            KartState& k = kart(i);
            if (k.item_next != Item::None) k.item_next = Item::None;
            else k.item = Item::None;
        }
        return got;
    }
    static bool any(const std::vector<Item>& v, Item item) { return std::find(v.begin(), v.end(), item) != v.end(); }
};

} // namespace

TEST_CASE("no spiked shell or lightning before frame 1800") {
    LimitBench b;
    b.set_race_ticks(0);
    const int gate = b.world.item_tuning().spiked_start_gate;
    REQUIRE(gate == 1800);
    REQUIRE(b.world.item_tuning().lightning_start_gate == 1800);
    while (b.world.race_ticks() < gate) {
        const auto got = b.draw();
        CHECK_FALSE(LimitBench::any(got, Item::SpikedShell));
        CHECK_FALSE(LimitBench::any(got, Item::Lightning));
    }
    // After the gate both turn up again.
    bool spiked = false, lightning = false;
    for (int n = 0; n < 2000 && !(spiked && lightning); ++n) {
        const auto got = b.draw();
        spiked = spiked || LimitBench::any(got, Item::SpikedShell);
        lightning = lightning || LimitBench::any(got, Item::Lightning);
    }
    CHECK(spiked);
    CHECK(lightning);
}

TEST_CASE("no second spiked shell while one is in a slot or in flight") {
    LimitBench b;
    b.set_race_ticks(4000);
    // Held in the reserve slot of a kart that isn't drawing.
    b.kart(0).item = Item::Coin;
    b.kart(0).item_next = Item::SpikedShell;
    CHECK((b.world.blocked_items(2) & item_bit(Item::SpikedShell)) != 0);
    for (int n = 0; n < 1500; ++n) CHECK_FALSE(LimitBench::any(b.draw(), Item::SpikedShell));
    // Thrown by kart 1: now it's in flight.
    b.kart(0).item_next = Item::None;
    b.kart(1).item = Item::SpikedShell;
    b.kart(1).item_uses = 1;
    b.tap(1);
    REQUIRE(std::any_of(b.world.entities().begin(), b.world.entities().end(),
                        [](const ItemEntity& e) { return e.type == EntityType::SpikedShell; }));
    CHECK(b.world.spiked_cooldown() > 0);
    // Even with the cooldown cleared, the shell in flight keeps the slot taken.
    WorldState s = b.world.save();
    s.spiked_cooldown = 0;
    b.world.restore(s);
    CHECK((b.world.blocked_items(2) & item_bit(Item::SpikedShell)) != 0);
    // In flight for at most spiked_life frames.
    for (const ItemEntity& e : b.world.entities())
        if (e.type == EntityType::SpikedShell) CHECK(e.life <= b.world.item_tuning().spiked_life);
    // Gone: free again.
    b.world.entities_mut().clear();
    CHECK((b.world.blocked_items(2) & item_bit(Item::SpikedShell)) == 0);
}

TEST_CASE("no spiked shell within 1800 frames of a throw") {
    LimitBench b;
    b.set_race_ticks(4000);
    b.kart(1).item = Item::SpikedShell;
    b.kart(1).item_uses = 1;
    b.tap(1);
    REQUIRE(b.kart(1).item != Item::SpikedShell);
    // tap() ran two ticks: the throw was on the first of them.
    const int thrown = b.world.race_ticks() - 2;
    b.world.entities_mut().clear(); // only the cooldown is left blocking it
    while (b.world.race_ticks() < thrown + 1800 - 1) {
        CHECK_FALSE(LimitBench::any(b.draw(), Item::SpikedShell));
        b.world.entities_mut().clear();
    }
    bool seen = false;
    for (int n = 0; n < 3000 && !seen; ++n) {
        seen = LimitBench::any(b.draw(), Item::SpikedShell);
        b.world.entities_mut().clear();
    }
    CHECK(seen);
}

TEST_CASE("lightning: two held at most, one per kart, cooldown after a strike") {
    LimitBench b;
    b.set_race_ticks(4000);
    b.kart(1).item = Item::Lightning;
    // Kart 1 holds one: it can't draw a second into its reserve slot.
    CHECK((b.world.blocked_items(1) & item_bit(Item::Lightning)) != 0);
    CHECK((b.world.blocked_items(2) & item_bit(Item::Lightning)) == 0);
    b.kart(0).item = Item::Lightning;
    // Two held: nobody else gets one.
    CHECK((b.world.blocked_items(2) & item_bit(Item::Lightning)) != 0);
    CHECK((b.world.blocked_items(3) & item_bit(Item::Lightning)) != 0);
    b.kart(0).item = Item::None;
    // Kart 1 strikes: 1800 frames with none.
    b.tap(1);
    CHECK(b.world.lightning_cooldown() > 1790);
    CHECK((b.world.blocked_items(2) & item_bit(Item::Lightning)) != 0);
}

TEST_CASE("a blocked item falls back without a second draw") {
    // Everything blocked: a fixed fallback, and the generator moved on by one draw at most.
    Rng a, c;
    CHECK(roll_item(a, 1, 12, ~0u) == Item::Coin);
    CHECK(roll_item(a, 12, 12, ~0u) == Item::Mushroom);
    CHECK(a.state == c.state);
    // One item blocked: one draw, same as unblocked.
    Rng d, e;
    roll_item(d, 12, 12, item_bit(Item::SpikedShell));
    roll_item(e, 12, 12);
    CHECK(d.state == e.state);
}

TEST_CASE("two worlds on the same inputs draw the same items, through a snapshot") {
    auto run_race = [](LimitBench& b, int from, int to, std::vector<Item>& out) {
        for (int t = from; t < to; ++t) {
            if (t == 400) { // someone throws a shell early on, so the cooldown is running at the snapshot
                b.kart(1).item = Item::SpikedShell;
                b.kart(1).item_uses = 1;
                b.tap(1);
            }
            const auto got = b.draw();
            out.insert(out.end(), got.begin(), got.end());
        }
    };
    LimitBench a, c;
    a.set_race_ticks(1700);
    c.set_race_ticks(1700);
    std::vector<Item> seq_a, seq_c;
    run_race(a, 0, 500, seq_a);
    run_race(c, 0, 500, seq_c);
    REQUIRE(seq_a == seq_c);
    REQUIRE(a.world.spiked_cooldown() > 0);
    // Carry world c's state over the wire into a third world and go on from there.
    LimitBench d;
    net::SnapshotMsg m;
    const WorldState ws = c.world.save();
    m.race_ticks = uint32_t(ws.race_ticks);
    m.spiked_cooldown = uint16_t(ws.spiked_cooldown);
    m.lightning_cooldown = uint16_t(ws.lightning_cooldown);
    net::SnapshotMsg back;
    REQUIRE(net::decode(net::encode(m), back));
    WorldState ds = ws;
    ds.spiked_cooldown = back.spiked_cooldown;
    ds.lightning_cooldown = back.lightning_cooldown;
    d.world.restore(ds);
    CHECK(d.world.spiked_cooldown() == a.world.spiked_cooldown());
    std::vector<Item> more_a, more_d;
    run_race(a, 500, 2600, more_a);
    run_race(d, 500, 2600, more_d);
    CHECK(more_a == more_d);
    CHECK(std::count(more_a.begin(), more_a.end(), Item::SpikedShell) > 0);
}

TEST_CASE("ink: none before frame 900, one held at a time, none within 1800 frames of a squirt") {
    LimitBench b;
    b.set_race_ticks(0);
    REQUIRE(b.world.item_tuning().ink_start_gate == 900);
    REQUIRE(b.world.item_tuning().ink_cooldown == 1800);
    while (b.world.race_ticks() < 900) CHECK_FALSE(LimitBench::any(b.draw(), Item::Ink));
    bool seen = false;
    for (int n = 0; n < 2000 && !seen; ++n) seen = LimitBench::any(b.draw(), Item::Ink);
    CHECK(seen);

    b.set_race_ticks(4000);
    // Held by one kart: nobody else draws one.
    b.kart(0).item = Item::Ink;
    CHECK((b.world.blocked_items(2) & item_bit(Item::Ink)) != 0);
    b.kart(0).item = Item::None;
    CHECK((b.world.blocked_items(2) & item_bit(Item::Ink)) == 0);
    // Squirted: 1800 frames with none.
    b.kart(3).item = Item::Ink;
    b.kart(3).item_uses = 1;
    b.tap(3);
    REQUIRE(b.kart(3).item != Item::Ink);
    CHECK(b.world.ink_cooldown() > 1790);
    const int used = b.world.race_ticks() - 2;
    while (b.world.race_ticks() < used + 1800 - 1) CHECK_FALSE(LimitBench::any(b.draw(), Item::Ink));
    seen = false;
    for (int n = 0; n < 3000 && !seen; ++n) seen = LimitBench::any(b.draw(), Item::Ink);
    CHECK(seen);
}
