#include "gk/net/protocol.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iterator>

namespace gk::net {

void Writer::f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    u32(bits);
}

float Reader::f32() {
    const uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

uint8_t Reader::u8() {
    if (pos_ >= data_.size()) {
        ok_ = false;
        return 0;
    }
    return data_[pos_++];
}

void Writer::str(const std::string& s) {
    const size_t n = std::min<size_t>(s.size(), 255);
    u8(uint8_t(n));
    buf_.insert(buf_.end(), s.begin(), s.begin() + long(n));
}

std::string Reader::str() {
    const uint8_t n = u8();
    std::string s;
    for (uint8_t i = 0; i < n && ok_; ++i) s.push_back(char(u8()));
    return s;
}

void Writer::unit_vec3(gk::vec3 v) {
    q16(v.x, 1.0f);
    q16(v.y, 1.0f);
    q16(v.z, 1.0f);
}

gk::vec3 Reader::unit_vec3() {
    gk::vec3 v{q16(1.0f), q16(1.0f), q16(1.0f)};
    const float l = glm::length(v);
    return l > 1e-6f ? v / l : gk::vec3(0, 1, 0);
}

// Smallest-three quaternion: 2 bits for the dropped component, 10 bits for each other.
void Writer::quat(gk::quat q) {
    q = glm::normalize(q);
    const float c[4] = {q.x, q.y, q.z, q.w};
    int largest = 0;
    for (int i = 1; i < 4; ++i)
        if (std::abs(c[i]) > std::abs(c[largest])) largest = i;
    const float sign = c[largest] < 0.0f ? -1.0f : 1.0f;
    uint32_t packed = uint32_t(largest) << 30;
    int shift = 20;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        const float v = std::clamp(c[i] * sign / 0.70710678f, -1.0f, 1.0f);
        packed |= uint32_t(std::lround((v + 1.0f) * 0.5f * 1023.0f)) << shift;
        shift -= 10;
    }
    u32(packed);
}

gk::quat Reader::quat() {
    const uint32_t packed = u32();
    const int largest = int(packed >> 30);
    float c[4];
    float sum = 0.0f;
    int shift = 20;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        const float v = float((packed >> shift) & 1023u) / 1023.0f * 2.0f - 1.0f;
        c[i] = v * 0.70710678f;
        sum += c[i] * c[i];
        shift -= 10;
    }
    c[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
    return glm::normalize(gk::quat(c[3], c[0], c[1], c[2]));
}

MsgType peek_type(std::span<const uint8_t> data) { return data.empty() ? MsgType(0) : MsgType(data[0]); }

// ---------------------------------------------------------------- inputs and karts

void write_input(Writer& w, const sim::KartInput& in) {
    w.i8(int8_t(std::lround(std::clamp(in.steer, -1.0f, 1.0f) * 127.0f)));
    w.i8(int8_t(std::lround(std::clamp(in.pitch, -1.0f, 1.0f) * 127.0f)));
    w.u8(uint8_t(in.accelerate) | uint8_t(in.brake) << 1 | uint8_t(in.drift) << 2 | uint8_t(in.item) << 3 |
         uint8_t(in.item_back) << 4);
}

sim::KartInput read_input(Reader& r) {
    sim::KartInput in;
    in.steer = float(r.i8()) / 127.0f;
    in.pitch = float(r.i8()) / 127.0f;
    const uint8_t b = r.u8();
    in.accelerate = b & 1;
    in.brake = b & 2;
    in.drift = b & 4;
    in.item = b & 8;
    in.item_back = b & 16;
    return in;
}

