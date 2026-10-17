// Track designer's view: draws every track in the catalog from above as a
// PNG (road coloured by surface, shaded by height, higher roads on top,
// control points numbered) and prints where the road crosses itself.
//
//   gk_trackmap data/tracks/tracks.json out_dir [track_id] [--race]
//
// --race also runs a 12-CPU, 2-lap race on each track and prints lap times
// and where karts fell off.
#include "gk/sim/ai.h"
#include "gk/sim/data.h"
#include "gk/sim/stats.h"
#include "gk/sim/track_def.h"
#include "gk/sim/world.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    for (const auto& c : t.center) lo = glm::min(lo, c.pos), hi = glm::max(hi, c.pos);
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
    for (size_t i = 0; i < t.center.size(); ++i) {
        const vec2 p = to_px(t.line_point(int(i)));
        img.put(int(p.x), int(p.y), 0x30ff60, t.center[i].pos.y + 0.1f);
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
    auto done = [&] { return std::all_of(w.karts().begin(), w.karts().end(), [](const KartState& k) { return k.finished; }); };
    while (!done() && frames < 60 * 60 * 4) {
        for (int i = 0; i < 12; ++i) in[size_t(i)] = cpu_input(w, i, cpu_skill(i % 3 == 0 ? CpuLevel::Hard : CpuLevel::Normal, i));
        w.tick(in);
        ++frames;
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
    std::printf("  race: first lap %.1f s, %s after %.1f s, %d respawns, %d wall hits\n", first_lap / 60.0f,
                done() ? "finished" : "NOT FINISHED", frames / 60.0f, respawns, wall_hits);
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
    bool racing = false;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--race") racing = true;
        else only = argv[i];
    }
    for (const TrackDef& d : cat.tracks)
        if (only.empty() || d.id == only) {
            draw(d, argv[2]);
            if (racing) race(d);
        }
    return 0;
}
