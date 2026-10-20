#include "gk/sim/stats.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace gk::sim {
namespace {
using json = nlohmann::json;

constexpr const char* kStatKeys[kStatCount] = {
    "speed_ground",    "speed_water",    "speed_air",    "acceleration", "weight",        "handling_ground",
    "handling_water",  "handling_air",   "traction",     "mini_turbo",   "invincibility",
};

bool read_json(const std::string& path, json& out, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    try {
        out = json::parse(f, nullptr, true, true);
    } catch (const std::exception& e) {
        if (error) *error = path + ": " + e.what();
        return false;
    }
    return true;
}

LevelCurve curve_from(const json& j) {
    LevelCurve c;
    for (const auto& p : j.at("points")) c.points.emplace_back(p.at(0).get<float>(), p.at(1).get<float>());
    c.scale = j.value("scale", 1.0f);
    c.source = j.value("source", "");
    return c;
}

LevelCurve linear(float v0, float v20) { return LevelCurve{{{0.0f, v0}, {20.0f, v20}}, 1.0f, "builtin"}; }

// Constants may be a bare number or {"value": ..., "source": ...}.
const json& value_of(const json& j) { return j.is_object() && j.contains("value") ? j.at("value") : j; }

template <class T>
void read_const(const json& c, const char* key, T& out) {
    if (c.contains(key)) out = value_of(c.at(key)).get<T>();
}
} // namespace

const char* stat_key(Stat s) { return kStatKeys[static_cast<int>(s)]; }

const char* surface_name(Surface s) {
    switch (s) {
    case Surface::Road: return "road";
    case Surface::OffroadLight: return "offroad_light";
    case Surface::OffroadMedium: return "offroad_medium";
    case Surface::OffroadHeavy: return "offroad_heavy";
    case Surface::Sand: return "sand";
    case Surface::Ice: return "ice";
    case Surface::Water: return "water";
    case Surface::BoostPad: return "boost_pad";
    case Surface::GlideRamp: return "glide_ramp";
    case Surface::TrickRamp: return "trick_ramp";
    case Surface::Wall: return "wall";
    case Surface::OutOfBounds: return "out_of_bounds";
    case Surface::Oil: return "oil";
    case Surface::Count: break;
    }
    return "?";
}

float LevelCurve::at(int level) const {
    if (points.empty()) return 0.0f;
    const float l = float(std::clamp(level, 0, kMaxLevel));
    if (l <= points.front().first) return points.front().second * scale;
    for (size_t i = 1; i < points.size(); ++i) {
        if (l <= points[i].first) {
            const auto [l0, v0] = points[i - 1];
            const auto [l1, v1] = points[i];
            const float t = l1 > l0 ? (l - l0) / (l1 - l0) : 1.0f;
            return (v0 + (v1 - v0) * t) * scale;
        }
    }
    return points.back().second * scale;
}

// ---------------------------------------------------------------- parts

bool PartsCatalog::load(const std::string& path, std::string* error) {
    json j;
    if (!read_json(path, j, error)) return false;
    try {
        auto read_list = [](const json& arr, std::vector<Part>& out) {
            out.clear();
            for (const auto& e : arr) {
                Part p;
                p.id = e.at("id").get<std::string>();
                p.name = e.value("name", p.id);
                const auto& pts = e.at("points");
                for (int i = 0; i < kStatCount && i < int(pts.size()); ++i) p.points[i] = pts[i].get<int>();
                p.drift = e.value("drift", "outward") == "inward" ? DriftType::Inward : DriftType::Outward;
                p.is_bike = e.value("bike", false);
                if (const auto it = e.find("look"); it != e.end()) {
                    auto hex = [](const std::string& h, uint32_t fallback) {
                        return h.size() == 7 && h[0] == '#' ? uint32_t(std::stoul(h.substr(1), nullptr, 16)) : fallback;
                    };
                    p.look.shape = it->value("shape", "");
                    p.look.color = hex(it->value("color", ""), p.look.color);
                    p.look.color2 = hex(it->value("color2", ""), p.look.color2);
                    p.look.size = it->value("size", 1.0f);
                }
                out.push_back(std::move(p));
            }
        };
        read_list(j.at("drivers"), drivers);
        read_list(j.at("bodies"), bodies);
        read_list(j.at("tires"), tires);
        read_list(j.at("gliders"), gliders);
    } catch (const std::exception& e) {
        if (error) *error = path + ": " + e.what();
        return false;
    }
    if (drivers.empty() || bodies.empty() || tires.empty() || gliders.empty()) {
        if (error) *error = path + ": every part list needs at least one entry";
        return false;
    }
    return true;
}