// Quantised kart state, about 55 bytes. Small rounding differences are fine:
// the server corrects the client, so bit-exact determinism is not needed.
void write_kart(Writer& w, const sim::KartState& s) {
    w.vec3(s.pos);
    w.quat(s.rot);
    w.q16(s.speed, 80.0f);
    w.q16(s.slide.x, 80.0f);
    w.q16(s.slide.y, 80.0f);
    w.q16(s.slide.z, 80.0f);
    const uint16_t flags = uint16_t(s.grounded) | uint16_t(s.drift_button_prev) << 1 |
                           uint16_t(s.boost_ignores_offroad) << 2 | uint16_t(s.trick_done) << 3 |
                           uint16_t(s.glide_armed) << 4 | uint16_t(s.gliding) << 5 | uint16_t(s.finished) << 6 |
                           uint16_t(s.item_button_prev) << 7 | uint16_t(s.trailing) << 8 |
                           uint16_t(s.item != sim::Item::None || s.item_next != sim::Item::None || s.roulette > 0) << 9;
    w.u16(flags);
    if (!s.grounded) {
        w.q16(s.air_vel.x, 120.0f);
        w.q16(s.air_vel.y, 120.0f);
        w.q16(s.air_vel.z, 120.0f);
    }
    w.u8(uint8_t(uint8_t(s.drift) | uint8_t(s.drift_dir + 1) << 2 | uint8_t(s.surface) << 4));
    w.u8(uint8_t(uint8_t(s.boost) | uint8_t(s.last_ground_surface) << 4));
    w.u8(uint8_t(std::min(s.air_frames, 255)));
    w.u16(uint16_t(std::clamp(s.mt_charge * 16.0f, 0.0f, 65535.0f)));
    w.i8(int8_t(std::lround(std::clamp(s.hop_steer, -1.0f, 1.0f) * 127.0f)));
    w.u8(s.drift_frames);
    w.u16(uint16_t(std::clamp(s.stuck_frames, 0, 65535)));
    w.u16(uint16_t(std::clamp(s.drift_turn * 20000.0f, 0.0f, 65535.0f)));
    w.u8(uint8_t(std::clamp(s.boost_frames, 0, 255)));
    w.u16(uint16_t(std::clamp(s.boost_mult * 1000.0f, 0.0f, 65535.0f)));
    w.u8(uint8_t(std::clamp(s.trick_window, 0, 255)));
    w.q8u(s.trick_anim, 1.0f);
    w.u8(uint8_t(std::clamp(s.respawn_frames, 0, 255)));
    w.u8(uint8_t(std::clamp(s.invincible_frames, 0, 255)));
    w.u8(uint8_t(std::clamp(s.stun_frames, 0, 255)));
    w.u16(uint16_t(s.last_safe_sample));
    w.u16(uint16_t(s.sample));
    w.u8(uint8_t(s.checkpoint));
    w.u8(uint8_t(s.lap));
    if (s.finished) w.u32(uint32_t(s.finish_tick));
    w.u8(uint8_t(s.coins));
    w.u8(uint8_t(std::clamp(s.accel_hold_frames, 0, 255)));
    w.q16(s.visual_drift_yaw, 1.0f);
    w.q8u(s.wall_bump, 1.0f);
    if (flags & (1u << 9)) {
        w.u8(uint8_t(s.item));
        w.u8(uint8_t(s.item_next));
        w.u8(s.item_uses);
        w.u8(uint8_t(std::clamp(s.roulette, 0, 255)));
    }
    w.u8(s.lucky_index);
    // Item effect timers: a mask of the running ones, then each as u16.
    const int timers[] = {s.spin_frames, s.tumble_frames, s.shrink_frames, s.star_frames, s.bullet_frames, s.ghost_frames,
                          s.ink_frames, s.golden_frames, s.fire_frames, s.chomper_frames, s.chomper_cooldown};
    uint16_t mask = 0;
    for (size_t i = 0; i < std::size(timers); ++i)
        if (timers[i] > 0) mask |= uint16_t(1u << i);
    w.u16(mask);
    for (size_t i = 0; i < std::size(timers); ++i)
        if (mask & (1u << i)) w.u16(uint16_t(std::clamp(timers[i], 0, 65535)));
}

