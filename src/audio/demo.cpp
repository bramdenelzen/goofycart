// gk_audiodemo: writes the game's sounds to WAV files for listening without
// the game: every effect in a row, the bots' engine voices, karts passing
// by with Doppler, then a short loop of each song mood.
//
//   gk_audiodemo [out_dir] [seconds_per_song]
#include "gk/audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace gk::audio;

namespace {

bool write_wav(const std::string& path, const std::vector<float>& stereo, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t data = uint32_t(stereo.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16), u16(1), u16(2), u32(uint32_t(rate)), u32(uint32_t(rate) * 4), u16(4), u16(16);
    std::fwrite("data", 1, 4, f);
    u32(data);
    for (float s : stereo) {
        const auto v = int16_t(std::max(-1.0f, std::min(1.0f, s)) * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    return std::fclose(f) == 0;
}

std::vector<float> run(Mixer& m, float seconds) {
    std::vector<float> out(size_t(seconds * float(m.sample_rate())) * 2);
    m.render(out.data(), int(out.size() / 2));
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const float song_s = argc > 2 ? float(std::atof(argv[2])) : 20.0f;
    constexpr int kRate = 48000;

    Mixer fx(kRate);
    std::vector<float> all;
    for (int i = 0; i < kSfxCount; ++i) {
        fx.play(Sfx(i));
        const auto part = run(fx, float(fx.sfx_buffer(Sfx(i)).size()) / kRate + 0.35f);
        all.insert(all.end(), part.begin(), part.end());
    }
    // A lap of engine: pull away, drift with sparks, land on grass.
    fx.set_engine({.active = true, .speed = 0.1f, .throttle = 1.0f});
    for (int t = 0; t < 60; ++t) {
        const float s = float(t) / 60.0f;
        fx.set_engine({.active = true, .speed = s, .throttle = 1.0f, .drift_tier = t > 30 ? (t - 30) / 8 : -1,
                       .rough = t > 54 ? 0.8f : 0.0f});
        const auto part = run(fx, 0.1f);
        all.insert(all.end(), part.begin(), part.end());
    }
    bool ok = write_wav(dir + "/sfx.wav", all, kRate);

    // Engines as voices: every bot's own, revving from idle to flat out one
    // after another.
    std::vector<float> bots;
    {
        Mixer mx(kRate);
        for (uint32_t seat = 0; seat < uint32_t(kKartEngines); ++seat) {
            KartEngine k;
            k.clip = std::make_shared<const EngineClip>(make_bot_clip(seat, kRate));
            k.throttle = 1.0f;
            k.gain = kLocalEngineGain;
            for (int t = 0; t <= 15; ++t) {
                k.speed = float(t) / 12.0f;
                mx.set_kart_engines({&k, 1});
                const auto part = run(mx, 0.1f);
                bots.insert(bots.end(), part.begin(), part.end());
            }
            mx.set_kart_engines({});
            const auto gap = run(mx, 0.3f);
            bots.insert(bots.end(), gap.begin(), gap.end());
        }
    }
    ok &= write_wav(dir + "/engine_bots.wav", bots, kRate);

    // Karts going by, placed and bent as the game does it (App::update_kart_engines):
    // one flying past a listener at the roadside, then one overtaking us by 6 m/s.
    std::vector<float> passes;
    {
        const auto voice = std::make_shared<const EngineClip>(make_bot_clip(0, kRate));
        for (const auto [kart_v, ear_v] : {std::pair{28.0f, 0.0f}, std::pair{30.0f, 24.0f}}) {
            Mixer mx(kRate);
            const float lane = 3.0f, rel_v = kart_v - ear_v, seconds = 70.0f / rel_v;
            for (float t = 0.0f; t < seconds; t += 0.02f) {
                const float x = -35.0f + rel_v * t; // along the road, from the listener
                const float d = std::sqrt(x * x + lane * lane);
                const float toward_x = -x / d;   // unit vector from the kart to the ears, along the road
                KartEngine k;
                k.clip = voice;
                k.speed = 1.0f;
                k.throttle = 1.0f;
                k.pan = std::clamp(x / (lane + 4.0f), -1.0f, 1.0f) * 0.8f;
                k.gain = 0.55f / (1.0f + d / 10.0f) * 2.0f; // a little louder than in a race, to hear it alone
                k.muffle = std::min(1.0f, d / 70.0f);
                k.doppler = doppler_factor(kart_v * toward_x, -ear_v * toward_x, 70.0f);
                mx.set_kart_engines({&k, 1});
                const auto part = run(mx, 0.02f);
                passes.insert(passes.end(), part.begin(), part.end());
            }
            mx.set_kart_engines({});
            const auto gap = run(mx, 0.6f);
            passes.insert(passes.end(), gap.begin(), gap.end());
        }
    }
    ok &= write_wav(dir + "/engine_passby.wav", passes, kRate);

    const char* names[] = {"bright", "dusty", "breezy", "frosty", "driving", "fiery", "airy", "night", "menu", "results"};
    for (int m = 0; m <= int(Mood::Results); ++m) {
        Mixer mx(kRate);
        mx.play_music(42, Mood(m));
        ok &= write_wav(dir + "/music_" + names[m] + ".wav", run(mx, song_s), kRate);
    }
    std::printf("%s\n", ok ? "wrote sfx.wav, engine_bots.wav, engine_passby.wav and music_*.wav" : "could not write the files");
    return ok ? 0 : 1;
}
