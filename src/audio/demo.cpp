// gk_audiodemo: writes the game's sounds to WAV files for listening without
// the game: every effect in a row, then a short loop of each song mood.
//
//   gk_audiodemo [out_dir] [seconds_per_song]
#include "gk/audio/mixer.h"

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

    const char* names[] = {"bright", "dusty", "breezy", "frosty", "driving", "fiery", "airy", "night", "menu", "results"};
    for (int m = 0; m <= int(Mood::Results); ++m) {
        Mixer mx(kRate);
        mx.play_music(42, Mood(m));
        ok &= write_wav(dir + "/music_" + names[m] + ".wav", run(mx, song_s), kRate);
    }
    std::printf("%s\n", ok ? "wrote sfx.wav and music_*.wav" : "could not write the files");
    return ok ? 0 : 1;
}
