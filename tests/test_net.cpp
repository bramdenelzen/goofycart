// Netcode tests: the wire format, and whole races between a Room and several
// ClientGames over an in-memory link with latency and packet loss.
#include "sim_fixture.h"

#include "gk/net/client_game.h"
#include "gk/net/room.h"

#include <functional>
#include <map>
#include <memory>
#include <cmath>
#include <random>

using namespace gk;
using namespace gk::net;

namespace {

// Simulated network between one Room and its clients. Unreliable packets are
// dropped with probability `loss`; reliable ones arrive one round trip late
// instead, and stay ordered per channel, like ENet.
class TestNetwork : public MessageSink {
public:
    TestNetwork(int one_way_ms, float loss, int jitter_ms = 0) : one_way_(one_way_ms), jitter_(jitter_ms), loss_(loss) {}

    void send(ClientId client, uint8_t channel, std::vector<uint8_t> data, bool reliable) override {
        queue(true, client, channel, std::move(data), reliable);
    }
    void client_send(ClientId client, uint8_t channel, std::vector<uint8_t> data, bool reliable) {
        queue(false, client, channel, std::move(data), reliable);
    }

    // Delivers everything due by `now_ms`.
    template <class ToClient>
    void deliver(int now_ms, Room& room, ToClient&& to_client) {
        std::vector<Packet> due;
        for (auto it = packets_.begin(); it != packets_.end();) {
            if (it->due <= now_ms) {
                due.push_back(std::move(*it));
                it = packets_.erase(it);
            } else {
                ++it;
            }
        }
        std::stable_sort(due.begin(), due.end(), [](const Packet& a, const Packet& b) { return a.due < b.due; });
        for (Packet& p : due) {
            if (p.to_client) to_client(p.client, p.data);
            else room.on_message(p.client, p.data);
        }
    }
    void set_now(int ms) { now_ = ms; }
    // A Wi-Fi hiccup: everything sent during [from, to) arrives at `to`.
    void stall(int from, int to) {
        stall_from_ = from;
        stall_to_ = to;
    }
    // Drops queued traffic for a client that went away.
    void forget(ClientId client) {
        std::erase_if(packets_, [&](const Packet& p) { return p.client == client; });
    }

private:
    struct Packet {
        int due;
        bool to_client;
        ClientId client;
        std::vector<uint8_t> data;
    };
    void queue(bool to_client, ClientId client, uint8_t channel, std::vector<uint8_t> data, bool reliable) {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        int due = now_ + one_way_ + (jitter_ > 0 ? int(u(rng_) * float(jitter_)) : 0);
        if (u(rng_) < loss_) {
            if (!reliable) return;
            due += 2 * one_way_ + 20;
        }
        if (now_ >= stall_from_ && now_ < stall_to_) due = std::max(due, stall_to_ + one_way_);
        if (reliable) {
            int& last = last_reliable_[{to_client, client, channel}];
            due = std::max(due, last);
            last = due;
        }
        packets_.push_back({due, to_client, client, std::move(data)});
    }

    int one_way_, jitter_;
    float loss_;
    int now_ = 0;
    int stall_from_ = 0, stall_to_ = 0;
    std::mt19937 rng_{7};
    std::vector<Packet> packets_;
    std::map<std::tuple<bool, ClientId, uint8_t>, int> last_reliable_;
};

// Short lobby and results phases so tests spend their time racing.
RoomConfig quick_config(int laps = 1) {
    RoomConfig cfg;
    cfg.laps = laps;
    cfg.lobby_ticks = 120;
    cfg.all_ready_ticks = 30;
    cfg.results_ticks = 120;
    return cfg;
}

struct TestClient {
    ClientId id;
    std::unique_ptr<ClientGame> game;
    double accum = 0.0;
    bool auto_ready = true;
    bool sent_ready = false;
    double sum_correction = 0.0;
    int corrections = 0;
};

// Drives a room and clients in 1 ms steps. Server ticks at exactly 60 Hz;
// each client runs at 60 Hz scaled by its own clock adjustment.
struct Harness {
    sim::PhysicsTables tables = test::tables();
    TestNetwork net;
    Room room;
    std::map<ClientId, TestClient> clients;
    int now_ms = 0;
    double server_accum = 0.0;