sim::KartState read_kart(Reader& r) {
    sim::KartState s;
    s.pos = r.vec3();
    s.rot = r.quat();
    s.speed = r.q16(80.0f);
    s.slide.x = r.q16(80.0f);
    s.slide.y = r.q16(80.0f);
    s.slide.z = r.q16(80.0f);
    const uint16_t flags = r.u16();
    s.grounded = flags & 1;
    s.drift_button_prev = flags & 2;
    s.boost_ignores_offroad = flags & 4;
    s.trick_done = flags & 8;
    s.glide_armed = flags & 16;
    s.gliding = flags & 32;
    s.finished = flags & 64;
    if (!s.grounded) {
        s.air_vel.x = r.q16(120.0f);
        s.air_vel.y = r.q16(120.0f);
        s.air_vel.z = r.q16(120.0f);
    }
    const uint8_t a = r.u8();
    s.drift = sim::DriftState(a & 3);
    s.drift_dir = int8_t(int((a >> 2) & 3) - 1);
    s.surface = sim::Surface(std::min<int>(a >> 4, int(sim::Surface::Count) - 1));
    const uint8_t b = r.u8();
    s.boost = sim::BoostKind(b & 15);
    s.last_ground_surface = sim::Surface(std::min<int>(b >> 4, int(sim::Surface::Count) - 1));
    s.air_frames = r.u8();
    s.mt_charge = float(r.u16()) / 16.0f;
    s.hop_steer = float(r.i8()) / 127.0f;
    s.drift_frames = r.u8();
    s.stuck_frames = r.u16();
    s.drift_turn = float(r.u16()) / 20000.0f;
    s.boost_frames = r.u8();
    s.boost_mult = float(r.u16()) / 1000.0f;
    s.trick_window = r.u8();
    s.trick_anim = r.q8u(1.0f);
    s.respawn_frames = r.u8();
    s.invincible_frames = r.u8();
    s.stun_frames = r.u8();
    s.last_safe_sample = r.u16();
    s.sample = r.u16();
    s.checkpoint = r.u8();
    s.lap = r.u8();
    if (s.finished) s.finish_tick = int(r.u32());
    s.coins = r.u8();
    s.accel_hold_frames = r.u8();
    s.visual_drift_yaw = r.q16(1.0f);
    s.wall_bump = r.q8u(1.0f);
    s.item_button_prev = flags & (1u << 7);
    s.trailing = flags & (1u << 8);
    if (flags & (1u << 9)) {
        s.item = sim::Item(std::min<int>(r.u8(), int(sim::Item::Count) - 1));
        s.item_next = sim::Item(std::min<int>(r.u8(), int(sim::Item::Count) - 1));
        s.item_uses = r.u8();
        s.roulette = r.u8();
    }
    s.lucky_index = r.u8();
    int* timers[] = {&s.spin_frames, &s.tumble_frames, &s.shrink_frames, &s.star_frames, &s.bullet_frames, &s.ghost_frames,
                     &s.ink_frames, &s.golden_frames, &s.fire_frames, &s.chomper_frames, &s.chomper_cooldown};
    const uint16_t mask = r.u16();
    for (size_t i = 0; i < std::size(timers); ++i)
        if (mask & (1u << i)) *timers[i] = r.u16();
    s.ground_normal = s.up();
    return s;
}

void write_entity(Writer& w, const sim::ItemEntity& e) {
    w.u8(uint8_t(e.type));
    w.i8(e.owner);
    w.i8(e.target);
    w.u8(e.bounces);
    w.u8(uint8_t(uint8_t(e.grounded) | uint8_t(e.returning) << 1));
    w.vec3(e.pos);
    w.q16(e.vel.x, 100.0f);
    w.q16(e.vel.y, 100.0f);
    w.q16(e.vel.z, 100.0f);
    w.i16(e.sample);
    w.i16(e.age);
    w.i16(e.life);
}

sim::ItemEntity read_entity(Reader& r) {
    sim::ItemEntity e;
    e.type = sim::EntityType(std::min<int>(r.u8(), int(sim::EntityType::Explosion)));
    e.owner = r.i8();
    e.target = r.i8();
    e.bounces = r.u8();
    const uint8_t f = r.u8();
    e.grounded = f & 1;
    e.returning = f & 2;
    e.pos = r.vec3();
    e.vel.x = r.q16(100.0f);
    e.vel.y = r.q16(100.0f);
    e.vel.z = r.q16(100.0f);
    e.sample = r.i16();
    e.age = r.i16();
    e.life = r.i16();
    return e;
}

// ---------------------------------------------------------------- params

