// Track designer's view: draws every track in the catalog from above as a
// PNG (road coloured by surface, shaded by height, higher roads on top,
// control points numbered) and prints where the road crosses itself.
//
//   gk_trackmap data/tracks/tracks.json out_dir [track_id] [--race]
//
// --race also runs a 12-CPU, 2-lap race on each track and prints lap times
// and where karts fell off.
// --crop x,z,size draws only that square, close up, triangle by triangle.
// --balance runs top-difficulty CPU time trials at 150cc (no items, no
// rubber band, five seeds) for the parts catalogue's max-speed build, its
// max-mini-turbo build and a build between them, and prints best and mean
// laps and the gap between the two extremes as Markdown table rows
// (docs/BALANCE.md). GK_LAPS=1 also prints every lap, wall hit and respawn.
#include "gk/sim/ai.h"
#include "gk/sim/data.h"
#include "gk/sim/stats.h"
#include "gk/sim/track_def.h"
#include "gk/sim/world.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace gk;
using namespace gk::sim;

namespace {

struct Image {
    int w, h;
    std::vector<uint8_t> rgb;
    std::vector<float> depth;
    Image(int w_, int h_) : w(w_), h(h_), rgb(size_t(w_ * h_ * 3), 0), depth(size_t(w_ * h_), -1e9f) {
        for (size_t i = 0; i < rgb.size(); i += 3) rgb[i] = 34, rgb[i + 1] = 38, rgb[i + 2] = 46;
    }
    void put(int x, int y, uint32_t c, float z = 1e9f) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const size_t i = size_t(y * w + x);
        if (z < depth[i]) return;
        depth[i] = z;
        rgb[i * 3] = uint8_t(c >> 16), rgb[i * 3 + 1] = uint8_t(c >> 8), rgb[i * 3 + 2] = uint8_t(c);
    }
};

uint32_t surface_rgb(Surface s) {
    switch (s) {
    case Surface::Road: return 0x8c8c96;
    case Surface::OffroadLight: return 0x5c9a46;
    case Surface::OffroadMedium: return 0x7a6a40;
    case Surface::OffroadHeavy: return 0x5a3c22;
    case Surface::Sand: return 0xdcc282;
    case Surface::Ice: return 0xbae4f5;
    case Surface::Water: return 0x3c78c8;
    case Surface::BoostPad: return 0xffaa1e;
    case Surface::GlideRamp: return 0x3c82ff;
    case Surface::TrickRamp: return 0xf0dc3c;
    case Surface::Wall: return 0xe03c3c;
    default: return 0x000000;
    }
}

uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int s) { return uint32_t(std::clamp(float((c >> s) & 255) * f, 0.0f, 255.0f)) << s; };
    return ch(0) | ch(8) | ch(16);
}

// 3x5 digits.
const char* kDigits[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111",
                           "101101111001001", "111100111001111", "111100111101111", "111001001001001",
                           "111101111101111", "111101111001111"};

void text(Image& img, int x, int y, int value, uint32_t c) {
    const std::string s = std::to_string(value);
    for (size_t k = 0; k < s.size(); ++k)
        for (int r = 0; r < 5; ++r)
            for (int q = 0; q < 3; ++q)
                if (kDigits[s[k] - '0'][r * 3 + q] == '1')
                    for (int a = 0; a < 2; ++a)
                        for (int b = 0; b < 2; ++b) img.put(x + int(k) * 8 + q * 2 + a, y + r * 2 + b, c);
}