    Harness(int one_way_ms, float loss, RoomConfig cfg = quick_config(), int jitter_ms = 0)
        : net(one_way_ms, loss, jitter_ms), room(tables, test::parts(), cfg, net) {}

    bool run_until(const std::function<bool()>& done, int limit_ms) {
        const int end = now_ms + limit_ms;
        while (!done() && now_ms < end) run_ms(50);
        return done();
    }

    ClientGame& connect(ClientId id, const std::string& name) {
        TestClient c;
        c.id = id;
        c.game = std::make_unique<ClientGame>(tables, [this, id](uint8_t ch, std::vector<uint8_t> d, bool rel) {
            net.client_send(id, ch, std::move(d), rel);
        });
        room.on_connect(id);
        ClientGame& g = *c.game;
        clients.emplace(id, std::move(c));
        g.join(name, {1, 0, 0, 0});
        return g;
    }

    void disconnect(ClientId id) {
        room.on_disconnect(id);
        clients.erase(id);
        net.forget(id);
    }

    void run_ms(int ms) {
        constexpr double kTickMs = 1000.0 / 60.0;
        for (int i = 0; i < ms; ++i) {
            ++now_ms;
            net.set_now(now_ms);
            net.deliver(now_ms, room, [&](ClientId id, const std::vector<uint8_t>& d) {
                if (auto it = clients.find(id); it != clients.end()) {
                    const int before = it->second.game->snapshots_applied();
                    it->second.game->on_message(d);
                    if (it->second.game->snapshots_applied() > before + 0 && it->second.game->snapshots_applied() > 1 &&
                        it->second.game->world() && it->second.game->world()->racing()) {
                        it->second.sum_correction += it->second.game->last_correction();
                        ++it->second.corrections;
                    }
                }
            });
            server_accum += 1.0;
            while (server_accum >= kTickMs) {
                server_accum -= kTickMs;
                room.tick();
            }
            for (auto& [id, c] : clients) {
                // Players press ready as soon as the lobby opens.
                const bool in_lobby = c.game->has_lobby() && c.game->phase() == RoomPhase::Lobby;
                if (in_lobby && c.auto_ready && !c.sent_ready) {
                    c.game->set_build({1, 0, 0, 0}, true);
                    c.sent_ready = true;
                }
                if (!in_lobby) c.sent_ready = false;
                c.accum += c.game->time_scale();
                while (c.accum >= kTickMs) {
                    c.accum -= kTickMs;
                    sim::KartInput in;
                    const sim::World* w = c.game->world();
                    const int seat = c.game->seat();
                    if (w && seat >= 0) {
                        in = bot_input(*w, seat, sim::CpuLevel::Hard);
                        // Even-numbered clients weave, so the others mispredict them.
                        if (id % 2 == 0) in.steer = std::clamp(in.steer + 0.4f * std::sin(float(c.game->tick()) * 0.07f), -1.0f, 1.0f);
                        if (!w->racing()) in.accelerate = false;
                    }
                    c.game->step(in);
                }
            }
        }
    }
};

} // namespace