PartsCatalog PartsCatalog::builtin() {
    PartsCatalog c;
    Part mid{"gobby", "Gobby", {5, 5, 5, 4, 5, 5, 5, 5, 4, 3, 4}};
    Part two{"standard", "Standard", {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}};
    Part one{"wing", "Wing", {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}};
    c.drivers = {mid};
    c.bodies = {two};
    c.tires = {two};
    c.gliders = {one};
    return c;
}

StatArray PartsCatalog::levels(const Build& b) const {
    StatArray out{};
    const Part* parts[4] = {&drivers.at(b.driver), &bodies.at(b.body), &tires.at(b.tires), &gliders.at(b.glider)};
    for (int s = 0; s < kStatCount; ++s) {
        int sum = 0;
        for (const Part* p : parts) sum += p->points[s];
        out[s] = std::clamp(sum, 0, kMaxLevel);
    }
    return out;
}

// ---------------------------------------------------------------- physics tables

PhysicsTables PhysicsTables::builtin() {
    PhysicsTables t;
    auto& c = t.curves;
    c[int(Stat::SpeedGround)] = linear(19.5f, 27.0f);
    c[int(Stat::SpeedWater)] = linear(19.5f, 27.0f); // as on the ground: parts make the difference
    c[int(Stat::SpeedAir)] = linear(20.0f, 27.0f);
    c[int(Stat::Acceleration)] = LevelCurve{{{0, 0.035f}, {1, 0.035f}, {10, 0.08f}, {20, 0.13f}}, 105.0f, "builtin"};
    c[int(Stat::Weight)] = LevelCurve{{{0, 8.0f}, {1, 8.0f}, {10, 22.5f}, {20, 60.0f}}, 1.0f, "builtin"};
    c[int(Stat::HandlingGround)] = linear(1.45f, 2.6f);
    c[int(Stat::HandlingWater)] = linear(1.35f, 2.45f);
    c[int(Stat::HandlingAir)] = linear(1.1f, 2.0f);
    c[int(Stat::Traction)] = linear(0.0f, 1.0f);
    c[int(Stat::MiniTurbo)] = linear(0.85f, 1.25f);
    c[int(Stat::Invincibility)] = linear(40.0f, 100.0f);
    t.sand_grip = LevelCurve{{{0, 0.981f}, {1, 0.981f}, {20, 1.0f}}, 1.0f, "builtin"};
    t.ice_grip = LevelCurve{{{0, 0.971f}, {1, 0.971f}, {20, 0.99f}}, 1.0f, "builtin"};
    t.mt_boost = linear(0.8f, 1.3f);
    return t;
}

