#pragma once
#include "gk/core/math.h"
#include "gk/sim/input.h"
#include "gk/sim/kart.h"
#include "gk/sim/stats.h"
#include "gk/sim/world.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Wire protocol between gk_client and gk_server. Little-endian, versioned.
// In-race traffic runs over one UDP connection with separate lanes:
//   kChannelState    unreliable, sequenced: inputs (C->S) and snapshots (S->C)
//   kChannelEvents   reliable, ordered: join, race setup, events, results
//   kChannelChat     reliable, low priority: chat and emotes
namespace gk::net {

inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr uint16_t kDefaultPort = 7777;
inline constexpr uint8_t kChannelState = 0;
inline constexpr uint8_t kChannelEvents = 1;
inline constexpr uint8_t kChannelChat = 2;
inline constexpr int kChannelCount = 3;
inline constexpr int kMaxSeats = 12;
inline constexpr int kInputRedundancy = 8; // each input packet repeats the last N inputs to cover loss
inline constexpr int kSnapshotInterval = 2; // ticks: 30 Hz snapshots

enum class MsgType : uint8_t {
    Hello = 1,    // C->S reliable
    Welcome,      // S->C reliable
    RaceSetup,    // S->C reliable: a new race begins (also sent after Welcome)
    Input,        // C->S unreliable
    Snapshot,     // S->C unreliable
    Events,       // S->C reliable
    Chat,         // both ways, reliable low priority
};

class Writer {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void i8(int8_t v) { u8(uint8_t(v)); }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void f32(float v);
    void vec3(gk::vec3 v) { f32(v.x); f32(v.y); f32(v.z); }
    void unit_vec3(gk::vec3 v); // 3 x int16
    void quat(gk::quat q);      // smallest three, 4 bytes
    void str(const std::string& s);
    // Fixed-point helpers for bounded values
    void q16(float v, float range) { i16(int16_t(std::clamp(v / range, -1.0f, 1.0f) * 32767.0f)); }
    void q8u(float v, float range) { u8(uint8_t(std::clamp(v / range, 0.0f, 1.0f) * 255.0f + 0.5f)); }

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    uint8_t u8();
    int8_t i8() { return int8_t(u8()); }
    uint16_t u16() { uint16_t a = u8(); return uint16_t(a | (uint16_t(u8()) << 8)); }
    int16_t i16() { return int16_t(u16()); }
    uint32_t u32() { uint32_t a = u16(); return a | (uint32_t(u16()) << 16); }
    int32_t i32() { return int32_t(u32()); }
    float f32();
    gk::vec3 vec3() { float x = f32(), y = f32(), z = f32(); return {x, y, z}; }
    gk::vec3 unit_vec3();
    gk::quat quat();
    std::string str();
    float q16(float range) { return float(i16()) / 32767.0f * range; }
    float q8u(float range) { return float(u8()) / 255.0f * range; }

    bool ok() const { return ok_; }
    bool at_end() const { return pos_ == data_.size(); }

private:
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// ---------------------------------------------------------------- messages

struct SeatInfo {
    bool human = false;
    std::string name;
    sim::Build build;
    sim::KartParams params;
};

struct RaceSetup {
    uint32_t race_id = 0;
    uint32_t start_tick = 0;   // server tick at which the race world was created (tick 0 of the World)
    sim::EngineClass engine = sim::EngineClass::cc150;
    uint8_t laps = 3;
    uint16_t countdown = 180;
    uint32_t data_hash = 0;    // CRC of the server's physics tables, to catch mismatched data files
    std::vector<SeatInfo> seats;
};

struct Hello {
    uint16_t protocol = kProtocolVersion;
    std::string name;
    sim::Build build;
};

struct Welcome {
    uint8_t seat = 0;
    uint32_t server_tick = 0;
};

struct InputMsg {
    uint32_t newest_tick = 0;               // world tick the first input is for
    std::vector<sim::KartInput> inputs;     // newest first
};

struct KartSnapshot {
    sim::KartState state;
    sim::KartInput input; // the input the kart used this tick (for extrapolation)
};

struct SnapshotMsg {
    uint32_t race_id = 0;
    uint32_t tick = 0;           // world tick of this state
    int32_t countdown_left = 0;
    uint32_t race_ticks = 0;
    uint32_t ack_input_tick = 0; // newest input tick from you that the server has used
    int8_t input_lead = 0;       // how many ticks early your inputs arrive (target ~2)
    std::vector<KartSnapshot> karts;
};

struct EventsMsg {
    uint32_t race_id = 0;
    uint32_t tick = 0;
    std::vector<sim::KartEvent> events;
};

MsgType peek_type(std::span<const uint8_t> data);

std::vector<uint8_t> encode(const Hello& m);
std::vector<uint8_t> encode(const Welcome& m);
std::vector<uint8_t> encode(const RaceSetup& m);
std::vector<uint8_t> encode(const InputMsg& m);
std::vector<uint8_t> encode(const SnapshotMsg& m);
std::vector<uint8_t> encode(const EventsMsg& m);

bool decode(std::span<const uint8_t> data, Hello& m);
bool decode(std::span<const uint8_t> data, Welcome& m);
bool decode(std::span<const uint8_t> data, RaceSetup& m);
bool decode(std::span<const uint8_t> data, InputMsg& m);
bool decode(std::span<const uint8_t> data, SnapshotMsg& m);
bool decode(std::span<const uint8_t> data, EventsMsg& m);

void write_input(Writer& w, const sim::KartInput& in);
sim::KartInput read_input(Reader& r);
void write_kart(Writer& w, const sim::KartState& s);
sim::KartState read_kart(Reader& r);

// The placeholder bot driver, shared so clients predict bots exactly as the
// server drives them.
inline sim::KartInput bot_input(const sim::KartState& kart, const sim::Track& track, int seat) {
    return sim::autopilot(kart, track, 6.0f + float(seat % 4));
}

uint32_t crc32(std::span<const uint8_t> data, uint32_t crc = 0);
uint32_t hash_constants(const sim::PhysicsTables& tables);

} // namespace gk::net
