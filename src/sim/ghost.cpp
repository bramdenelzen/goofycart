#include "gk/sim/ghost.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace gk::sim {
namespace {

constexpr char kMagic[4] = {'G', 'K', 'G', 'H'};
constexpr uint8_t kVersion = 2; // 2: the energy-trade glider; older runs no longer replay

int8_t to_i8(float v) { return int8_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f)); }

struct Writer {
    std::vector<uint8_t>& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
    }
    void str(const std::string& s) {
        u8(uint8_t(std::min<size_t>(s.size(), 255)));
        out.insert(out.end(), s.begin(), s.begin() + std::min<ptrdiff_t>(ptrdiff_t(s.size()), 255));
    }
};

struct Reader {
    std::span<const uint8_t> in;
    size_t at = 0;
    bool ok = true;
    uint8_t u8() {
        if (at >= in.size()) return ok = false, 0;
        return in[at++];
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(u8()) << (8 * i);
        return v;
    }
    std::string str() {
        const size_t n = u8();
        if (at + n > in.size()) return ok = false, std::string();
        std::string s(in.begin() + ptrdiff_t(at), in.begin() + ptrdiff_t(at + n));
        at += n;
        return s;
    }
};

} // namespace

KartInput quantize(const KartInput& in) {
    KartInput q = in;
    q.steer = float(to_i8(in.steer)) / 127.0f;
    q.pitch = float(to_i8(in.pitch)) / 127.0f;
    return q;
}

std::vector<uint8_t> Ghost::encode() const {
    std::vector<uint8_t> out;
    Writer w{out};
    for (char c : kMagic) w.u8(uint8_t(c));
    w.u8(kVersion);
    w.str(track);
    w.str(name);
    w.u8(uint8_t(engine));
    w.u8(uint8_t(laps));
    for (int v : {build.driver, build.body, build.tires, build.glider}) w.u8(uint8_t(v));
    w.u32(uint32_t(finish_tick));
    w.u32(uint32_t(inputs.size()));
    out.reserve(out.size() + inputs.size() * 3);
    for (const KartInput& in : inputs) {
        w.u8(uint8_t(to_i8(in.steer)));
        w.u8(uint8_t(to_i8(in.pitch)));
        w.u8(uint8_t(in.accelerate | in.brake << 1 | in.drift << 2 | in.item << 3 | in.item_back << 4));
    }
    return out;
}

bool Ghost::decode(std::span<const uint8_t> data, Ghost& out) {
    Reader r{data};
    for (char c : kMagic)
        if (r.u8() != uint8_t(c)) return false;
    if (r.u8() != kVersion) return false;
    Ghost g;
    g.track = r.str();
    g.name = r.str();
    g.engine = EngineClass(std::min<uint8_t>(r.u8(), 3));
    g.laps = std::max<int>(1, r.u8());
    g.build = {r.u8(), r.u8(), r.u8(), r.u8()};
    g.finish_tick = int(r.u32());
    const uint32_t n = r.u32();
    if (!r.ok || n > 60u * 60u * 30u || r.at + size_t(n) * 3 != data.size()) return false; // at most 30 minutes
    g.inputs.resize(n);
    for (KartInput& in : g.inputs) {
        in.steer = float(int8_t(r.u8())) / 127.0f;
        in.pitch = float(int8_t(r.u8())) / 127.0f;
        const uint8_t f = r.u8();
        in.accelerate = f & 1, in.brake = f >> 1 & 1, in.drift = f >> 2 & 1, in.item = f >> 3 & 1, in.item_back = f >> 4 & 1;
    }
    out = std::move(g);
    return r.ok;
}

bool save_ghost(const std::string& path, const Ghost& g) {
    const auto bytes = g.encode();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(f);
}

bool load_ghost(const std::string& path, Ghost& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return Ghost::decode(bytes, out);
}

World make_time_trial_world(Track track, const PhysicsTables& tables, const RaceConfig& rc, const KartParams& params) {
    track.item_boxes.clear();
    World w(std::move(track), tables, rc);
    w.add_kart(params);
    w.give_item(0, Item::TripleMushroom);
    return w;
}

GhostRunner::GhostRunner(Track track, const PhysicsTables& tables, const RaceConfig& rc, const KartParams& params,
                         const Ghost* replay)
    : world_(std::make_unique<World>(make_time_trial_world(std::move(track), tables, rc, params))) {
    if (replay) replay_ = *replay;
}

void GhostRunner::tick() {
    KartInput in;
    if (replaying()) {
        if (size_t(world_->tick_count()) < replay_.inputs.size()) in = replay_.inputs[size_t(world_->tick_count())];
    } else if (!kart().finished) {
        in = quantize(cpu_input(*world_, 0, cpu_skill(CpuLevel::Hard, 0)));
    }
    world_->tick(std::span(&in, 1));
}

bool ghost_still_valid(const Ghost& g, Track track, const PhysicsTables& tables, const KartParams& params) {
    if (g.finish_tick <= 0) return false;
    RaceConfig rc;
    rc.engine = g.engine;
    rc.laps = g.laps;
    GhostRunner run(std::move(track), tables, rc, params, &g);
    for (size_t i = 0; i < g.inputs.size() && !run.kart().finished; ++i) run.tick();
    return run.kart().finished && run.kart().finish_tick == g.finish_tick;
}

} // namespace gk::sim