TEST_CASE("kart state survives the wire within quantisation") {
    sim::KartState s;
    s.pos = {12.5f, -3.25f, 400.125f};
    s.rot = glm::angleAxis(0.7f, glm::normalize(vec3(0.1f, 1.0f, 0.05f)));
    s.speed = 23.4f;
    s.slide = {1.5f, 0.0f, -0.75f};
    s.grounded = false;
    s.air_vel = {3.0f, -8.0f, 20.0f};
    s.drift = sim::DriftState::Drifting;
    s.drift_dir = -1;
    s.mt_charge = 412.0f;
    s.boost = sim::BoostKind::SuperMiniTurbo;
    s.boost_frames = 77;
    s.boost_mult = 1.25f;
    s.lap = 2;
    s.checkpoint = 13;
    s.sample = 301;
    s.coins = 7;
    s.finished = true;
    s.finish_tick = 5555;
    s.susp_y = 0.083f;
    s.susp_vy = -1.7f;
    s.susp_pitch = -0.06f;
    s.susp_roll_v = 0.9f;
    s.susp_wobble = 4;

    Writer w;
    write_kart(w, s);
    CHECK(w.data().size() < 86);
    Reader r(w.data());
    const sim::KartState d = read_kart(r);
    CHECK(r.ok());
    CHECK(r.at_end());
    CHECK(glm::length(d.pos - s.pos) < 1e-4f);
    CHECK(std::abs(glm::dot(d.rot, s.rot)) > 0.999f);
    CHECK(d.speed == doctest::Approx(s.speed).epsilon(0.01));
    CHECK(glm::length(d.air_vel - s.air_vel) < 0.02f);
    CHECK(d.drift == s.drift);
    CHECK(d.drift_dir == -1);
    CHECK(d.mt_charge == doctest::Approx(412.0f));
    CHECK(d.boost == s.boost);
    CHECK(d.boost_frames == 77);
    CHECK(d.lap == 2);
    CHECK(d.checkpoint == 13);
    CHECK(d.sample == 301);
    CHECK(d.coins == 7);
    CHECK(d.finished);
    CHECK(d.finish_tick == 5555);
    CHECK(d.susp_y == doctest::Approx(0.083f).epsilon(0.001));
    CHECK(d.susp_vy == doctest::Approx(-1.7f).epsilon(0.001));
    CHECK(d.susp_pitch == doctest::Approx(-0.06f).epsilon(0.001));
    CHECK(d.susp_roll_v == doctest::Approx(0.9f).epsilon(0.001));
    CHECK(d.susp_wobble == 4);
}

TEST_CASE("messages round-trip and reject garbage") {
    InputMsg in;
    in.newest_tick = 1234;
    for (int i = 0; i < kInputRedundancy; ++i) {
        sim::KartInput k;
        k.steer = -0.5f + 0.1f * float(i);
        k.accelerate = true;
        k.drift = i % 2;
        in.inputs.push_back(k);
    }
    const auto bytes = encode(in);
    InputMsg out;
    REQUIRE(decode(bytes, out));
    CHECK(out.newest_tick == 1234);
    REQUIRE(out.inputs.size() == size_t(kInputRedundancy));
    CHECK(out.inputs[3].steer == doctest::Approx(-0.2f).epsilon(0.02));
    CHECK(out.inputs[3].drift);

    RaceSetup rs;
    rs.race_id = 9;
    rs.laps = 2;
    rs.seats.push_back({true, "Albert", {1, 2, 3, 0}, {}});
    rs.seats.push_back({false, "CPU", {0, 0, 0, 1}, {}});
    RaceSetup rs2;
    REQUIRE(decode(encode(rs), rs2));
    CHECK(rs2.race_id == 9);
    CHECK(rs2.laps == 2);
    REQUIRE(rs2.seats.size() == 2);
    CHECK(rs2.seats[0].human);
    CHECK(rs2.seats[0].name == "Albert");
    CHECK(rs2.seats[0].build.tires == 3);

    std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + 7);
    CHECK_FALSE(decode(truncated, out));
    SnapshotMsg snap;
    CHECK_FALSE(decode(bytes, snap)); // wrong type
}

