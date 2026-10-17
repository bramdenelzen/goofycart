// Audio: every effect exists and is sane, songs differ by seed, and the mixer
// output stays bounded with everything playing at once.
#include "gk/audio/mixer.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace gk::audio;

namespace {
float peak(const std::vector<float>& v) {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::abs(x));
    return m;
}
bool finite(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}
std::vector<float> run(Mixer& m, int frames) {
    std::vector<float> out(size_t(frames) * 2);
    m.render(out.data(), frames);
    return out;
}
} // namespace

TEST_CASE("every sound effect is short, audible and finite") {
    for (int i = 0; i < kSfxCount; ++i) {
        CAPTURE(i);
        const auto b = make_sfx(Sfx(i), 48000);
        CHECK(b.size() > 480);      // over 10 ms
        CHECK(b.size() < 48000 * 2); // under 2 s
        CHECK(finite(b));
        CHECK(peak(b) > 0.1f);
        CHECK(peak(b) <= 1.0f);
    }
}

TEST_CASE("songs are written from the seed and mood") {
    Mixer a(48000), b(48000), c(48000);
    a.play_music(1, Mood::Bright);
    b.play_music(1, Mood::Bright);
    c.play_music(2, Mood::Night);
    const auto oa = run(a, 48000 * 2), ob = run(b, 48000 * 2), oc = run(c, 48000 * 2);
    CHECK(oa == ob);
    CHECK(oa != oc);
    CHECK(peak(oa) > 0.05f);
    CHECK(finite(oa));
    CHECK(finite(oc));
    for (int m = 0; m <= int(Mood::Results); ++m) {
        Mixer x(48000);
        x.play_music(7, Mood(m));
        const auto o = run(x, 48000);
        CHECK(finite(o));
        CHECK(peak(o) > 0.02f);
    }
}

TEST_CASE("the mixer stays bounded with everything at once") {
    Mixer m(48000);
    m.play_music(3, Mood::Fiery);
    m.set_engine({.active = true, .speed = 1.2f, .throttle = 1.0f, .drift_tier = 3, .rough = 1.0f, .boosting = true});
    for (int i = 0; i < kSfxCount; ++i) m.play(Sfx(i), 1.0f, i % 2 ? -1.0f : 1.0f);
    const auto o = run(m, 48000);
    CHECK(finite(o));
    CHECK(peak(o) <= 1.0f);
    CHECK(peak(o) > 0.3f);

    // Silence when nothing plays.
    Mixer q(48000);
    CHECK(peak(run(q, 4800)) == 0.0f);
}