bool PhysicsTables::load(const std::string& path, std::string* error) {
    json j;
    if (!read_json(path, j, error)) return false;
    PhysicsTables t = builtin();
    try {
        if (j.contains("engine_class_speed")) {
            const auto& e = j.at("engine_class_speed");
            const char* keys[4] = {"50cc", "100cc", "150cc", "200cc"};
            for (int i = 0; i < 4; ++i)
                if (e.contains(keys[i])) t.cc_speed[i] = e.at(keys[i]).get<float>();
        }
        const auto& cj = j.at("curves");
        for (int s = 0; s < kStatCount; ++s)
            if (cj.contains(kStatKeys[s])) t.curves[s] = curve_from(cj.at(kStatKeys[s]));
        if (j.contains("extra_curves")) {
            const auto& x = j.at("extra_curves");
            if (x.contains("sand_grip")) t.sand_grip = curve_from(x.at("sand_grip"));
            if (x.contains("ice_grip")) t.ice_grip = curve_from(x.at("ice_grip"));
            if (x.contains("mt_boost")) t.mt_boost = curve_from(x.at("mt_boost"));
        }
        if (j.contains("constants")) {
            const auto& c = j.at("constants");
            auto& k = t.constants;
            read_const(c, "gravity", k.gravity);
            read_const(c, "hop_velocity", k.hop_velocity);
            read_const(c, "min_drift_speed", k.min_drift_speed);
            read_const(c, "brake_decel", k.brake_decel);
            read_const(c, "coast_decel", k.coast_decel);
            read_const(c, "reverse_top", k.reverse_top);
            read_const(c, "reverse_accel", k.reverse_accel);
            read_const(c, "overspeed_decel", k.overspeed_decel);
            read_const(c, "slope_factor", k.slope_factor);
            read_const(c, "road_slip_retention", k.road_slip_retention);
            read_const(c, "drift_slip_retention", k.drift_slip_retention);
            read_const(c, "drift_entry_frames", k.drift_entry_frames);
            read_const(c, "stuck_respawn_frames", k.stuck_respawn_frames);
            read_const(c, "drift_entry_slip_retention", k.drift_entry_slip_retention);
            read_const(c, "drift_turn_wide", k.drift_turn_wide);
            read_const(c, "drift_turn_neutral", k.drift_turn_neutral);
            read_const(c, "drift_turn_tight", k.drift_turn_tight);
            read_const(c, "drift_turn_start", k.drift_turn_start);
            read_const(c, "drift_turn_response", k.drift_turn_response);
            read_const(c, "drift_turn_response_settled", k.drift_turn_response_settled);
            read_const(c, "drift_inward_bonus", k.drift_inward_bonus);
            read_const(c, "coin_speed_bonus", k.coin_speed_bonus);
            read_const(c, "max_coins", k.max_coins);
            read_const(c, "charge_fast", k.charge_fast);
            read_const(c, "charge_slow", k.charge_slow);
            read_const(c, "spark_threshold", k.spark_threshold);
            read_const(c, "mt_thresholds", k.mt_thresholds);
            read_const(c, "mt_frames", k.mt_frames);
            read_const(c, "mt_boost_mult", k.mt_boost_mult);
            read_const(c, "offroad_light", k.offroad_light);
            read_const(c, "offroad_medium", k.offroad_medium);
            read_const(c, "offroad_heavy", k.offroad_heavy);
            read_const(c, "boost_pad_frames", k.boost_pad_frames);
            read_const(c, "boost_pad_mult", k.boost_pad_mult);
            read_const(c, "mushroom_frames", k.mushroom_frames);
            read_const(c, "mushroom_mult", k.mushroom_mult);
            read_const(c, "trick_frames", k.trick_frames);
            read_const(c, "trick_mult", k.trick_mult);
            read_const(c, "rocket_frames", k.rocket_frames);
            read_const(c, "rocket_mult", k.rocket_mult);
            read_const(c, "rocket_window_early", k.rocket_window_early);
            read_const(c, "rocket_window_late", k.rocket_window_late);
            read_const(c, "burnout_frames", k.burnout_frames);
            read_const(c, "boost_accel", k.boost_accel);
            read_const(c, "glide_launch_velocity", k.glide_launch_velocity);
            read_const(c, "glide_gravity", k.glide_gravity);
            read_const(c, "glide_ratio", k.glide_ratio);
            read_const(c, "glide_dive_range", k.glide_dive_range);
            read_const(c, "glide_climb_range", k.glide_climb_range);
            read_const(c, "glide_path_rate", k.glide_path_rate);
            read_const(c, "glide_floor", k.glide_floor);
            read_const(c, "glide_floor_gain", k.glide_floor_gain);
            read_const(c, "glide_cap_mult", k.glide_cap_mult);
            read_const(c, "glide_bank_drag", k.glide_bank_drag);
            read_const(c, "glide_pitch_speed", k.glide_pitch_speed);
            read_const(c, "fall_respawn_drop", k.fall_respawn_drop);
            read_const(c, "water_gravity_scale", k.water_gravity_scale);
            read_const(c, "water_terminal_fall", k.water_terminal_fall);
            read_const(c, "water_pitch_gain", k.water_pitch_gain);
            read_const(c, "drift_turn_water_scale", k.drift_turn_water_scale);
            read_const(c, "water_hysteresis", k.water_hysteresis);
            read_const(c, "trick_window_frames", k.trick_window_frames);
            read_const(c, "trick_min_air_frames", k.trick_min_air_frames);
            read_const(c, "trick_chain_frames", k.trick_chain_frames);
            read_const(c, "trick_max_height", k.trick_max_height);
            read_const(c, "crest_stick", k.crest_stick);
            read_const(c, "crest_carry", k.crest_carry);
            read_const(c, "grip_descent_gain", k.grip_descent_gain);
            read_const(c, "grip_carry_decel", k.grip_carry_decel);
            read_const(c, "grip_carry_frames", k.grip_carry_frames);
            read_const(c, "grip_charge_mult", k.grip_charge_mult);
            read_const(c, "respawn_frames", k.respawn_frames);
            read_const(c, "susp_freq_light", k.susp_freq_light);
            read_const(c, "susp_freq_heavy", k.susp_freq_heavy);
            read_const(c, "susp_damping_light", k.susp_damping_light);
            read_const(c, "susp_damping_heavy", k.susp_damping_heavy);
            read_const(c, "susp_land_gain_light", k.susp_land_gain_light);
            read_const(c, "susp_land_gain_heavy", k.susp_land_gain_heavy);
            read_const(c, "susp_angle_freq", k.susp_angle_freq);
            read_const(c, "susp_angle_damping", k.susp_angle_damping);
            read_const(c, "susp_pitch_per_accel", k.susp_pitch_per_accel);
            read_const(c, "susp_roll_per_lateral", k.susp_roll_per_lateral);
            read_const(c, "susp_slope_gain", k.susp_slope_gain);
            read_const(c, "susp_slope_lag", k.susp_slope_lag);
            read_const(c, "susp_max_compress", k.susp_max_compress);
            read_const(c, "susp_max_extend", k.susp_max_extend);
            read_const(c, "susp_max_pitch", k.susp_max_pitch);
            read_const(c, "susp_max_roll", k.susp_max_roll);
            read_const(c, "susp_air_droop", k.susp_air_droop);
            read_const(c, "susp_wobble_frames", k.susp_wobble_frames);
            read_const(c, "susp_wobble_steer", k.susp_wobble_steer);
            read_const(c, "tilt_lateral_light", k.tilt_lateral_light);
            read_const(c, "tilt_lateral_heavy", k.tilt_lateral_heavy);
            read_const(c, "tilt_per_lateral", k.tilt_per_lateral);
            read_const(c, "tilt_max", k.tilt_max);
            read_const(c, "tilt_freq", k.tilt_freq);
            read_const(c, "tilt_damping", k.tilt_damping);
            read_const(c, "tilt_bounce", k.tilt_bounce);
        }
    } catch (const std::exception& e) {
        if (error) *error = path + ": " + e.what();
        return false;
    }
    *this = std::move(t);
    return true;
}

