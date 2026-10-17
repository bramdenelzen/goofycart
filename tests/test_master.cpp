// Master server: accounts, server list and matchmaking, ratings, and the
// HTTP API end to end on localhost.
#include "gk/online/master_client.h"
#include "gk/online/master_http.h"

#include <doctest/doctest.h>
#include <httplib.h>

#include <cstdio>
#include <thread>

using namespace gk::online;

TEST_CASE("sha256 matches the standard test vectors") {
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex(std::string(1000, 'a')).size() == 64);
}

TEST_CASE("accounts: names, tokens, saving") {
    const std::string path = "test_accounts.json";
    std::remove(path.c_str());
    std::string token, id;
    {
        MasterState m(path);
        const auto a = m.create_account("Albert");
        REQUIRE(a);
        token = a->token;
        id = a->account.id;
        CHECK(a->account.token_hash != token); // only the hash is kept
        CHECK(m.account_for_token(token)->name == "Albert");
        CHECK_FALSE(m.account_for_token("nope"));
        const auto b = m.create_account("albert"); // taken, ignoring case
        REQUIRE(b);
        CHECK(b->account.name == "albert2");
        CHECK_FALSE(m.create_account(""));
        CHECK_FALSE(m.create_account("a name far too long to show"));
    }
    MasterState again(path);
    REQUIRE(again.load());
    CHECK(again.account_for_token(token)->id == id);
    std::remove(path.c_str());
}

TEST_CASE("servers drop off when silent, and matchmaking picks a good one") {
    MasterState m;
    auto server = [](std::string host, std::string region, int players, std::string phase, int protocol = 6) {
        ServerEntry s;
        s.host = host;
        s.port = 7777;
        s.region = region;
        s.players = players;
        s.phase = phase;
        s.protocol = protocol;
        return s;
    };
    m.heartbeat(server("eu-empty", "eu", 0, "lobby"), 0.0);
    m.heartbeat(server("eu-busy", "eu", 5, "lobby"), 0.0);
    m.heartbeat(server("eu-full", "eu", 12, "lobby"), 0.0);
    m.heartbeat(server("na-busy", "na", 8, "lobby"), 0.0);
    m.heartbeat(server("eu-old", "eu", 3, "lobby", 5), 0.0);
    CHECK(m.servers(1.0, 6).size() == 4);
    CHECK(m.match("eu", 6, 1.0)->host == "eu-busy");
    CHECK(m.match("na", 6, 1.0)->host == "na-busy");
    CHECK(m.match("asia", 6, 1.0)->host == "na-busy"); // nobody near: the busiest lobby
    CHECK_FALSE(m.match("eu", 7, 1.0));
    // Thirty seconds of silence and it is gone.
    m.heartbeat(server("eu-empty", "eu", 0, "lobby"), 40.0);
    CHECK(m.servers(41.0, 6).size() == 1);
    CHECK(m.match("eu", 6, 41.0)->host == "eu-empty");
}

TEST_CASE("ratings move towards results") {
    MasterState m;
    const auto a = m.create_account("A"), b = m.create_account("B"), c = m.create_account("C");
    REQUIRE((a && b && c));
    auto r = m.report({{a->account.id, b->account.id, c->account.id}});
    CHECK(r[a->account.id] > 1000);
    CHECK(r[c->account.id] < 1000);
    CHECK(r[a->account.id] + r[b->account.id] + r[c->account.id] == 3000);
    CHECK(m.account(a->account.id)->wins == 1);
    CHECK(m.account(b->account.id)->races == 1);
    // An upset moves more than an expected result.
    const int before = m.account(c->account.id)->rating;
    r = m.report({{c->account.id, a->account.id}});
    CHECK(r[c->account.id] - before > 16);
    // Unknown accounts are ignored.
    CHECK(m.report({{"ghost", a->account.id}}).size() == 1);
}

TEST_CASE("the HTTP API end to end") {
    MasterState state;
    httplib::Server http;
    double now = 0.0;
    install_master_routes(http, state, "secret", [&now] { return now; });
    const int port = http.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread t([&] { http.listen_after_bind(); });
    http.wait_until_ready();
    const std::string url = "http://127.0.0.1:" + std::to_string(port);

    MasterClient player(url);
    const auto acc = player.create_account("Albert");
    REQUIRE_MESSAGE(acc, player.error());
    CHECK(acc->token.size() == 48);
    CHECK(player.me(acc->token)->name == "Albert");
    CHECK_FALSE(player.me("bad"));
    CHECK_FALSE(player.match("eu", 6)); // no servers yet
    CHECK(player.error().find("free seat") != std::string::npos);

    MasterClient wrong(url, "guess");
    ServerEntry self;
    self.port = 7777;
    self.region = "eu";
    self.protocol = 6;
    self.phase = "lobby";
    CHECK_FALSE(wrong.heartbeat(self));
    MasterClient game(url, "secret");
    REQUIRE_MESSAGE(game.heartbeat(self), game.error());
    const auto found = player.match("eu", 6);
    REQUIRE(found);
    CHECK(found->host == "127.0.0.1"); // taken from where the heartbeat came from
    CHECK(found->port == 7777);
    CHECK(player.servers(6).size() == 1);

    const auto who = game.verify(acc->token);
    REQUIRE(who);
    CHECK(who->id == acc->id);
    CHECK_FALSE(game.verify("bad"));
    const auto other = player.create_account("Bea");
    REQUIRE(other);
    CHECK(game.report({{other->id, acc->id}}));
    CHECK(player.me(acc->token)->rating < 1000);
    CHECK_FALSE(wrong.report({{acc->id}}));

    http.stop();
    t.join();
}