TEST_CASE("four clients race at 150 ms round trip with 2% loss") {
    Harness h(75, 0.02f, quick_config(), 10);
    h.connect(1, "A");
    h.connect(2, "B");
    h.connect(3, "C");
    h.connect(4, "D");
    REQUIRE(h.run_until([&] { return h.room.racing(); }, 5000));
    h.run_ms(500);
    CHECK(h.room.human_count() == 4);
    for (auto& [id, c] : h.clients) {
        CHECK(c.game->seat() >= 0);
        CHECK(c.game->world() != nullptr);
    }

    // Race until the room ends it (everyone finished, or the grace period ran out).
    int human_ticks = 0;
    std::map<ClientId, int> seat_of;
    for (auto& [id, c] : h.clients) seat_of[id] = c.game->seat();
    while (h.room.racing() && h.now_ms < 200'000) {
        h.run_ms(100);
        if (h.room.racing() && h.room.world()->racing()) human_ticks += 4 * 6;
    }
    CHECK_FALSE(h.room.racing());
    CHECK(h.now_ms < 200'000);

    // Inputs arrived in time: redundancy covers loss and the clock keeps a lead.
    MESSAGE("late inputs: ", h.room.late_inputs(), " of ~", human_ticks);
    CHECK(h.room.late_inputs() < human_ticks / 100);

    for (auto& [id, c] : h.clients) {
        const sim::World* w = c.game->world();
        REQUIRE(w != nullptr);
        const auto& k = w->karts()[size_t(seat_of[id])];
        CHECK(k.finished);
        const double mean = c.corrections ? c.sum_correction / c.corrections : 0.0;
        MESSAGE("client ", id, ": lead ", c.game->input_lead(), ", mean correction ", mean, " m, max ",
                c.game->max_correction(), " m over ", c.corrections, " snapshots");
        CHECK(c.corrections > 100);
        CHECK(mean < 0.15);
    }
}

TEST_CASE("a bad connection: 250 ms round trip, 80 ms jitter, 8% loss") {
    Harness h(125, 0.08f, quick_config(), 80);
    h.connect(1, "A");
    h.connect(2, "B");
    REQUIRE(h.run_until([&] { return h.room.racing(); }, 8000));
    h.run_ms(3000);
    for (int i = 0; i < 300 && h.room.racing(); ++i) h.run_ms(100);
    for (auto& [id, c] : h.clients) {
        const double mean = c.corrections ? c.sum_correction / c.corrections : 0.0;
        MESSAGE("client ", id, ": mean correction ", mean, " m, max ", c.game->max_correction(), " m");
        CHECK(mean < 0.05);
        CHECK(c.game->max_correction() < 1.0);
    }
}

TEST_CASE("Wi-Fi hiccups don't throw the player's own kart around") {
    Harness h(40, 0.02f, quick_config(3), 20);
    h.connect(1, "A");
    h.connect(2, "B");
    REQUIRE(h.run_until([&] { return h.room.racing(); }, 8000));
    h.run_ms(4000);
    for (int spike = 0; spike < 8 && h.room.racing(); ++spike) {
        h.net.stall(h.now_ms, h.now_ms + 400 + spike * 80);
        h.run_ms(2000);
    }
    // Hits and bumps the client could not see coming still correct it a little.
    for (auto& [id, c] : h.clients) {
        const double mean = c.corrections ? c.sum_correction / c.corrections : 0.0;
        MESSAGE("client ", id, ": mean correction ", mean, " m, worst ", c.game->max_correction(), " m");
        CHECK(mean < 0.02);
        CHECK(c.game->max_correction() < 5.0);
    }
}

TEST_CASE("a bot takes over when a player leaves, and late joiners spectate") {
    Harness h(40, 0.0f);
    ClientGame& a = h.connect(1, "A");
    h.connect(2, "B");
    REQUIRE(h.run_until([&] { return h.room.racing(); }, 5000));
    h.run_ms(300);
    const int seat_b = h.clients.at(2).game->seat();
    REQUIRE(seat_b >= 0);
    CHECK(a.setup().seats[size_t(seat_b)].human);
    CHECK(a.setup().seats[size_t(seat_b)].name == "B");

    h.run_ms(6000); // past the countdown, racing
    const int sample_before = h.room.world()->karts()[size_t(seat_b)].sample;
    h.disconnect(2);
    h.run_ms(3000);
    CHECK(h.room.human_count() == 1);
    CHECK_FALSE(a.setup().seats[size_t(seat_b)].human);
    CHECK(h.room.world()->karts()[size_t(seat_b)].sample != sample_before); // still driving

    // Joining mid-race: watch now, race next time.
    ClientGame& c = h.connect(3, "C");
    h.run_ms(500);
    CHECK(c.joined());
    CHECK(c.seat() == -1);
    CHECK(c.world() != nullptr);
    CHECK(c.snapshots_applied() > 5);
    CHECK(c.phase() == RoomPhase::Racing);
    const uint32_t first_race = h.room.race_id();
    REQUIRE(h.run_until([&] { return h.room.race_id() != first_race; }, 300'000));
    h.run_ms(500);
    CHECK(h.room.race_id() == first_race + 1);
    CHECK(c.seat() >= 0);
    CHECK(h.room.human_count() == 2);
}

TEST_CASE("the lobby waits for players to be ready") {
    RoomConfig cfg = quick_config();
    cfg.lobby_ticks = 600;
    Harness h(20, 0.0f, cfg);
    h.run_ms(1000);
    CHECK(h.room.phase() == RoomPhase::Lobby); // nobody here: no timer
    ClientGame& a = h.connect(1, "A");
    h.clients.at(1).auto_ready = false;
    h.run_ms(1000);
    REQUIRE(a.has_lobby());
    CHECK(a.phase() == RoomPhase::Lobby);
    REQUIRE(a.lobby().players.size() == 1);
    CHECK(a.lobby().players[0].you);
    CHECK_FALSE(a.lobby().players[0].ready);
    CHECK(a.lobby().seconds_left > 5);

    a.set_build({2, 3, 1, 1}, true);
    h.run_ms(1000);
    CHECK(h.room.racing());
    CHECK(a.setup().seats[size_t(a.seat())].build.body == 3);
}

TEST_CASE("a Grand Prix awards MK8 points over its races") {
    RoomConfig cfg = quick_config();
    cfg.races_per_gp = 2;
    Harness h(20, 0.0f, cfg);
    ClientGame& a = h.connect(1, "A");
    REQUIRE(h.run_until([&] { return h.room.phase() == RoomPhase::Results; }, 200'000));
    h.run_ms(200);
    const ResultsMsg* r1 = a.results();
    REQUIRE(r1 != nullptr);
    CHECK(r1->race_number == 1);
    REQUIRE(r1->entries.size() == 12);
    int sum = 0;
    for (size_t i = 0; i < r1->entries.size(); ++i) {
        CHECK(r1->entries[i].points == kRacePoints[i]);
        CHECK(r1->entries[i].total == r1->entries[i].points);
        sum += r1->entries[i].points;
    }
    CHECK(sum == 82);
    int my_total = 0;
    for (const auto& e : r1->entries)
        if (e.human) my_total = e.total;
    CHECK(my_total > 0);
    CHECK(r1->entries[0].finish_ticks > 0);

    REQUIRE(h.run_until([&] { return h.room.racing(); }, 20'000));
    REQUIRE(h.run_until([&] { return h.room.phase() == RoomPhase::Results; }, 200'000));
    h.run_ms(200);
    const ResultsMsg* r2 = a.results();
    REQUIRE(r2 != nullptr);
    CHECK(r2->race_number == 2);
    int total_sum = 0;
    for (const auto& e : r2->entries) total_sum += e.total;
    CHECK(total_sum == 2 * 82);

    // After the last race the Grand Prix starts over.
    REQUIRE(h.run_until([&] { return h.room.phase() == RoomPhase::Lobby; }, 20'000));
    h.run_ms(200);
    CHECK(a.lobby().race_number == 1);
    CHECK(a.lobby().players[0].points == 0);
}

TEST_CASE("an empty room goes back to the lobby") {
    Harness h(10, 0.0f);
    h.connect(1, "A");
    REQUIRE(h.run_until([&] { return h.room.racing(); }, 5000));
    h.disconnect(1);
    h.run_ms(100);
    CHECK_FALSE(h.room.racing());
    CHECK(h.room.phase() == RoomPhase::Lobby);
}

TEST_CASE("clients race on the server's track, not their previous one") {
    // Each client builds the track from its own data; the first race, and
    // every race after a track change, must use the track the server named.
    sim::TrackCatalog catalog;
    std::string err;
    REQUIRE(catalog.load(sim::data_path("tracks/tracks.json"), &err));
    RoomConfig cfg = quick_config();
    cfg.tracks = catalog;
    cfg.track = catalog.tracks.back().id; // not the default first track
    Harness h(20, 0.0f, cfg);
    ClientGame& g = h.connect(1, "A");
    g.set_tracks(catalog);
    REQUIRE(h.run_until([&] { return h.room.racing() && g.world(); }, 5000));
    CHECK(h.room.world()->track().id == cfg.track);
    CHECK(g.world()->track().id == cfg.track);
}