namespace {
void write_params(Writer& w, const sim::KartParams& p) {
    for (float f : {p.top_speed, p.top_speed_water, p.glide_speed, p.accel, p.mass, p.turn_rate, p.turn_rate_water,
                    p.turn_rate_air, p.sand_grip, p.ice_grip, p.slip_traction, p.mt_charge, p.mt_boost})
        w.f32(f);
    w.u16(uint16_t(p.invincibility_frames));
    w.u8(uint8_t(p.drift));
    w.u8(uint8_t(p.is_bike));
    w.f32(p.rubber_band);
}

sim::KartParams read_params(Reader& r) {
    sim::KartParams p;
    for (float* f : {&p.top_speed, &p.top_speed_water, &p.glide_speed, &p.accel, &p.mass, &p.turn_rate,
                     &p.turn_rate_water, &p.turn_rate_air, &p.sand_grip, &p.ice_grip, &p.slip_traction, &p.mt_charge,
                     &p.mt_boost})
        *f = r.f32();
    p.invincibility_frames = r.u16();
    p.drift = sim::DriftType(r.u8() & 1);
    p.is_bike = r.u8() != 0;
    p.rubber_band = r.f32();
    return p;
}

void write_build(Writer& w, const sim::Build& b) {
    w.u8(uint8_t(b.driver));
    w.u8(uint8_t(b.body));
    w.u8(uint8_t(b.tires));
    w.u8(uint8_t(b.glider));
}

sim::Build read_build(Reader& r) {
    sim::Build b;
    b.driver = r.u8();
    b.body = r.u8();
    b.tires = r.u8();
    b.glider = r.u8();
    return b;
}

bool expect(Reader& r, MsgType t) { return r.u8() == uint8_t(t); }
} // namespace

// ---------------------------------------------------------------- encode / decode

std::vector<uint8_t> encode(const Hello& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Hello));
    w.u16(m.protocol);
    w.str(m.name);
    write_build(w, m.build);
    w.str(m.token);
    return w.take();
}

bool decode(std::span<const uint8_t> d, Hello& m) {
    Reader r(d);
    if (!expect(r, MsgType::Hello)) return false;
    m.protocol = r.u16();
    m.name = r.str();
    m.build = read_build(r);
    m.token = r.str();
    return r.ok();
}

std::vector<uint8_t> encode(const Welcome& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Welcome));
    w.u8(m.seat);
    w.u32(m.server_tick);
    return w.take();
}

bool decode(std::span<const uint8_t> d, Welcome& m) {
    Reader r(d);
    if (!expect(r, MsgType::Welcome)) return false;
    m.seat = r.u8();
    m.server_tick = r.u32();
    return r.ok();
}

std::vector<uint8_t> encode(const RaceSetup& m) {
    Writer w;
    w.u8(uint8_t(MsgType::RaceSetup));
    w.u32(m.race_id);
    w.u32(m.start_tick);
    w.u8(uint8_t(m.engine));
    w.u8(m.laps);
    w.u16(m.countdown);
    w.u8(uint8_t(m.cpu_level));
    w.str(m.track);
    w.u32(m.data_hash);
    w.u8(uint8_t(m.seats.size()));
    for (const auto& s : m.seats) {
        w.u8(uint8_t(s.human));
        w.str(s.name);
        write_build(w, s.build);
        write_params(w, s.params);
    }
    return w.take();
}

bool decode(std::span<const uint8_t> d, RaceSetup& m) {
    Reader r(d);
    if (!expect(r, MsgType::RaceSetup)) return false;
    m.race_id = r.u32();
    m.start_tick = r.u32();
    m.engine = sim::EngineClass(std::min<int>(r.u8(), 3));
    m.laps = r.u8();
    m.countdown = r.u16();
    m.cpu_level = sim::CpuLevel(std::min<int>(r.u8(), 2));
    m.track = r.str();
    m.data_hash = r.u32();
    const uint8_t n = r.u8();
    if (n > kMaxSeats) return false;
    m.seats.resize(n);
    for (auto& s : m.seats) {
        s.human = r.u8() != 0;
        s.name = r.str();
        s.build = read_build(r);
        s.params = read_params(r);
    }
    return r.ok();
}

std::vector<uint8_t> encode(const InputMsg& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Input));
    w.u32(m.newest_tick);
    w.u8(uint8_t(std::min<size_t>(m.inputs.size(), 32)));
    for (size_t i = 0; i < m.inputs.size() && i < 32; ++i) write_input(w, m.inputs[i]);
    return w.take();
}