void draw(const TrackDef& def, const std::string& out_dir) {
    const Track t = build_track(def);
    vec3 lo(1e9f), hi(-1e9f);
    for (const auto& c : t.road) lo = glm::min(lo, c.pos), hi = glm::max(hi, c.pos);
    const float margin = 30.0f, px = 3.0f; // pixels per metre
    const int w = int((hi.x - lo.x + 2 * margin) * px), h = int((hi.z - lo.z + 2 * margin) * px);
    Image img(w, h);
    // Seen from above: +x to the right, +z down the image.
    auto to_px = [&](vec3 p) { return vec2((p.x - lo.x + margin) * px, (p.z - lo.z + margin) * px); };
    const float yspan = std::max(hi.y - lo.y, 1.0f);
    for (const auto& tri : t.triangles) {
        if (tri.surface == Surface::OutOfBounds) continue;
        const vec3 a = t.vertices[tri.a], b = t.vertices[tri.b], c = t.vertices[tri.c];
        const vec2 pa = to_px(a), pb = to_px(b), pc = to_px(c);
        const float area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
        if (std::abs(area) < 1e-6f) continue;
        const int x0 = std::max(0, int(std::floor(std::min({pa.x, pb.x, pc.x})))),
                  x1 = std::min(w - 1, int(std::ceil(std::max({pa.x, pb.x, pc.x}))));
        const int y0 = std::max(0, int(std::floor(std::min({pa.y, pb.y, pc.y})))),
                  y1 = std::min(h - 1, int(std::ceil(std::max({pa.y, pb.y, pc.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                const float u = ((pb.x - p.x) * (pc.y - p.y) - (pb.y - p.y) * (pc.x - p.x)) / area;
                const float v = ((pc.x - p.x) * (pa.y - p.y) - (pc.y - p.y) * (pa.x - p.x)) / area;
                const float s = 1.0f - u - v;
                if (u < -0.01f || v < -0.01f || s < -0.01f) continue;
                const float z = u * a.y + v * b.y + s * c.y + (tri.surface == Surface::Wall ? 0.5f : 0.0f);
                img.put(x, y, shade(surface_rgb(tri.surface), 0.55f + 0.6f * (z - lo.y) / yspan), z);
            }
    }
    // Start line, racing line, control points.
    for (size_t i = 0; i < t.road.size(); ++i) {
        const vec2 p = to_px(t.line_point(int(i)));
        img.put(int(p.x), int(p.y), t.road[i].branch < 0 ? 0x30ff60 : 0x30e0ff, t.road[i].pos.y + 0.1f);
    }
    // Tunnels: a dark roof stripe down the middle with ribs across it.
    for (const Track::Tunnel& tn : t.tunnels)
        for (int k = 0; k < tn.count; ++k) {
            const CenterSample& c = t.road[size_t(tn.first + k)];
            const float span = tn.half_span.empty() ? c.half_width : tn.half_span[size_t(k)];
            for (float o = -span; o <= span; o += 0.5f)
                if (std::abs(o) < 1.0f || k % 4 == 0) {
                    const vec2 p = to_px(c.pos + c.right * o);
                    img.put(int(p.x), int(p.y), 0x282830, c.pos.y + 0.3f);
                }
        }
    // Zones (tinted), barriers (red-orange bars) and switches (yellow discs).
    for (const Track::Zone& z : t.zones)
        for (int i = z.first; i < z.first + z.count; ++i) {
            const CenterSample& c = t.road[size_t(i)];
            for (float o = std::max(z.lo, -c.half_width); o <= std::min(z.hi, c.half_width); o += 0.6f) {
                const vec2 p = to_px(c.pos + c.right * o);
                if ((int(p.x) + int(p.y)) % 3 == 0) img.put(int(p.x), int(p.y), z.surface == Surface::OutOfBounds ? 0xff5010 : 0x40a0ff, c.pos.y + 0.2f);
            }
        }
    for (const Track::Barrier& b : t.barriers)
        for (float x = -b.half.x; x <= b.half.x; x += 0.2f)
            for (float z = -b.half.z; z <= b.half.z; z += 0.2f) {
                const vec2 p = to_px(b.pos + b.right * x + b.forward * z);
                img.put(int(p.x), int(p.y), 0xff7030, b.pos.y + 3.0f);
            }
    for (const Track::Switch& sw : t.switches)
        for (float a = 0; a < 6.3f; a += 0.05f)
            for (float r = 0; r < sw.radius; r += 0.3f) {
                const vec2 p = to_px(sw.pos + sw.right * (std::cos(a) * r) + sw.forward * (std::sin(a) * r));
                img.put(int(p.x), int(p.y), 0xffe020, sw.pos.y + 3.0f);
            }
    for (const BranchDef& bd : def.branches)
        for (size_t i = 0; i < bd.points.size(); ++i) {
            const vec2 p = to_px(vec3(bd.points[i]));
            for (int a = -2; a <= 2; ++a)
                for (int b = -2; b <= 2; ++b) img.put(int(p.x) + a, int(p.y) + b, 0x60e0ff);
            text(img, int(p.x) + 5, int(p.y) - 5, int(i), 0x60e0ff);
        }
    for (float o = -t.center[0].half_width; o <= t.center[0].half_width; o += 0.2f) {
        const vec2 p = to_px(t.center[0].pos + t.center[0].right * o);
        img.put(int(p.x), int(p.y), 0xffffff);
    }
    for (size_t i = 0; i < def.points.size(); ++i) {
        const vec2 p = to_px(vec3(def.points[i]));
        for (int a = -2; a <= 2; ++a)
            for (int b = -2; b <= 2; ++b) img.put(int(p.x) + a, int(p.y) + b, 0xffffff);
        text(img, int(p.x) + 5, int(p.y) - 5, int(i), 0xffffff);
    }
    const std::string path = out_dir + "/" + def.id + ".png";
    stbi_write_png(path.c_str(), w, h, 3, img.rgb.data(), w * 3);

    // Report crossings: samples far apart along the loop but close from above.
    const int n = int(t.center.size());
    float tightest = 1e9f;
    for (const auto& lp : t.line) tightest = std::min(tightest, 1.0f / std::max(std::abs(lp.curvature), 1e-4f));
    std::printf("%s: %.0f m, %d samples, tightest line radius %.0f m, height %.0f..%.0f -> %s\n", def.name.c_str(),
                t.length, n, tightest, lo.y, hi.y, path.c_str());
    for (const Track::Branch& b : t.branches)
        std::printf("  branch %s: %d samples (%.0f m) from main %d to %d%s%s\n", b.name.c_str(), b.count, float(b.count) * 2.0f, b.fork, b.merge,
                    b.gate >= 0 ? ", gated" : "", b.vanish ? ", vanishes" : "");
    // Side-road bends tighter than the road is wide fold the road over itself.
    for (const Track::Branch& b : t.branches)
        for (int k = 2; k + 2 < b.count; ++k) {
            const CenterSample &p0 = t.sample(b.first + k - 2), &p1 = t.sample(b.first + k), &p2 = t.sample(b.first + k + 2);
            const vec2 d1 = glm::normalize(vec2(p1.pos.x - p0.pos.x, p1.pos.z - p0.pos.z)), d2 = glm::normalize(vec2(p2.pos.x - p1.pos.x, p2.pos.z - p1.pos.z));
            const float ang = std::acos(std::clamp(glm::dot(d1, d2), -1.0f, 1.0f));
            const float radius = ang > 1e-4f ? 4.0f / ang : 1e9f;
            if (radius < p1.half_width + 4.0f) {
                std::printf("  FOLD: branch %s bends %.0f m wide at %d m (sample %d)\n", b.name.c_str(), radius, k * 2, b.first + k);
                k += 4;
            }
        }
    std::printf("  %zu gates, %zu switches, %zu barriers, %zu zones, %zu obstacles, %zu movers\n", t.gates.size(), t.switches.size(),
                t.barriers.size(), t.zones.size(), t.obstacles.size(), t.movers.size());
    // Parts of the road that come close from above must be a bridge's
    // height apart; report the tightest such pair and every crossing.
    float worst = 1e9f;
    int wi = -1, wj = -1, last = -100;
    for (int i = 0; i < n; ++i)
        for (int j = i + 20; j < n; ++j) {
            if (n - (j - i) < 20) continue;
            const CenterSample &a = t.center[size_t(i)], &b = t.center[size_t(j)];
            const float d = glm::length(vec2(a.pos.x - b.pos.x, a.pos.z - b.pos.z));
            const float dy = std::abs(a.pos.y - b.pos.y);
            if (d < a.half_width + b.half_width + 10.0f && dy < worst) worst = dy, wi = i, wj = j;
            if (d < 2.0f && i - last > 10) {
                std::printf("  crossing at samples %d / %d: %.1f m apart vertically\n", i, j, dy);
                last = i;
            }
        }
    if (wi >= 0) std::printf("  closest overlap: samples %d / %d, %.1f m apart vertically\n", wi, wj, worst);
}

// --crop x,z,size: a close-up from above of one square of track, 40 px per
// metre, every triangle outlined (floors by surface, wall bases as thick red
// lines), for working out how the meshes meet at a junction.
void crop(const TrackDef& def, const std::string& out_dir, float cx, float cz, float size) {
    const Track t = build_track(def);
    const float px = 40.0f;
    const int n = int(size * px);
    Image img(n, n);
    auto to_px = [&](vec3 p) { return vec2((p.x - cx + size * 0.5f) * px, (p.z - cz + size * 0.5f) * px); };
    auto line = [&](vec2 a, vec2 b, uint32_t c, float z, int thick) {
        const int steps = int(glm::length(b - a)) + 1;
        for (int k = 0; k <= steps; ++k) {
            const vec2 p = glm::mix(a, b, float(k) / float(steps));
            for (int dx = -thick; dx <= thick; ++dx)
                for (int dy = -thick; dy <= thick; ++dy) img.put(int(p.x) + dx, int(p.y) + dy, c, z);
        }
    };
    for (const auto& tri : t.triangles) {
        if (tri.surface == Surface::OutOfBounds) continue;
        const vec3 a = t.vertices[tri.a], b = t.vertices[tri.b], c = t.vertices[tri.c];
        const vec2 pa = to_px(a), pb = to_px(b), pc = to_px(c);
        if (std::max({pa.x, pb.x, pc.x}) < 0 || std::min({pa.x, pb.x, pc.x}) > float(n) || std::max({pa.y, pb.y, pc.y}) < 0 ||
            std::min({pa.y, pb.y, pc.y}) > float(n))
            continue;
        const float area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
        if (tri.surface == Surface::Wall || std::abs(area) < 1.0f) {
            // A wall from above: its foot.
            const vec3* v[3] = {&a, &b, &c};
            std::sort(std::begin(v), std::end(v), [](const vec3* x, const vec3* y) { return x->y < y->y; });
            line(to_px(*v[0]), to_px(*v[1]), 0xff2020, 1e6f, 2);
            continue;
        }
        const int x0 = std::max(0, int(std::min({pa.x, pb.x, pc.x}))), x1 = std::min(n - 1, int(std::max({pa.x, pb.x, pc.x})) + 1);
        const int y0 = std::max(0, int(std::min({pa.y, pb.y, pc.y}))), y1 = std::min(n - 1, int(std::max({pa.y, pb.y, pc.y})) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                const float u = ((pb.x - p.x) * (pc.y - p.y) - (pb.y - p.y) * (pc.x - p.x)) / area;
                const float v = ((pc.x - p.x) * (pa.y - p.y) - (pc.y - p.y) * (pa.x - p.x)) / area;
                const float w = 1.0f - u - v;
                if (u < 0 || v < 0 || w < 0) continue;
                const float z = u * a.y + v * b.y + w * c.y;
                // Overlapping floors show as a magenta checker.
                const size_t i = size_t(y * n + x);
                if (img.depth[i] > -1e8f && std::abs(img.depth[i] - z) < 0.12f) { img.rgb[i * 3] = 255, img.rgb[i * 3 + 1] = 0, img.rgb[i * 3 + 2] = 255; continue; }
                img.put(x, y, surface_rgb(tri.surface), z);
            }
        line(pa, pb, 0x202020, 1e5f, 0), line(pb, pc, 0x202020, 1e5f, 0), line(pc, pa, 0x202020, 1e5f, 0);
    }
    const std::string path = out_dir + "/" + def.id + "_crop.png";
    stbi_write_png(path.c_str(), n, n, 3, img.rgb.data(), n * 3);
    std::printf("%s\n", path.c_str());
}

void race(const TrackDef& def) {
    PhysicsTables tables;
    PartsCatalog parts;
    std::string err;
    if (!tables.load(data_path("physics.json"), &err) || !parts.load(data_path("parts.json"), &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return;
    }
    World w(build_track(def), tables, RaceConfig{.laps = 2, .countdown_frames = 180, .seed = 3});
    for (int i = 0; i < 12; ++i) {
        const Build b{i % 4, i % 4, (i / 2) % 4, i % 2};
        KartParams p = tables.params(parts.levels(b), parts.body(b), EngineClass::cc150);
        p.rubber_band = cpu_rubber_band(CpuLevel::Normal);
        w.add_kart(p);
    }
    std::vector<KartInput> in(12);
    int frames = 0, first_lap = 0, respawns = 0, wall_hits = 0;
    // Suspension: how hard it works over the race, to tune it by.
    float max_y = 0.0f, max_pitch = 0.0f, max_roll = 0.0f;
    long busy = 0, pinned = 0, samples = 0;
    int wobbles = 0;
    // Stalls: the longest each kart spent crawling (under 3 m/s) once racing, and where.
    std::vector<int> crawl(12, 0), worst(12, 0), worst_at(12, 0);
    auto done = [&] { return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; }); };
    while (!done() && frames < 60 * 60 * 4) {
        for (int i = 0; i < 12; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(i % 3 == 0 ? CpuLevel::Hard : CpuLevel::Normal, i));
        w.tick(in);
        ++frames;
        for (size_t i = 0; i < w.karts().size() && w.racing() && w.race_ticks() > 240; ++i) {
            const KartState& k = w.karts()[i];
            crawl[i] = !k.finished && std::abs(k.speed) < 3.0f ? crawl[i] + 1 : 0;
            if (crawl[i] > worst[i]) worst[i] = crawl[i], worst_at[i] = k.sample;
        }
        for (const KartState& k : w.karts()) {
            if (k.finished || !k.grounded) continue;
            ++samples;
            max_y = std::max(max_y, k.susp_y);
            max_pitch = std::max(max_pitch, std::abs(k.susp_pitch));
            max_roll = std::max(max_roll, std::abs(k.susp_roll));
            busy += std::abs(k.susp_y) > 0.02f || std::abs(k.susp_pitch) > 0.03f || std::abs(k.susp_roll) > 0.03f;
            pinned += std::abs(k.susp_pitch) >= tables.constants.susp_max_pitch - 1e-4f || std::abs(k.susp_roll) >= tables.constants.susp_max_roll - 1e-4f;
            wobbles += k.susp_wobble == uint8_t(tables.constants.susp_wobble_frames - 1);
        }
        for (const auto& e : w.events()) {
            if (e.type == KartEventType::Respawn && !w.karts()[size_t(e.kart)].finished) {
                ++respawns;
                { const KartState& k = w.karts()[size_t(e.kart)]; std::printf("  respawn: kart %d at %.0f %.0f %.0f (sample %d, last safe %d)\n", e.kart, k.pos.x, k.pos.y, k.pos.z, k.sample, k.last_safe_sample); }
            }
            if (e.type == KartEventType::WallHit && !w.karts()[size_t(e.kart)].finished) {
                ++wall_hits;
                std::printf("  wall hit: kart %d at sample %d\n", e.kart, w.karts()[size_t(e.kart)].sample);
            }
            if (e.type == KartEventType::Finish) std::printf("  kart %d finished at %.1f s\n", e.kart, w.race_ticks() / 60.0f);
            if (e.type == KartEventType::Lap && first_lap == 0) first_lap = w.race_ticks();
        }
    }
    for (size_t i = 0; i < worst.size(); ++i)
        if (worst[i] > 120) std::printf("  stall: kart %d crawled %.1f s at sample %d\n", int(i), float(worst[i]) / 60.0f, worst_at[i]);
    for (size_t i = 0; i < w.karts().size(); ++i) {
        const KartState& k = w.karts()[i];
        if (!k.finished)
            std::printf("  unfinished: kart %d on lap %d at sample %d (%.0f %.0f %.0f), speed %.1f, %s\n", int(i), k.lap, k.sample, k.pos.x, k.pos.y, k.pos.z,
                        k.speed, k.grounded ? "grounded" : "airborne");
    }
    std::printf("  race: first lap %.1f s, %s after %.1f s, %d respawns, %d wall hits\n", first_lap / 60.0f,
                done() ? "finished" : "NOT FINISHED", frames / 60.0f, respawns, wall_hits);
    std::printf("  suspension: max compression %.2f m, pitch %.2f, roll %.2f rad; moving %.0f%% of the time, at a cap %.1f%%, %d hard landings\n",
                max_y, max_pitch, max_roll, 100.0 * double(busy) / double(std::max(1L, samples)),
                100.0 * double(pinned) / double(std::max(1L, samples)), wobbles);
}

struct BalanceBuilds {
    Build speed, turbo, middle;
};

// The real combinations at the ends: the fastest build (the best mini-turbo
// among them), the best mini-turbo build (the fastest among them), and the
// combination nearest the midpoint of the two.
BalanceBuilds balance_builds(const PartsCatalog& parts) {
    std::vector<std::pair<Build, StatArray>> all;
    for (int d = 0; d < int(parts.drivers.size()); ++d)
        for (int b = 0; b < int(parts.bodies.size()); ++b)
            for (int t = 0; t < int(parts.tires.size()); ++t)
                for (int g = 0; g < int(parts.gliders.size()); ++g) {
                    const Build x{d, b, t, g};
                    all.emplace_back(x, parts.levels(x));
                }
    const int S = int(Stat::SpeedGround), M = int(Stat::MiniTurbo);
    auto best = [&](int primary, int secondary) {
        return std::max_element(all.begin(), all.end(), [&](const auto& a, const auto& b) {
            return std::pair(a.second[size_t(primary)], a.second[size_t(secondary)]) < std::pair(b.second[size_t(primary)], b.second[size_t(secondary)]);
        });
    };
    BalanceBuilds r;
    const auto sp = best(S, M), mt = best(M, S);
    r.speed = sp->first, r.turbo = mt->first;
    const float ms = 0.5f * float(sp->second[size_t(S)] + mt->second[size_t(S)]), mm = 0.5f * float(sp->second[size_t(M)] + mt->second[size_t(M)]);
    r.middle = std::min_element(all.begin(), all.end(), [&](const auto& a, const auto& b) {
        auto d = [&](const StatArray& l) { return std::pow(float(l[size_t(S)]) - ms, 2.0f) + std::pow(float(l[size_t(M)]) - mm, 2.0f); };
        return d(a.second) < d(b.second);
    })->first;
    return r;
}

struct LapStats {
    float best = 1e9f, mean = 0.0f;
    float drifting = 0.0f; // share of race time spent drifting
    int laps = 0;
    bool finished = true;
};

LapStats time_trial(const TrackDef& def, const PhysicsTables& tables, const PartsCatalog& parts, Build b, int seeds) {
    LapStats st;
    float sum = 0.0f;
    long drift_frames = 0, all_frames = 0;
    for (int seed = 0; seed < seeds; ++seed) {
        World w(build_track(def), tables, RaceConfig{.laps = 3, .countdown_frames = 180, .items = false, .seed = uint64_t(seed + 1)});
        KartParams p = tables.params(parts.levels(b), parts.body(b), EngineClass::cc150);
        w.add_kart(p);
        const CpuSkill skill = cpu_skill(CpuLevel::Hard, seed);
        std::vector<KartInput> in(1);
        int last = 0, frames = 0;
        while (!w.karts()[0].finished && frames < 60 * 60 * 6) {
            in[0] = cpu_input(w, 0, skill);
            w.tick(in);
            ++frames;
            if (w.racing()) ++all_frames, drift_frames += w.karts()[0].drift == DriftState::Drifting;
            for (const auto& e : w.events()) {
                if (std::getenv("GK_LAPS") && (e.type == KartEventType::Respawn || e.type == KartEventType::WallHit || e.type == KartEventType::Hit || e.type == KartEventType::Smash || e.type == KartEventType::TireBounce))
                    std::printf("    event %d at sample %d, %.1f s\n", int(e.type), w.karts()[0].sample, w.race_ticks() / 60.0f);
                if (e.type == KartEventType::Lap || e.type == KartEventType::Finish) {
                    if (e.type == KartEventType::Finish && w.race_ticks() == last) continue; // the last lap also fires Lap
                    const float t = float(w.race_ticks() - last) / 60.0f;
                    last = w.race_ticks();
                    if (std::getenv("GK_LAPS")) std::printf("  seed %d lap %.2f\n", seed, t);
                    st.best = std::min(st.best, t);
                    sum += t;
                    ++st.laps;
                }
            }
        }
        st.finished = st.finished && w.karts()[0].finished;
        if (!w.karts()[0].finished) std::fprintf(stderr, "  %s: seed %d did not finish (lap %d, sample %d)\n", def.id.c_str(), seed, w.karts()[0].lap, w.karts()[0].sample);
    }
    st.drifting = float(drift_frames) / float(std::max(1L, all_frames));
    st.mean = st.laps ? sum / float(st.laps) : 0.0f;
    return st;
}

void balance(const std::vector<TrackDef>& tracks, const std::string& only) {
    PhysicsTables tables;
    PartsCatalog parts;
    std::string err;
    if (!tables.load(data_path("physics.json"), &err) || !parts.load(data_path("parts.json"), &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return;
    }
    const BalanceBuilds bb = balance_builds(parts);
    auto describe = [&](const char* name, Build b) {
        const StatArray l = parts.levels(b);
        const KartParams p = tables.params(l, parts.body(b), EngineClass::cc150);
        std::printf("%s: %s / %s / %s / %s, speed %d, mini-turbo %d, accel %d, handling %d, weight %d -> top %.2f m/s, charge x%.2f, boost x%.2f\n", name,
                    parts.drivers[size_t(b.driver)].name.c_str(), parts.bodies[size_t(b.body)].name.c_str(), parts.tires[size_t(b.tires)].name.c_str(),
                    parts.gliders[size_t(b.glider)].name.c_str(), l[int(Stat::SpeedGround)], l[int(Stat::MiniTurbo)], l[int(Stat::Acceleration)],
                    l[int(Stat::HandlingGround)], l[int(Stat::Weight)], p.top_speed, p.mt_charge, p.mt_boost);
    };
    describe("speed build", bb.speed);
    describe("mini-turbo build", bb.turbo);
    describe("middle build", bb.middle);
    std::printf("\n| Track | Speed best | Speed mean | Mini-turbo best | Mini-turbo mean | Middle best | Middle mean | Gap (MT vs speed, mean) | Drifting |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    constexpr int kSeeds = 5;
    for (const TrackDef& d : tracks) {
        if (!only.empty() && d.id != only) continue;
        const LapStats a = time_trial(d, tables, parts, bb.speed, kSeeds), b = time_trial(d, tables, parts, bb.turbo, kSeeds),
                       c = time_trial(d, tables, parts, bb.middle, kSeeds);
        const float gap = 100.0f * (b.mean - a.mean) / a.mean;
        std::printf("| %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %+.1f%%%s | %.0f%% |\n", d.name.c_str(), a.best, a.mean, b.best, b.mean, c.best, c.mean, gap,
                    a.finished && b.finished && c.finished ? "" : " (DNF)", 100.0f * c.drifting);
        std::fflush(stdout);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: gk_trackmap tracks.json out_dir [track_id]\n");
        return 2;
    }
    TrackCatalog cat;
    std::string err;
    if (!cat.load(argv[1], &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::string only;
    bool racing = false, balancing = false;
    float crop_x = 0, crop_z = 0, crop_size = 0;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--race") racing = true;
        else if (std::string(argv[i]) == "--crop" && i + 1 < argc) std::sscanf(argv[++i], "%f,%f,%f", &crop_x, &crop_z, &crop_size);
        else if (std::string(argv[i]) == "--balance") balancing = true;
        else only = argv[i];
    }
    if (balancing) {
        balance(cat.tracks, only);
        return 0;
    }
    for (const TrackDef& d : cat.tracks)
        if (only.empty() || d.id == only) {
            if (crop_size > 0.0f) {
                crop(d, argv[2], crop_x, crop_z, crop_size);
                continue;
            }
            draw(d, argv[2]);
            if (racing) race(d);
        }
    return 0;
}
