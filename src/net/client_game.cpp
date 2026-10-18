#include "gk/net/client_game.h"

#include "gk/core/log.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gk::net {
namespace {
constexpr int kInitialLead = 6;          // first guess before the server reports back
constexpr int kStaleSnapshotTicks = 150; // stop predicting when the server goes quiet
constexpr float kSnapDistance = 14.0f;   // corrections larger than this snap instead of blending
constexpr float kMaxLead = 14.0f;        // ticks: the most input buffer a bad connection gets
constexpr int kLeadSettleTicks = 40;     // server ticks to ignore the lead after a jump
constexpr float kOffsetDecay = 0.88f;    // per tick: most of a correction is gone in ~0.3 s
} // namespace

ClientGame::ClientGame(sim::PhysicsTables tables, SendFn send) : tables_(std::move(tables)), send_(std::move(send)) {
    data_hash_ = hash_constants(tables_);
}

void ClientGame::join(const std::string& name, const sim::Build& build, const std::string& token) {
    Hello h;
    h.name = name;
    h.build = build;
    h.token = token;
    send_(kChannelEvents, encode(h), true);
}

void ClientGame::set_build(const sim::Build& build, bool ready) {
    SetBuildMsg m;
    m.build = build;
    m.ready = ready;
    send_(kChannelEvents, encode(m), true);
}

vec3 ClientGame::visual_offset(int kart) const {
    return kart >= 0 && size_t(kart) < visual_offset_.size() ? visual_offset_[size_t(kart)] : vec3(0.0f);
}

std::vector<sim::KartEvent> ClientGame::take_events() { return std::exchange(events_, {}); }

void ClientGame::on_message(std::span<const uint8_t> data) {
    switch (peek_type(data)) {
    case MsgType::Welcome: {
        Welcome w;
        if (!decode(data, w)) return;
        welcomed_ = true;
        seat_ = w.seat == 255 ? -1 : int(w.seat);
        break;
    }
    case MsgType::RaceSetup: {
        RaceSetup s;
        if (decode(data, s)) apply_setup(s);
        break;
    }
    case MsgType::Snapshot: {
        SnapshotMsg m;
        if (decode(data, m)) apply_snapshot(m);
        break;
    }
    case MsgType::Lobby: {
        LobbyMsg m;
        if (decode(data, m)) {
            lobby_ = std::move(m);
            has_lobby_ = true;
        }
        break;
    }
    case MsgType::Results: {
        ResultsMsg m;
        if (decode(data, m)) results_ = std::move(m);
        break;
    }
    case MsgType::Events: {
        EventsMsg m;
        if (!decode(data, m) || !world_ || m.race_id != setup_.race_id) return;
        for (const auto& e : m.events) {
            const bool authoritative = e.type == sim::KartEventType::Lap || e.type == sim::KartEventType::Finish;
            if (e.kart != seat_ || authoritative) events_.push_back(e);
        }
        break;
    }
    default:
        break;
    }
}

void ClientGame::apply_setup(const RaceSetup& s) {
    if (s.data_hash != data_hash_ && !data_mismatch_) {
        data_mismatch_ = true;
        log_warn("server physics data differs from ours (hash {:08x} vs {:08x}): expect corrections", s.data_hash,
                 data_hash_);
    }
    if (!world_ || s.race_id != setup_.race_id || s.seats.size() != world_->karts().size()) {
        sim::RaceConfig rc;
        rc.engine = s.engine;
        rc.laps = s.laps;
        rc.countdown_frames = s.countdown;
        // The new setup's track: setup_ still holds the previous race's.
        if (!s.track.empty() && tracks_.find(s.track) < 0)
            log_warn("server races on track '{}', which this client does not have", s.track);
        world_ = std::make_unique<sim::World>(sim::make_track(tracks_, s.track), tables_, rc);
        for (const SeatInfo& seat : s.seats) world_->add_kart(seat.params);
        clock_started_ = false;
        history_ = {};
        snapshots_applied_ = 0;
        last_snapshot_tick_ = 0;
        ignore_lead_until_ = 0;
        time_scale_ = 1.0;
        target_lead_ = float(kTargetLead);
        remote_inputs_.assign(s.seats.size(), {});
        step_inputs_.assign(s.seats.size(), {});
        visual_offset_.assign(s.seats.size(), vec3(0.0f));
        events_.clear();
    } else {
        for (size_t i = 0; i < s.seats.size(); ++i) world_->set_kart_params(int(i), s.seats[i].params);
    }
    setup_ = s;
}

sim::KartInput ClientGame::input_at(uint32_t tick) const {
    // Exact input if we have it, otherwise the newest earlier one (after a clock jump).
    for (uint32_t back = 0; back < 32 && back < tick; ++back) {
        const InputSlot& slot = history_[(tick - back) % kHistory];
        if (slot.tick == tick - back) return slot.input;
    }
    return {};
}

void ClientGame::simulate_to(uint32_t target, bool forward) {
    while (uint32_t(world_->tick_count()) < target) {
        const uint32_t t = uint32_t(world_->tick_count()) + 1;
        const auto& karts = world_->karts();
        for (size_t i = 0; i < karts.size(); ++i) {
            // Mirrors the server: bots, and everyone past the finish line, use the bot driver.
            if (!setup_.seats[i].human || karts[i].finished) step_inputs_[i] = bot_input(*world_, int(i), setup_.cpu_level);
            else if (int(i) == seat_) step_inputs_[i] = input_at(t);
            else step_inputs_[i] = remote_inputs_[i];
        }
        world_->tick(step_inputs_);
        if (seat_ >= 0 && size_t(seat_) < karts.size()) {
            InputSlot& slot = history_[t % kHistory];
            if (slot.tick == t) {
                slot.state = karts[size_t(seat_)];
                slot.has_state = true;
            }
        }
        if (!forward) continue;
        // Lap and finish wait for the server; everything else is shown at once.
        for (const auto& e : world_->events())
            if (e.kart == seat_ && e.type != sim::KartEventType::Lap && e.type != sim::KartEventType::Finish)
                events_.push_back(e);
    }
}