bool decode(std::span<const uint8_t> d, InputMsg& m) {
    Reader r(d);
    if (!expect(r, MsgType::Input)) return false;
    m.newest_tick = r.u32();
    const uint8_t n = r.u8();
    if (n > 32) return false;
    m.inputs.resize(n);
    for (auto& in : m.inputs) in = read_input(r);
    return r.ok();
}

std::vector<uint8_t> encode(const SnapshotMsg& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Snapshot));
    w.u32(m.race_id);
    w.u32(m.tick);
    w.i32(m.countdown_left);
    w.u32(m.race_ticks);
    w.u32(m.ack_input_tick);
    w.i8(m.input_lead);
    w.u8(uint8_t(m.karts.size()));
    for (const auto& k : m.karts) {
        write_kart(w, k.state);
        write_input(w, k.input);
    }
    w.u32(uint32_t(m.rng));
    w.u32(uint32_t(m.rng >> 32));
    w.u8(uint8_t(std::min<size_t>(m.boxes.size(), 255)));
    for (size_t i = 0; i < m.boxes.size() && i < 255; ++i) w.u8(uint8_t(std::clamp(m.boxes[i], 0, 255)));
    w.u8(uint8_t(std::min<size_t>(m.entities.size(), 255)));
    for (size_t i = 0; i < m.entities.size() && i < 255; ++i) write_entity(w, m.entities[i]);
    return w.take();
}

bool decode(std::span<const uint8_t> d, SnapshotMsg& m) {
    Reader r(d);
    if (!expect(r, MsgType::Snapshot)) return false;
    m.race_id = r.u32();
    m.tick = r.u32();
    m.countdown_left = r.i32();
    m.race_ticks = r.u32();
    m.ack_input_tick = r.u32();
    m.input_lead = r.i8();
    const uint8_t n = r.u8();
    if (n > kMaxSeats) return false;
    m.karts.resize(n);
    for (auto& k : m.karts) {
        k.state = read_kart(r);
        k.input = read_input(r);
    }
    const uint64_t lo = r.u32();
    m.rng = lo | (uint64_t(r.u32()) << 32);
    m.boxes.resize(r.u8());
    for (int& b : m.boxes) b = r.u8();
    m.entities.resize(r.u8());
    for (auto& e : m.entities) e = read_entity(r);
    return r.ok();
}

std::vector<uint8_t> encode(const EventsMsg& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Events));
    w.u32(m.race_id);
    w.u32(m.tick);
    w.u8(uint8_t(std::min<size_t>(m.events.size(), 255)));
    for (size_t i = 0; i < m.events.size() && i < 255; ++i) {
        w.u8(uint8_t(m.events[i].type));
        w.u8(uint8_t(m.events[i].kart));
        w.i32(m.events[i].value);
    }
    return w.take();
}

bool decode(std::span<const uint8_t> d, EventsMsg& m) {
    Reader r(d);
    if (!expect(r, MsgType::Events)) return false;
    m.race_id = r.u32();
    m.tick = r.u32();
    const uint8_t n = r.u8();
    m.events.resize(n);
    for (auto& e : m.events) {
        e.type = sim::KartEventType(r.u8());
        e.kart = r.u8();
        e.value = r.i32();
    }
    return r.ok();
}

std::vector<uint8_t> encode(const LobbyMsg& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Lobby));
    w.u8(uint8_t(m.phase));
    w.u16(m.seconds_left);
    w.u8(m.race_number);
    w.u8(m.races_per_gp);
    w.str(m.cup);
    w.str(m.track);
    w.u8(uint8_t(std::min<size_t>(m.players.size(), 32)));
    for (size_t i = 0; i < m.players.size() && i < 32; ++i) {
        const LobbyPlayer& p = m.players[i];
        w.str(p.name);
        write_build(w, p.build);
        w.u8(uint8_t(uint8_t(p.ready) | uint8_t(p.you) << 1));
        w.u16(p.points);
    }
    return w.take();
}