KartParams PhysicsTables::params(const StatArray& lv, const Part& body, EngineClass cc) const {
    auto L = [&](Stat s) { return lv[int(s)]; };
    auto C = [&](Stat s) { return curves[int(s)].at(L(s)); };
    const float ccs = cc_speed[int(cc)];
    KartParams p;
    p.top_speed = C(Stat::SpeedGround) * ccs;
    p.top_speed_water = C(Stat::SpeedWater) * ccs;
    p.glide_speed = C(Stat::SpeedAir) * ccs;
    // Faster classes also accelerate harder so time-to-top-speed stays similar.
    p.accel = C(Stat::Acceleration) * ccs;
    p.mass = C(Stat::Weight);
    p.turn_rate = C(Stat::HandlingGround);
    p.turn_rate_water = C(Stat::HandlingWater);
    p.turn_rate_air = C(Stat::HandlingAir);
    p.slip_traction = C(Stat::Traction);
    p.sand_grip = sand_grip.at(L(Stat::Traction));
    p.ice_grip = ice_grip.at(L(Stat::Traction));
    p.mt_charge = C(Stat::MiniTurbo);
    p.mt_boost = mt_boost.at(L(Stat::MiniTurbo));
    p.invincibility_frames = int(C(Stat::Invincibility));
    p.weight01 = float(L(Stat::Weight)) / float(kMaxLevel);
    p.drift = body.drift;
    p.is_bike = body.is_bike;
    return p;
}

} // namespace gk::sim
