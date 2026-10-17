#include "gk/net/room.h"

#include "gk/core/log.h"

#include <algorithm>

namespace gk::net {

Room::Room(sim::PhysicsTables tables, sim::PartsCatalog parts, RoomConfig config, MessageSink& sink)
    : tables_(std::move(tables)), parts_(std::move(parts)), config_(config), sink_(sink) {
    data_hash_ = hash_constants(tables_);
    config_.seats = std::clamp(config_.seats, 1, kMaxSeats);
}

int Room::human_count() const {
    int n = 0;
    for (const auto& [id, c] : clients_) n += c.hello;
    return n;
}

sim::KartParams Room::params_for(const sim::Build& b) const {
    sim::Build v = b;
    v.driver = std::clamp(v.driver, 0, int(parts_.drivers.size()) - 1);
    v.body = std::clamp(v.body, 0, int(parts_.bodies.size()) - 1);
    v.tires = std::clamp(v.tires, 0, int(parts_.tires.size()) - 1);
    v.glider = std::clamp(v.glider, 0, int(parts_.gliders.size()) - 1);
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
    if (seat >= 0 && seat < int(seats_.size())) {
        seats_[size_t(seat)].human = false;
        seats_[size_t(seat)].name = "CPU";
        if (world_) broadcast_setup();
    }
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
        c.build = h.build;
        log_info("room: {} joined", c.name);
        if (!world_ && ended_for_ < 0) {
            start_race();
        } else if (world_ && world_->countdown_left() > 0) {
            assign_seat(id, c);
            broadcast_setup();
        }
        Welcome w;
        w.seat = c.seat < 0 ? 255 : uint8_t(c.seat);
        w.server_tick = server_tick_;
        sink_.send(id, kChannelEvents, encode(w), true);
        if (world_) sink_.send(id, kChannelEvents, encode(make_setup()), true);
        break;
    }
    case MsgType::Input: {
        InputMsg m;
        if (!c.hello || c.seat < 0 || !world_ || !decode(data, m) || m.inputs.empty()) return;
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
            if (world_) world_->set_kart_params(int(s), seats_[s].params);
            c.seat = int(s);
            c.inputs.clear();
            return;
        }
    }
}

void Room::start_race() {
    ++race_id_;
    ended_for_ = -1;
    humans_done_for_ = -1;
    sim::RaceConfig rc;
    rc.engine = config_.engine;
    rc.laps = config_.laps;
    rc.countdown_frames = config_.countdown;
    world_ = std::make_unique<sim::World>(sim::make_greybox_track(), tables_, rc);
    seats_.assign(size_t(config_.seats), Seat{});
    for (int i = 0; i < config_.seats; ++i) {
        Seat& s = seats_[size_t(i)];
        s.name = "CPU";
        s.build = {i % int(parts_.drivers.size()), i % int(parts_.bodies.size()), (i / 2) % int(parts_.tires.size()), 0};
        s.params = params_for(s.build);
    }
    for (const Seat& s : seats_) world_->add_kart(s.params);
    for (auto& [id, c] : clients_) {
        c.seat = -1;
        if (c.hello) assign_seat(id, c);
    }
    step_inputs_.assign(seats_.size(), {});
    log_info("room: race {} starting with {} human(s)", race_id_, human_count());
    broadcast_setup();
}

void Room::end_race() {
    world_.reset();
    ended_for_ = 0;
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

void Room::tick() {
    ++server_tick_;
    if (!world_) {
        if (ended_for_ >= 0 && ++ended_for_ >= config_.results_ticks) {
            ended_for_ = -1;
            if (human_count() > 0 || !config_.idle_without_humans) start_race();
        } else if (ended_for_ < 0 && !config_.idle_without_humans) {
            start_race();
        }
        return;
    }
    if (config_.idle_without_humans && human_count() == 0) {
        world_.reset();
        ended_for_ = -1;
        return;
    }

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
                if (world_->racing()) ++late_inputs_;
            }
            c.last_input = in;
            c.inputs.erase(c.inputs.begin(), c.inputs.upper_bound(next));
        } else {
            in = bot_input(karts[i], world_->track(), int(i));
        }
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

    // Race flow: results once everyone is done, or a grace period after the last human.
    bool all_done = true, humans_done = true;
    for (size_t i = 0; i < seats_.size(); ++i) {
        all_done &= karts[i].finished;
        if (seats_[i].human) humans_done &= karts[i].finished;
    }
    if (humans_done && humans_done_for_ < 0) humans_done_for_ = 0;
    if (humans_done_for_ >= 0) ++humans_done_for_;
    if (all_done || humans_done_for_ >= config_.finish_grace_ticks || world_->race_ticks() > 60 * 60 * 10) {
        log_info("room: race {} over", race_id_);
        end_race();
    }
}

void Room::send_snapshots() {
    SnapshotMsg base;
    base.race_id = race_id_;
    base.tick = uint32_t(world_->tick_count());
    base.countdown_left = world_->countdown_left();
    base.race_ticks = uint32_t(world_->race_ticks());
    const auto& karts = world_->karts();
    for (size_t i = 0; i < karts.size(); ++i) base.karts.push_back({karts[i], seats_[i].last_input});
    for (auto& [id, c] : clients_) {
        if (!c.hello) continue;
        SnapshotMsg m = base;
        m.ack_input_tick = c.used_tick;
        m.input_lead = int8_t(std::clamp(std::lround(c.lead), -100L, 100L));
        sink_.send(id, kChannelState, encode(m), false);
    }
}

} // namespace gk::net
