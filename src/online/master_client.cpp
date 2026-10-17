#include "gk/online/master_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>

namespace gk::online {
namespace {

using json = nlohmann::json;

AccountInfo account_from(const json& j) {
    AccountInfo a;
    a.id = j.value("id", "");
    a.name = j.value("name", "");
    a.token = j.value("token", "");
    a.rating = j.value("rating", 1000);
    a.races = j.value("races", 0);
    a.wins = j.value("wins", 0);
    return a;
}

ServerEntry server_from(const json& j) {
    ServerEntry s;
    s.host = j.value("host", "");
    s.port = uint16_t(j.value("port", 0));
    s.name = j.value("name", "");
    s.region = j.value("region", "");
    s.protocol = j.value("protocol", 0);
    s.players = j.value("players", 0);
    s.max_players = j.value("max_players", 12);
    s.phase = j.value("phase", "");
    s.track = j.value("track", "");
    return s;
}

} // namespace

struct MasterClient::Impl {
    httplib::Client http;
    std::string key;
    bool tls;
    Impl(const std::string& url, std::string k) : http(url), key(std::move(k)), tls(url.rfind("https://", 0) == 0) {
        http.set_connection_timeout(5);
        http.set_read_timeout(10);
        http.set_write_timeout(10);
#ifdef GK_HAVE_HTTPS
        // A private CA (self-signed master, LAN tournaments); otherwise the system's.
        if (const char* ca = std::getenv("GK_MASTER_CA"); ca && *ca) http.set_ca_cert_path(ca);
#endif
    }
};

MasterClient::MasterClient(std::string base_url, std::string server_key)
    : impl_(std::make_unique<Impl>(base_url, std::move(server_key))) {}
MasterClient::~MasterClient() = default;
MasterClient::MasterClient(MasterClient&&) noexcept = default;

bool MasterClient::https() const { return impl_->tls; }

namespace {
// Runs a request, returning the parsed body for a 2xx status.
template <class F>
std::optional<json> call(F&& f, std::string& error) {
    const httplib::Result r = f();
    if (!r) {
        error = "master server unreachable (" + httplib::to_string(r.error()) + ")";
        return std::nullopt;
    }
    json j;
    try {
        j = r->body.empty() ? json::object() : json::parse(r->body);
    } catch (const std::exception&) {
        error = "bad reply from master server (HTTP " + std::to_string(r->status) + ")";
        return std::nullopt;
    }
    if (r->status < 200 || r->status >= 300) {
        error = j.value("error", "HTTP " + std::to_string(r->status));
        return std::nullopt;
    }
    error.clear();
    return j;
}
} // namespace

std::optional<AccountInfo> MasterClient::create_account(const std::string& name) {
    const auto j = call([&] { return impl_->http.Post("/v1/accounts", json{{"name", name}}.dump(), "application/json"); }, error_);
    if (!j) return std::nullopt;
    return account_from(*j);
}

std::optional<AccountInfo> MasterClient::me(const std::string& token) {
    const auto j = call([&] { return impl_->http.Get("/v1/me", httplib::Headers{{"Authorization", "Bearer " + token}}); }, error_);
    if (!j) return std::nullopt;
    AccountInfo a = account_from(*j);
    a.token = token;
    return a;
}

std::optional<ServerEntry> MasterClient::match(const std::string& region, int protocol) {
    const httplib::Params params{{"region", region}, {"protocol", std::to_string(protocol)}};
    const auto j = call([&] { return impl_->http.Get("/v1/match", params, httplib::Headers{}); }, error_);
    if (!j) return std::nullopt;
    return server_from(*j);
}

std::vector<ServerEntry> MasterClient::servers(int protocol) {
    const httplib::Params params{{"protocol", std::to_string(protocol)}};
    std::vector<ServerEntry> out;
    if (const auto j = call([&] { return impl_->http.Get("/v1/servers", params, httplib::Headers{}); }, error_))
        for (const auto& s : j->value("servers", json::array())) out.push_back(server_from(s));
    return out;
}

bool MasterClient::heartbeat(const ServerEntry& s) {
    json b{{"port", s.port}, {"name", s.name}, {"region", s.region}, {"protocol", s.protocol}, {"players", s.players},
           {"max_players", s.max_players}, {"phase", s.phase}, {"track", s.track}};
    if (!s.host.empty()) b["host"] = s.host;
    return call([&] { return impl_->http.Post("/v1/servers/heartbeat", httplib::Headers{{"X-Server-Key", impl_->key}}, b.dump(), "application/json"); },
                error_)
        .has_value();
}

std::optional<AccountInfo> MasterClient::verify(const std::string& token) {
    const auto j = call([&] {
        return impl_->http.Post("/v1/sessions/verify", httplib::Headers{{"X-Server-Key", impl_->key}}, json{{"token", token}}.dump(),
                                "application/json");
    }, error_);
    if (!j) return std::nullopt;
    return account_from(*j);
}

bool MasterClient::report(const RaceReport& r) {
    return call([&] {
        return impl_->http.Post("/v1/results", httplib::Headers{{"X-Server-Key", impl_->key}}, json{{"accounts", r.accounts}}.dump(),
                                "application/json");
    }, error_)
        .has_value();
}

} // namespace gk::online
