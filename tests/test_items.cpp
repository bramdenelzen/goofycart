// Items: roulette odds, boxes, throwing, hits, shields and the special items,
// on a flat straight so each case is easy to set up.
#include "sim_fixture.h"

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