void ClientGame::apply_snapshot(const SnapshotMsg& m) {
    if (!world_ || m.race_id != setup_.race_id || m.karts.size() != world_->karts().size()) return;
    if (snapshots_applied_ > 0 && m.tick <= last_snapshot_tick_) return; // old or duplicate
    ++snapshots_applied_;
    last_snapshot_tick_ = m.tick;
    ticks_since_snapshot_ = 0;
    for (size_t i = 0; i < m.karts.size(); ++i) remote_inputs_[i] = m.karts[i].input;

    // Clock: keep our inputs arriving kTargetLead ticks before the server needs them.
    if (!clock_started_) {
        clock_started_ = true;
        tick_ = m.tick + uint32_t(kInitialLead);
    } else if (seat_ >= 0) {
        // After a jump, the reported lead is stale until our new inputs have
        // been through the server and its average has settled.
        if (m.tick >= ignore_lead_until_) {
            last_lead_ = m.input_lead;
            // Adaptive input buffer: when inputs reach the server late (a
            // jittery or lossy connection), run further ahead so they arrive
            // in time; on a steady connection creep back to the minimum.
            if (last_lead_ < 1) target_lead_ = std::min(kMaxLead, target_lead_ + 1.5f);
            else target_lead_ = std::max(float(kTargetLead), target_lead_ - 0.02f);
            const int err = last_lead_ - int(std::lround(target_lead_));
            if (err < -4 || err > 20) {
                // Far off (start-up, a long stall): jump instead of drifting there.
                tick_ = uint32_t(std::max<int64_t>(int64_t(tick_) - err, int64_t(m.tick) + 1));
                ignore_lead_until_ = m.tick + uint32_t(kLeadSettleTicks);
                time_scale_ = 1.0;
            } else if (std::abs(err) <= 1) {
                time_scale_ = 1.0;
            } else {
                time_scale_ = std::clamp(1.0 - 0.015 * double(err), 0.9, 1.1);
            }
        }
    } else {
        // Spectators just stay slightly ahead of the newest snapshot.
        const int ahead = int(int64_t(tick_) - int64_t(m.tick));
        time_scale_ = ahead < 1 ? 1.05 : ahead > 4 ? 0.95 : 1.0;
    }
    if (tick_ < m.tick) tick_ = m.tick;

    // Rewind and replay.
    const auto& karts = world_->karts();
    std::vector<vec3> before(karts.size());
    for (size_t i = 0; i < karts.size(); ++i) before[i] = karts[i].pos + visual_offset_[i];
    const bool had_local = seat_ >= 0 && size_t(seat_) < karts.size();
    const vec3 local_before = had_local ? karts[size_t(seat_)].pos : vec3(0.0f);

    sim::WorldState ws;
    ws.tick = m.tick;
    ws.countdown_left = m.countdown_left;
    ws.race_ticks = int(m.race_ticks);
    ws.karts.reserve(m.karts.size());
    for (const auto& k : m.karts) ws.karts.push_back(k.state);
    ws.entities = m.entities;
    ws.box_respawn = m.boxes;
    ws.props = m.props;
    ws.rng.state = m.rng;
    world_->restore(ws);
    // Our own kart goes where we drove it: keep our movement over the
    // server's (which only guesses our input while packets are held up),
    // unless the server knows better, e.g. we were hit or fell off.
    if (seat_ >= 0) {
        const InputSlot& slot = history_[m.tick % kHistory];
        if (slot.tick == m.tick && slot.has_state) world_->adopt_motion(seat_, slot.state);
    }
    simulate_to(tick_, false);

    for (size_t i = 0; i < karts.size(); ++i) {
        vec3 off = before[i] - karts[i].pos;
        if (glm::length(off) > kSnapDistance || snapshots_applied_ == 1) off = vec3(0.0f);
        visual_offset_[i] = off;
    }
    if (had_local && snapshots_applied_ > 1) {
        last_correction_ = glm::length(local_before - karts[size_t(seat_)].pos);
        max_correction_ = std::max(max_correction_, last_correction_);
    }
}

void ClientGame::send_inputs() {
    if (seat_ < 0) return;
    InputMsg m;
    m.newest_tick = tick_;
    for (int i = 0; i < kInputRedundancy && uint32_t(i) < tick_; ++i) m.inputs.push_back(input_at(tick_ - uint32_t(i)));
    if (world_ && uint32_t(world_->tick_count()) == tick_ && size_t(seat_) < world_->karts().size()) {
        m.has_state = true;
        m.state = world_->karts()[size_t(seat_)];
    }
    send_(kChannelState, encode(m), false);
}

bool ClientGame::step(const sim::KartInput& input) {
    if (!world_ || !clock_started_) return false;
    if (++ticks_since_snapshot_ > kStaleSnapshotTicks) return false;
    // Corrections fade out: small ones quickly, big ones over half a second.
    for (vec3& o : visual_offset_) o *= glm::length(o) > 1.5f ? 0.93f : kOffsetDecay;
    ++tick_;
    history_[tick_ % kHistory] = {tick_, input};
    simulate_to(tick_, true);
    send_inputs();
    return true;
}

} // namespace gk::net
