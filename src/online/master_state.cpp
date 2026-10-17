#include "gk/online/master.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

namespace gk::online {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > 16) return false;
    return std::all_of(n.begin(), n.end(), [](char c) { return c >= 0x20 && c < 0x7f; }) && n.front() != ' ' &&
           n.back() != ' ';
}

} // namespace

std::string random_hex(int bytes) {
    static thread_local std::random_device rd;
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < bytes; ++i) {
        const auto b = uint8_t(rd());
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

MasterState::MasterState(std::string save_path) : path_(std::move(save_path)) {}

std::optional<NewAccount> MasterState::create_account(const std::string& wanted) {
    if (!valid_name(wanted)) return std::nullopt;
    std::lock_guard lock(mutex_);
    auto taken = [&](const std::string& n) {
        return std::any_of(accounts_.begin(), accounts_.end(), [&](const auto& a) { return lower(a.second.name) == lower(n); });
    };
    std::string name = wanted;
    for (int i = 2; taken(name); ++i) name = wanted.substr(0, 12) + std::to_string(i);
    NewAccount out;
    do out.account.id = random_hex(8);
    while (accounts_.count(out.account.id));
    out.account.name = name;
    out.token = random_hex(24);
    out.account.token_hash = sha256_hex(out.token);
    accounts_[out.account.id] = out.account;
    by_token_[out.account.token_hash] = out.account.id;
    save_locked();
    return out;
}

std::optional<Account> MasterState::account_for_token(const std::string& token) const {
    if (token.empty()) return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto it = by_token_.find(sha256_hex(token));
    if (it == by_token_.end()) return std::nullopt;
    return accounts_.at(it->second);
}

std::optional<Account> MasterState::account(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto it = accounts_.find(id);
    if (it == accounts_.end()) return std::nullopt;
    return it->second;
}

void MasterState::heartbeat(const ServerEntry& s, double now) {
    std::lock_guard lock(mutex_);
    ServerEntry e = s;
    e.last_seen = now;
    e.players = std::clamp(e.players, 0, std::max(1, e.max_players));
    servers_[e.key()] = e;
}

std::vector<ServerEntry> MasterState::servers(double now, int protocol) const {
    std::lock_guard lock(mutex_);
    std::vector<ServerEntry> out;
    for (const auto& [k, s] : servers_)
        if (now - s.last_seen <= kServerTimeout && (protocol == 0 || s.protocol == protocol)) out.push_back(s);
    return out;
}

std::optional<ServerEntry> MasterState::match(const std::string& region, int protocol, double now) const {
    const auto list = servers(now, protocol);
    const ServerEntry* best = nullptr;
    double best_score = -1e9;
    for (const ServerEntry& s : list) {
        if (s.players >= s.max_players) continue;
        double score = 0.0;
        if (!region.empty() && s.region == region) score += 1000.0;
        if (s.phase == "lobby") score += 100.0; // you get to race next, not spectate first
        score += 10.0 * s.players;              // join people rather than an empty room
        if (score > best_score) best_score = score, best = &s;
    }
    if (!best) return std::nullopt;
    return *best;
}

std::map<std::string, int> MasterState::report(const RaceReport& r) {
    std::lock_guard lock(mutex_);
    std::vector<Account*> racers;
    for (const std::string& id : r.accounts) {
        auto it = accounts_.find(id);
        if (it != accounts_.end() && std::find(racers.begin(), racers.end(), &it->second) == racers.end())
            racers.push_back(&it->second);
    }
    std::map<std::string, int> out;
    const size_t n = racers.size();
    std::vector<double> delta(n, 0.0);
    // Each pair is a game the better-placed player won; K is spread over the
    // opponents so a 12-player race moves a rating about as much as one duel.
    constexpr double kK = 32.0;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            const double expect_i = 1.0 / (1.0 + std::pow(10.0, (racers[j]->rating - racers[i]->rating) / 400.0));
            const double d = kK * (1.0 - expect_i) / double(n - 1);
            delta[i] += d;
            delta[j] -= d;
        }
    for (size_t i = 0; i < n; ++i) {
        Account& a = *racers[i];
        a.rating = std::max(1, a.rating + int(std::lround(delta[i])));
        ++a.races;
        if (i == 0 && n > 1) ++a.wins;
        out[a.id] = a.rating;
    }
    save_locked();
    return out;
}

bool MasterState::store_crash(const std::string& report) {
    if (report.size() > kMaxCrashBytes || report.rfind("Gobby Kart crash report", 0) != 0) return false;
    std::lock_guard lock(mutex_);
    if (crashes_ >= kMaxCrashes) return false;
    ++crashes_;
    if (path_.empty()) return true;
    std::error_code ec;
    const auto dir = std::filesystem::absolute(path_, ec).parent_path() / "crashes";
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / (std::to_string(std::time(nullptr)) + "-" + random_hex(4) + ".txt");
    std::ofstream f(file, std::ios::binary);
    f << report;
    return bool(f);
}

int MasterState::crash_count() const {
    std::lock_guard lock(mutex_);
    return crashes_;
}

bool MasterState::save() const {
    std::lock_guard lock(mutex_);
    return save_locked();
}

bool MasterState::save_locked() const {
    if (path_.empty()) return true;
    nlohmann::json j = nlohmann::json::array();
    for (const auto& [id, a] : accounts_)
        j.push_back({{"id", a.id}, {"name", a.name}, {"token_hash", a.token_hash}, {"rating", a.rating},
                     {"races", a.races}, {"wins", a.wins}});
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << nlohmann::json{{"accounts", j}}.dump(1);
        if (!f) return false;
    }
    std::remove(path_.c_str());
    return std::rename(tmp.c_str(), path_.c_str()) == 0;
}

bool MasterState::load() {
    if (path_.empty()) return false;
    std::ifstream f(path_);
    if (!f) return false;
    try {
        const auto j = nlohmann::json::parse(f);
        std::lock_guard lock(mutex_);
        accounts_.clear();
        by_token_.clear();
        for (const auto& e : j.at("accounts")) {
            Account a;
            a.id = e.at("id");
            a.name = e.at("name");
            a.token_hash = e.at("token_hash");
            a.rating = e.value("rating", 1000);
            a.races = e.value("races", 0);
            a.wins = e.value("wins", 0);
            by_token_[a.token_hash] = a.id;
            accounts_[a.id] = a;
        }
        std::error_code ec;
        const auto dir = std::filesystem::absolute(path_, ec).parent_path() / "crashes";
        crashes_ = 0;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) crashes_ += e.is_regular_file(ec) ? 1 : 0;
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

} // namespace gk::online