bool decode(std::span<const uint8_t> d, LobbyMsg& m) {
    Reader r(d);
    if (!expect(r, MsgType::Lobby)) return false;
    m.phase = RoomPhase(std::min<int>(r.u8(), int(RoomPhase::Results)));
    m.seconds_left = r.u16();
    m.race_number = r.u8();
    m.races_per_gp = r.u8();
    m.cup = r.str();
    m.track = r.str();
    const uint8_t n = r.u8();
    if (n > 32) return false;
    m.players.resize(n);
    for (auto& p : m.players) {
        p.name = r.str();
        p.build = read_build(r);
        const uint8_t f = r.u8();
        p.ready = f & 1;
        p.you = f & 2;
        p.points = r.u16();
    }
    return r.ok();
}

std::vector<uint8_t> encode(const SetBuildMsg& m) {
    Writer w;
    w.u8(uint8_t(MsgType::SetBuild));
    write_build(w, m.build);
    w.u8(uint8_t(m.ready));
    return w.take();
}

bool decode(std::span<const uint8_t> d, SetBuildMsg& m) {
    Reader r(d);
    if (!expect(r, MsgType::SetBuild)) return false;
    m.build = read_build(r);
    m.ready = r.u8() != 0;
    return r.ok();
}

std::vector<uint8_t> encode(const ResultsMsg& m) {
    Writer w;
    w.u8(uint8_t(MsgType::Results));
    w.u32(m.race_id);
    w.u8(m.race_number);
    w.u8(m.races_per_gp);
    w.u8(uint8_t(std::min<size_t>(m.entries.size(), kMaxSeats)));
    for (size_t i = 0; i < m.entries.size() && i < size_t(kMaxSeats); ++i) {
        const ResultEntry& e = m.entries[i];
        w.u8(e.seat);
        w.u8(uint8_t(e.human));
        w.str(e.name);
        w.u32(e.finish_ticks);
        w.u8(e.points);
        w.u16(e.total);
    }
    return w.take();
}

bool decode(std::span<const uint8_t> d, ResultsMsg& m) {
    Reader r(d);
    if (!expect(r, MsgType::Results)) return false;
    m.race_id = r.u32();
    m.race_number = r.u8();
    m.races_per_gp = r.u8();
    const uint8_t n = r.u8();
    if (n > kMaxSeats) return false;
    m.entries.resize(n);
    for (auto& e : m.entries) {
        e.seat = r.u8();
        e.human = r.u8() != 0;
        e.name = r.str();
        e.finish_ticks = r.u32();
        e.points = r.u8();
        e.total = r.u16();
    }
    return r.ok();
}

uint32_t crc32(std::span<const uint8_t> data, uint32_t crc) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (uint8_t b : data) crc = table[(crc ^ b) & 0xff] ^ (crc >> 8);
    return ~crc;
}

uint32_t hash_constants(const sim::PhysicsTables& t) {
    const auto& k = t.constants;
    Writer w;
    for (float f : {k.gravity, k.hop_velocity, k.min_drift_speed, k.brake_decel, k.coast_decel, k.reverse_top,
                    k.reverse_accel, k.overspeed_decel, k.slope_factor, k.road_slip_retention, k.drift_slip_retention,
                    k.drift_entry_slip_retention, k.drift_turn_wide, k.drift_turn_neutral, k.drift_turn_tight,
                    k.drift_turn_start, k.drift_turn_response, k.drift_inward_bonus,
                    k.coin_speed_bonus, k.charge_fast, k.charge_slow, k.spark_threshold, k.mt_thresholds[0],
                    k.mt_thresholds[1], k.mt_thresholds[2], k.mt_boost_mult, k.offroad_light, k.offroad_medium,
                    k.offroad_heavy, k.boost_pad_mult, k.mushroom_mult, k.trick_mult, k.rocket_mult, k.boost_accel,
                    k.glide_launch_velocity, k.glide_sink, k.glide_pitch_speed})
        w.f32(f);
    for (int i : {k.max_coins, k.mt_frames[0], k.mt_frames[1], k.mt_frames[2], k.boost_pad_frames, k.mushroom_frames,
                  k.trick_frames, k.rocket_frames, k.rocket_window_early, k.rocket_window_late, k.burnout_frames,
                  k.trick_window_frames, k.respawn_frames, k.drift_entry_frames, k.stuck_respawn_frames})
        w.i32(i);
    return crc32(w.data());
}

} // namespace gk::net
