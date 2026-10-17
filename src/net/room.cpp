#include "gk/net/room.h"

#include "gk/core/log.h"

#include <algorithm>

namespace gk::net {

Room::Room(sim::PhysicsTables tables, sim::PartsCatalog parts, RoomConfig config, MessageSink& sink)
    : tables_(std::move(tables)), parts_(std::move(parts)), config_(config), sink_(sink) {
    data_hash_ = hash_constants(tables_);
    config_.seats = std::clamp(config_.seats, 1, kMaxSeats);
    config_.races_per_gp = std::max(1, config_.races_per_gp);
}

int Room::human_count() const {
    int n = 0;
    for (const auto& [id, c] : clients_) n += c.hello;
    return n;
}

sim::Build Room::clamp_build(sim::Build b) const {
    b.driver = std::clamp(b.driver, 0, int(parts_.drivers.size()) - 1);
    b.body = std::clamp(b.body, 0, int(parts_.bodies.size()) - 1);
    b.tires = std::clamp(b.tires, 0, int(parts_.tires.size()) - 1);
    b.glider = std::clamp(b.glider, 0, int(parts_.gliders.size()) - 1);
    return b;
}

sim::KartParams Room::params_for(const sim::Build& b) const {
    const sim::Build v = clamp_build(b);
    return tables_.params(parts_.levels(v), parts_.body(v), config_.engine);
}

void Room::on_connect(ClientId id) { clients_[id] = Client{}; }

void Room::on_disconnect(ClientId id) {
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    const int seat = it->second.seat;
    log_info("room: {} left", it->second.name);
    clients_.erase(it);
    // A bot takes over the kart in place, keeping position, items and coins.
    if (world_ && seat >= 0 && seat < int(seats_.size())) {
        Seat& s = seats_[size_t(seat)];
        s.human = false;
        s.name = parts_.drivers[size_t(s.build.driver)].name;
        broadcast_setup();
    }
    send_lobby();
}

void Room::on_message(ClientId id, std::span<const uint8_t> data) {
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    Client& c = it->second;
    switch (peek_type(data)) {
    case MsgType::Hello: {
        Hello h;
        if (!decode(data, h) || h.protocol != kProtocolVersion || c.hello) return;
        c.hello = true;
        c.name = h.name.empty() ? "Player" : h.name.substr(0, 16);
        c.build = clamp_build(h.build);
        log_info("room: {} joined", c.name);
        // Joining during the countdown still gets a kart; later, you watch.
        if (phase_ == RoomPhase::Racing && world_->countdown_left() > 0) {
            assign_seat(id, c);
            broadcast_setup();
        }
        Welcome w;
        w.seat = c.seat < 0 ? 255 : uint8_t(c.seat);
        w.server_tick = server_tick_;
        sink_.send(id, kChannelEvents, encode(w), true);
        if (world_ && phase_ == RoomPhase::Racing) sink_.send(id, kChannelEvents, encode(make_setup()), true);
        if (phase_ == RoomPhase::Results) sink_.send(id, kChannelEvents, encode(results_), true);
        send_lobby();
        break;
    }
    case MsgType::SetBuild: {
        SetBuildMsg m;
        if (!c.hello || !decode(data, m)) return;
        c.build = clamp_build(m.build);
        c.ready = phase_ == RoomPhase::Lobby && m.ready;
        send_lobby();
        break;
    }
    case MsgType::Input: {
        InputMsg m;
        if (!c.hello || c.seat < 0 || !world_ || phase_ != RoomPhase::Racing || !decode(data, m) || m.inputs.empty())
            return;
        const uint32_t now = uint32_t(world_->tick_count());
        // Never trust client state: inputs are clamped by the decoder and only
        // inputs for future ticks are accepted.
        for (size_t i = 0; i < m.inputs.size(); ++i) {
            const uint32_t t = m.newest_tick - uint32_t(i);
            if (t <= now || t > now + 120) continue;
            c.inputs.emplace(t, m.inputs[i]);
        }
        const float lead = float(int64_t(m.newest_tick) - int64_t(now) - 1);
        c.lead = c.lead * 0.8f + lead * 0.2f;
        break;
    }
    default:
        break;
    }
}

void Room::assign_seat(ClientId id, Client& c) {
    for (size_t i = 0; i < seats_.size(); ++i) {
        // Humans replace bots from the back of the grid forwards.
        const size_t s = seats_.size() - 1 - i;
        if (!seats_[s].human) {
            seats_[s].human = true;
            seats_[s].client = id;
            seats_[s].name = c.name;
            seats_[s].build = c.build;
            seats_[s].params = params_for(c.build);
            if (world_ && s < world_->karts().size()) world_->set_kart_params(int(s), seats_[s].params);
            c.seat = int(s);
            c.inputs.clear();
            c.lead = 0.0f;
            return;
        }
    }
}

void Room::enter_lobby() {
    phase_ = RoomPhase::Lobby;
    world_.reset();
    seats_.clear();
    for (auto& [id, c] : clients_) {
        c.ready = false;
        c.seat = -1;
    }
    lobby_timer_running_ = human_count() > 0 || !config_.idle_without_humans;
    phase_ticks_ = config_.lobby_ticks;
    ready_ticks_ = -1;
    send_lobby();
}

void Room::start_race() {
    ++race_id_;
    phase_ = RoomPhase::Racing;
    first_finish_for_ = -1;
    humans_done_for_ = -1;
    sim::RaceConfig rc;
    rc.engine = config_.engine;
    rc.laps = config_.laps;
    rc.countdown_frames = config_.countdown;
    world_ = std::make_unique<sim::World>(sim::make_greybox_track(), tables_, rc);
    seats_.assign(size_t(config_.seats), Seat{});
    const int nd = int(parts_.drivers.size()), nb = int(parts_.bodies.size()), nt = int(parts_.tires.size());
    for (int i = 0; i < config_.seats; ++i) {
        Seat& s = seats_[size_t(i)];
        s.build = clamp_build({i % nd, (i / 2) % nb, (i / 3) % nt, i % 2});
        s.name = parts_.drivers[size_t(s.build.driver)].name;
        s.params = params_for(s.build);
        world_->add_kart(s.params);
    }
    for (auto& [id, c] : clients_) {
        c.seat = -1;
        c.ready = false;
        if (c.hello) assign_seat(id, c);
    }
    step_inputs_.assign(seats_.size(), {});
    log_info("room: race {} ({} of {}) starting with {} player(s)", race_id_, race_number_, config_.races_per_gp,
             human_count());
    broadcast_setup();
    send_lobby();
}

void Room::finish_race() {
    const auto order = world_->standings();
    const auto& karts = world_->karts();
    results_ = {};
    results_.race_id = race_id_;
    results_.race_number = uint8_t(race_number_);
    results_.races_per_gp = uint8_t(config_.races_per_gp);
    for (size_t p = 0; p < order.size(); ++p) {
        const int s = order[p];
        const Seat& seat = seats_[size_t(s)];
        ResultEntry e;
        e.seat = uint8_t(s);
        e.human = seat.human;
        e.name = seat.name;
        e.finish_ticks = karts[size_t(s)].finished ? uint32_t(karts[size_t(s)].finish_tick) : 0u;
        e.points = uint8_t(p < 12 ? kRacePoints[p] : 0);
        if (seat.human && clients_.count(seat.client)) {
            Client& c = clients_[seat.client];
            c.points += e.points;
            e.total = uint16_t(c.points);
        } else {
            bot_points_[size_t(s)] += e.points;
            e.total = uint16_t(bot_points_[size_t(s)]);
        }
        results_.entries.push_back(e);
    }
    log_info("room: race {} over, winner {}", race_id_, results_.entries.empty() ? "-" : results_.entries[0].name);
    const auto msg = encode(results_);
    for (auto& [id, c] : clients_)
        if (c.hello) sink_.send(id, kChannelEvents, msg, true);
    phase_ = RoomPhase::Results;
    phase_ticks_ = config_.results_ticks;
    send_lobby();
}

RaceSetup Room::make_setup() const {
    RaceSetup s;
    s.race_id = race_id_;
    s.start_tick = server_tick_ - uint32_t(world_ ? world_->tick_count() : 0);
    s.engine = config_.engine;
    s.laps = uint8_t(config_.laps);
    s.countdown = uint16_t(config_.countdown);
    s.data_hash = data_hash_;
    for (const Seat& seat : seats_) s.seats.push_back({seat.human, seat.name, seat.build, seat.params});
    return s;
}

void Room::broadcast_setup() {
    const auto msg = encode(make_setup());
    for (auto& [id, c] : clients_) {
        if (!c.hello) continue;
        Welcome w;
        w.seat = c.seat < 0 ? 255 : uint8_t(c.seat);
        w.server_tick = server_tick_;
        sink_.send(id, kChannelEvents, encode(w), true);
        sink_.send(id, kChannelEvents, msg, true);
    }
}

void Room::send_lobby_to(ClientId to) {
    LobbyMsg m;
    m.phase = phase_;
    m.seconds_left = phase_ == RoomPhase::Racing || (phase_ == RoomPhase::Lobby && !lobby_timer_running_)
                         ? 0
                         : uint16_t((std::max(lobby_ticks_left(), 0) + 59) / 60);
    m.race_number = uint8_t(race_number_);
    m.races_per_gp = uint8_t(config_.races_per_gp);
    for (const auto& [id, c] : clients_) {
        if (!c.hello) continue;
        m.players.push_back({c.name, c.build, c.ready, uint16_t(c.points), id == to});
    }
    sink_.send(to, kChannelEvents, encode(m), true);
}

int Room::lobby_ticks_left() const {
    return phase_ == RoomPhase::Lobby && ready_ticks_ >= 0 ? std::min(phase_ticks_, ready_ticks_) : phase_ticks_;
}

void Room::send_lobby() {
    for (const auto& [id, c] : clients_)
        if (c.hello) send_lobby_to(id);
}

void Room::tick() {
    ++server_tick_;
    const int humans = human_count();
    switch (phase_) {
    case RoomPhase::Lobby: {
        if (humans == 0 && config_.idle_without_humans) {
            lobby_timer_running_ = false;
            race_number_ = 1;
            bot_points_ = {};
            return;
        }
        if (!lobby_timer_running_) {
            lobby_timer_running_ = true;
            phase_ticks_ = config_.lobby_ticks;
            send_lobby();
        }
        bool all_ready = humans > 0;
        for (const auto& [id, c] : clients_)
            if (c.hello) all_ready &= c.ready;
        // Everyone ready: a short countdown, cancelled if someone un-readies or joins.
        if (all_ready && ready_ticks_ < 0) {
            ready_ticks_ = config_.all_ready_ticks;
            send_lobby();
        } else if (!all_ready && ready_ticks_ >= 0) {
            ready_ticks_ = -1;
            send_lobby();
        }
        --phase_ticks_;
        if (ready_ticks_ >= 0) --ready_ticks_;
        if (phase_ticks_ <= 0 || ready_ticks_ == 0) {
            ready_ticks_ = -1;
            start_race();
        } else if (phase_ticks_ % 60 == 0) {
            send_lobby();
        }
        return;
    }
    case RoomPhase::Racing:
        if (humans == 0 && config_.idle_without_humans) {
            log_info("room: everyone left, back to the lobby");
            race_number_ = 1;
            bot_points_ = {};
            enter_lobby();
            return;
        }
        step_race();
        return;
    case RoomPhase::Results:
        if (--phase_ticks_ <= 0) {
            if (race_number_ >= config_.races_per_gp) {
                // Grand Prix over: totals start again.
                race_number_ = 1;
                bot_points_ = {};
                for (auto& [id, c] : clients_) c.points = 0;
            } else {
                ++race_number_;
            }
            enter_lobby();
        } else if (phase_ticks_ % 60 == 0) {
            send_lobby();
        }
        return;
    }
}

void Room::step_race() {
    const uint32_t next = uint32_t(world_->tick_count()) + 1;
    const auto& karts = world_->karts();
    for (size_t i = 0; i < seats_.size(); ++i) {
        Seat& seat = seats_[i];
        sim::KartInput in;
        if (seat.human) {
            Client& c = clients_[seat.client];
            auto it = c.inputs.find(next);
            if (it != c.inputs.end()) {
                in = it->second;
                c.used_tick = next;
            } else {
                in = c.last_input; // late or lost: repeat the last input
                if (world_->racing() && !karts[i].finished) ++late_inputs_;
            }
            c.last_input = in;
            c.inputs.erase(c.inputs.begin(), c.inputs.upper_bound(next));
        }
        // Bots drive themselves; so does everyone after crossing the line.
        if (!seat.human || karts[i].finished) in = bot_input(karts[i], world_->track(), int(i));
        seat.last_input = in;
        step_inputs_[i] = in;
    }
    world_->tick(step_inputs_);

    if (!world_->events().empty()) {
        EventsMsg ev;
        ev.race_id = race_id_;
        ev.tick = uint32_t(world_->tick_count());
        ev.events = world_->events();
        const auto msg = encode(ev);
        for (auto& [id, c] : clients_)
            if (c.hello) sink_.send(id, kChannelEvents, msg, true);
    }
    if (world_->tick_count() % kSnapshotInterval == 0) send_snapshots();

    // When to end: everyone finished; shortly after the last player finished;
    // or a time limit after the first player finished.
    bool all_done = true, humans_done = true, any_human = false, human_finished = false;
    for (size_t i = 0; i < seats_.size(); ++i) {
        all_done &= karts[i].finished;
        if (seats_[i].human) {
            any_human = true;
            humans_done &= karts[i].finished;
            human_finished |= karts[i].finished;
        }
    }
    if (!any_human) humans_done = all_done;
    if (human_finished && first_finish_for_ < 0) first_finish_for_ = 0;
    if (first_finish_for_ >= 0) ++first_finish_for_;
    if (humans_done && humans_done_for_ < 0) humans_done_for_ = 0;
    if (humans_done_for_ >= 0) ++humans_done_for_;
    if (all_done || humans_done_for_ >= config_.after_humans_ticks || first_finish_for_ >= config_.finish_timeout_ticks ||
        world_->race_ticks() > config_.max_race_ticks)
        finish_race();
}

void Room::send_snapshots() {
    SnapshotMsg base;
    base.race_id = race_id_;
    base.tick = uint32_t(world_->tick_count());
    base.countdown_left = world_->countdown_left();
    base.race_ticks = uint32_t(world_->race_ticks());
    const auto& karts = world_->karts();
    for (size_t i = 0; i < karts.size(); ++i) base.karts.push_back({karts[i], seats_[i].last_input});
    const sim::WorldState ws = world_->save();
    base.rng = ws.rng.state;
    base.boxes = ws.box_respawn;
    base.entities = ws.entities;
    for (auto& [id, c] : clients_) {
        if (!c.hello) continue;
        SnapshotMsg m = base;
        m.ack_input_tick = c.used_tick;
        m.input_lead = int8_t(std::clamp(std::lround(c.lead), -100L, 100L));
        sink_.send(id, kChannelState, encode(m), false);
    }
}

} // namespace gk::net
