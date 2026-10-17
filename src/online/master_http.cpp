#include "gk/online/master_http.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace gk::online {
namespace {

using json = nlohmann::json;

json account_json(const Account& a) {
    return {{"id", a.id}, {"name", a.name}, {"rating", a.rating}, {"races", a.races}, {"wins", a.wins}};
}

json server_json(const ServerEntry& s) {
    return {{"host", s.host}, {"port", s.port}, {"name", s.name}, {"region", s.region}, {"protocol", s.protocol},
            {"players", s.players}, {"max_players", s.max_players}, {"phase", s.phase}, {"track", s.track}};
}

void reply(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

std::string bearer(const httplib::Request& req) {
    const std::string h = req.get_header_value("Authorization");
    return h.rfind("Bearer ", 0) == 0 ? h.substr(7) : std::string();
}

std::optional<json> body_json(const httplib::Request& req) {
    try {
        return json::parse(req.body);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

void install_master_routes(httplib::Server& http, MasterState& state, std::string server_key, std::function<double()> clock) {
    auto server_ok = [server_key](const httplib::Request& req) {
        return server_key.empty() || req.get_header_value("X-Server-Key") == server_key;
    };

    http.Get("/healthz", [](const httplib::Request&, httplib::Response& res) { reply(res, 200, {{"ok", true}}); });

    http.Post("/v1/accounts", [&state](const httplib::Request& req, httplib::Response& res) {
        const auto b = body_json(req);
        if (!b || !b->contains("name") || !(*b)["name"].is_string()) return reply(res, 400, {{"error", "name required"}});
        const auto made = state.create_account((*b)["name"].get<std::string>());
        if (!made) return reply(res, 400, {{"error", "names are 1-16 printable characters"}});
        json j = account_json(made->account);
        j["token"] = made->token;
        reply(res, 201, j);
    });

    http.Get("/v1/me", [&state](const httplib::Request& req, httplib::Response& res) {
        const auto a = state.account_for_token(bearer(req));
        if (!a) return reply(res, 401, {{"error", "unknown token"}});
        reply(res, 200, account_json(*a));
    });

    http.Get("/v1/servers", [&state, clock](const httplib::Request& req, httplib::Response& res) {
        const int protocol = req.has_param("protocol") ? std::atoi(req.get_param_value("protocol").c_str()) : 0;
        json list = json::array();
        for (const auto& s : state.servers(clock(), protocol)) list.push_back(server_json(s));
        reply(res, 200, {{"servers", list}});
    });

    http.Get("/v1/match", [&state, clock](const httplib::Request& req, httplib::Response& res) {
        const int protocol = req.has_param("protocol") ? std::atoi(req.get_param_value("protocol").c_str()) : 0;
        const auto s = state.match(req.get_param_value("region"), protocol, clock());
        if (!s) return reply(res, 404, {{"error", "no server has a free seat"}});
        reply(res, 200, server_json(*s));
    });

    http.Post("/v1/servers/heartbeat", [&state, clock, server_ok](const httplib::Request& req, httplib::Response& res) {
        if (!server_ok(req)) return reply(res, 403, {{"error", "bad server key"}});
        const auto b = body_json(req);
        if (!b || !b->contains("port")) return reply(res, 400, {{"error", "port required"}});
        ServerEntry s;
        s.host = b->value("host", "");
        if (s.host.empty()) s.host = req.remote_addr; // the address it called from
        s.port = uint16_t(b->value("port", 0));
        s.name = b->value("name", "Gobby Kart server");
        s.region = b->value("region", "");
        s.protocol = b->value("protocol", 0);
        s.players = b->value("players", 0);
        s.max_players = b->value("max_players", 12);
        s.phase = b->value("phase", "");
        s.track = b->value("track", "");
        state.heartbeat(s, clock());
        reply(res, 200, json::object());
    });

    http.Post("/v1/sessions/verify", [&state, server_ok](const httplib::Request& req, httplib::Response& res) {
        if (!server_ok(req)) return reply(res, 403, {{"error", "bad server key"}});
        const auto b = body_json(req);
        const auto a = b ? state.account_for_token(b->value("token", "")) : std::nullopt;
        if (!a) return reply(res, 404, {{"error", "unknown token"}});
        reply(res, 200, account_json(*a));
    });

    http.Post("/v1/results", [&state, server_ok](const httplib::Request& req, httplib::Response& res) {
        if (!server_ok(req)) return reply(res, 403, {{"error", "bad server key"}});
        const auto b = body_json(req);
        if (!b || !b->contains("accounts") || !(*b)["accounts"].is_array()) return reply(res, 400, {{"error", "accounts required"}});
        RaceReport r;
        for (const auto& id : (*b)["accounts"])
            if (id.is_string()) r.accounts.push_back(id.get<std::string>());
        reply(res, 200, {{"ratings", state.report(r)}});
    });
}

} // namespace gk::online
