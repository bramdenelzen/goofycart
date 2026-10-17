#include "master_link.h"

#include "gk/core/log.h"

#include <chrono>

namespace gk {

MasterLink::MasterLink(std::string url, std::string server_key, online::ServerEntry self)
    : client_(std::move(url), std::move(server_key)), status_(std::move(self)) {
    thread_ = std::thread([this] { run(); });
}

MasterLink::~MasterLink() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void MasterLink::sync(net::Room& room) {
    auto checks = room.take_account_checks();
    auto reports = room.take_reports();
    std::vector<Verified> done;
    {
        std::lock_guard lock(mutex_);
        status_.players = room.human_count();
        status_.phase = room.phase() == net::RoomPhase::Lobby ? "lobby" : room.phase() == net::RoomPhase::Racing ? "racing" : "results";
        status_.track = room.current_track();
        for (auto& c : checks) checks_.push_back(std::move(c));
        for (auto& r : reports) reports_.push_back(std::move(r));
        done.swap(verified_);
    }
    if (!checks.empty() || !reports.empty()) wake_.notify_all();
    for (const Verified& v : done) room.set_account(v.client, v.account, v.name);
}

void MasterLink::run() {
    using clock = std::chrono::steady_clock;
    auto next_beat = clock::now();
    bool was_ok = false;
    std::unique_lock lock(mutex_);
    while (!quit_) {
        wake_.wait_until(lock, next_beat, [this] { return quit_ || !checks_.empty() || !reports_.empty(); });
        if (quit_) break;
        if (!checks_.empty()) {
            const auto c = checks_.front();
            checks_.pop_front();
            lock.unlock();
            const auto a = client_.verify(c.token);
            lock.lock();
            if (a) verified_.push_back({c.client, a->id, a->name});
            else log_warn("master: could not verify a player ({})", client_.error());
            continue;
        }
        if (!reports_.empty()) {
            const auto r = reports_.front();
            reports_.pop_front();
            lock.unlock();
            if (!client_.report({r})) log_warn("master: race report failed ({})", client_.error());
            lock.lock();
            continue;
        }
        if (clock::now() >= next_beat) {
            const online::ServerEntry s = status_;
            lock.unlock();
            const bool ok = client_.heartbeat(s);
            lock.lock();
            if (ok != was_ok) {
                if (ok) log_info("master: registered as {} ({})", s.name, s.region.empty() ? "no region" : s.region);
                else log_warn("master: heartbeat failed ({})", client_.error());
                was_ok = ok;
            }
            next_beat = clock::now() + std::chrono::seconds(ok ? 10 : 5);
        }
    }
}

} // namespace gk
