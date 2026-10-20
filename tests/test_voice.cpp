// Engine sounds from a voice: whatever was said (a held "ahhh", a word)
// becomes a loop with no click, every bot sings its own, a recording
// survives the wire and a WAV file, Doppler bends the pitch the right way,
// and the mixer plays the loops at the synth's loudness, at the voice's own
// pitch flat out and lower when slow.
#include "gk/audio/mixer.h"
#include "gk/audio/voice.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace gk::audio;

namespace {
constexpr int kRate = 48000;
constexpr float kPi = 3.14159265f;

float rms(const std::vector<float>& v, size_t from = 0, size_t count = SIZE_MAX) {
    const size_t end = std::min(v.size(), count == SIZE_MAX ? v.size() : from + count);
    double s = 0.0;
    for (size_t i = from; i < end; ++i) s += double(v[i]) * double(v[i]);
    return end > from ? float(std::sqrt(s / double(end - from))) : 0.0f;
}
bool finite(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}
// How far the loop jumps where it wraps, against the steepest step the wave
// itself takes anywhere else: a click jumps well past that, while a seam that
// happens to land on a steep edge of the wave does not.
float seam_ratio(const std::vector<float>& v) {
    float steepest = 0.0f;
    for (size_t i = 1; i < v.size(); ++i) steepest = std::max(steepest, std::abs(v[i] - v[i - 1]));
    return std::abs(v.front() - v.back()) / std::max(steepest, 1e-6f);
}
// A held vowel with room noise around it: a saw at `f0` through two crude
// formants, faded in and out, between stretches of near-silence.
std::vector<float> fake_ahh(float before, float held, float after, float f0 = 140.0f, float level = 0.4f) {
    std::vector<float> out;
    uint32_t n = 12345;
    auto hiss = [&] {
        n ^= n << 13, n ^= n >> 17, n ^= n << 5;
        return (float(n >> 8) / float(1u << 23) - 1.0f) * 0.002f;
    };
    for (int i = 0; i < int(before * kRate); ++i) out.push_back(hiss());
    float ph = 0.0f, lp1 = 0.0f, lp2 = 0.0f;
    const int len = int(held * kRate), edge = kRate / 20;
    for (int i = 0; i < len; ++i) {
        ph += f0 / float(kRate);
        ph -= std::floor(ph);
        const float saw = 2.0f * ph - 1.0f;
        lp1 += (saw - lp1) * 0.25f;
        lp2 += (lp1 - lp2) * 0.25f;
        const float env = std::min({1.0f, float(i) / float(edge), float(len - i) / float(edge)});
        out.push_back(level * env * lp2 + hiss());
    }
    for (int i = 0; i < int(after * kRate); ++i) out.push_back(hiss());
    return out;
}
std::vector<float> run(Mixer& m, int frames) {
    std::vector<float> out(size_t(frames) * 2);
    m.render(out.data(), frames);
    return out;
}
// Upward zero crossings per second in the left channel: the pitch of a sine.
float crossings(const std::vector<float>& stereo, int rate) {
    int count = 0;
    for (size_t i = 2; i + 1 < stereo.size(); i += 2)
        if (stereo[i - 2] < 0.0f && stereo[i] >= 0.0f) ++count;
    return float(count) / (float(stereo.size() / 2) / float(rate));
}
std::shared_ptr<const EngineClip> sine_clip(float hz) {
    // Whole periods only, so it loops cleanly.
    auto c = std::make_shared<EngineClip>();
    const int period = int(std::lround(float(kRate) / hz));
    for (int i = 0; i < period * 40; ++i) c->samples.push_back(0.3f * std::sin(2.0f * kPi * float(i) / float(period)));
    return c;
}
float level_for(Mixer& m) {
    run(m, kRate / 2); // let every glide settle
    const auto out = run(m, kRate / 2);
    return rms(out);
}
} // namespace

TEST_CASE("a held ahhh loops whole, without the room around it, at a steady loudness") {
    const auto take = fake_ahh(0.4f, 1.5f, 0.4f);
    const EngineClip c = make_loop(take, kRate);
    REQUIRE_FALSE(c.samples.empty());
    CHECK(c.rate == kRate);
    // All of it, start to end, and none of the 0.8 s of room noise.
    CHECK(c.seconds() > 1.4f);
    CHECK(c.seconds() < 1.6f);
    CHECK(finite(c.samples));
    CHECK(rms(c.samples) == doctest::Approx(0.22f).epsilon(0.1));
    CHECK(seam_ratio(c.samples) < 1.2f);
}

TEST_CASE("a word with a pause between its syllables loops whole") {
    // "ba ... NAAN": two bursts with 0.15 s of quiet between them.
    std::vector<float> take = fake_ahh(0.3f, 0.2f, 0.15f, 180.0f);
    const auto second = fake_ahh(0.0f, 0.25f, 0.3f, 130.0f);
    take.insert(take.end(), second.begin(), second.end());
    const EngineClip c = make_loop(take, kRate);
    REQUIRE_FALSE(c.samples.empty());
    CHECK(c.seconds() > 0.55f); // both syllables and the pause, not just the longer one
    CHECK(c.seconds() < 0.7f);
    CHECK(seam_ratio(c.samples) < 1.2f);
}

TEST_CASE("a take with no quiet around it still makes a loop") {
    const EngineClip c = make_loop(fake_ahh(0.0f, 1.0f, 0.0f), kRate);
    REQUIRE_FALSE(c.samples.empty());
    CHECK(seam_ratio(c.samples) < 1.2f);
}

TEST_CASE("silence, a blip or a murmur makes no loop") {
    CHECK(make_loop(std::vector<float>(kRate * 2, 0.0f), kRate).samples.empty());
    CHECK(make_loop(fake_ahh(0.8f, 0.12f, 0.8f), kRate).samples.empty());              // a cough: too short
    CHECK(make_loop(fake_ahh(0.4f, 1.0f, 0.4f, 140.0f, 0.004f), kRate).samples.empty()); // too quiet to be anything
    CHECK(make_loop(std::vector<float>(100, 0.5f), kRate).samples.empty());
}

TEST_CASE("a soft last syllable stays, even when the word fills the whole take") {
    // "VROOM-pa": loud, then soft, with no quiet anywhere to tell the room
    // by. The soft part is the word's own, not the room, so it stays.
    std::vector<float> take = fake_ahh(0.0f, 0.3f, 0.0f, 140.0f, 0.4f);
    const auto soft = fake_ahh(0.0f, 0.2f, 0.02f, 110.0f, 0.07f);
    take.insert(take.end(), soft.begin(), soft.end());
    const EngineClip c = make_loop(take, kRate);
    REQUIRE_FALSE(c.samples.empty());
    CHECK(c.seconds() > 0.45f);
}

TEST_CASE("a long take is cut at the limit") {
    const EngineClip c = make_loop(fake_ahh(0.2f, 5.0f, 0.2f), kRate);
    REQUIRE_FALSE(c.samples.empty());
    CHECK(c.seconds() <= kMaxLoopSeconds + 0.01f);
    CHECK(c.seconds() >= kMaxLoopSeconds - 0.05f);
}

TEST_CASE("every bot sings its own engine, the same every time") {
    std::vector<EngineClip> bots;
    for (uint32_t seat = 0; seat < 12; ++seat) {
        CAPTURE(seat);
        bots.push_back(make_bot_clip(seat, kRate));
        const EngineClip& c = bots.back();
        CHECK(c.seconds() > 0.5f);
        CHECK(finite(c.samples));
        CHECK(rms(c.samples) == doctest::Approx(0.22f).epsilon(0.1));
        CHECK(seam_ratio(c.samples) < 1.2f);
    }
    CHECK(make_bot_clip(5, kRate).samples == bots[5].samples);
    // No two alike: however one is slid against another, they never line up.
    const size_t n = 2048;
    for (size_t a = 0; a < bots.size(); ++a)
        for (size_t b = a + 1; b < bots.size(); ++b) {
            float best = 0.0f;
            for (int lag = -200; lag <= 200; lag += 2) {
                double dot = 0.0, ea = 0.0, eb = 0.0;
                for (size_t i = 300; i < 300 + n; ++i) {
                    const double x = bots[a].samples[i], y = bots[b].samples[size_t(int(i) + lag)];
                    dot += x * y, ea += x * x, eb += y * y;
                }
                best = std::max(best, float(dot / std::sqrt(ea * eb + 1e-12)));
            }
            CAPTURE(a);
            CAPTURE(b);
            CHECK(best < 0.9f);
        }
}

TEST_CASE("a recording crosses the wire small and close to how it left") {
    auto clip = sine_clip(300.0f);
    const auto wire = encode_clip(*clip);
    CHECK(wire.size() <= kMaxWireBytes);
    CHECK(float(wire.size()) == doctest::Approx(clip->seconds() * float(kWireRate)).epsilon(0.01));
    const EngineClip back = decode_clip(wire);
    CHECK(back.rate == kWireRate);
    CHECK(rms(back.samples) == doctest::Approx(0.22f).epsilon(0.1)); // levelled on the way in
    // Same shape: against a 300 Hz sine at the wire rate, almost perfectly correlated.
    double dot = 0.0, ea = 0.0, eb = 0.0, best = 0.0;
    for (int phase = 0; phase < 54; ++phase) {
        dot = ea = eb = 0.0;
        for (size_t i = 0; i < back.samples.size(); ++i) {
            const double ref = std::sin(2.0 * kPi * 300.0 * double(int(i) + phase) / double(kWireRate));
            dot += back.samples[i] * ref, ea += double(back.samples[i]) * back.samples[i], eb += ref * ref;
        }
        best = std::max(best, dot / std::sqrt(ea * eb));
    }
    CHECK(best > 0.98);
}

TEST_CASE("too much for the wire is looped short, and an oversized one is cut on arrival") {
    EngineClip long_clip;
    for (int i = 0; i < kRate * 3; ++i) long_clip.samples.push_back(0.3f * std::sin(2.0f * kPi * 200.0f * float(i) / float(kRate)));
    const auto wire = encode_clip(long_clip);
    CHECK(wire.size() <= kMaxWireBytes);
    CHECK(wire.size() > kMaxWireBytes / 2);
    const std::vector<uint8_t> flood(kMaxWireBytes * 4, 0x80);
    CHECK(decode_clip(flood).samples.size() == kMaxWireBytes);
}

TEST_CASE("a WAV file keeps the engine between sessions") {
    const auto dir = std::filesystem::temp_directory_path();
    const std::string path = (dir / "gk_test_engine.wav").string();
    const EngineClip c = make_loop(fake_ahh(0.3f, 1.0f, 0.3f), kRate);
    REQUIRE(save_wav(path, c));
    const auto back = load_wav(path);
    REQUIRE(back.has_value());
    CHECK(back->rate == c.rate);
    REQUIRE(back->samples.size() == c.samples.size());
    float worst = 0.0f;
    for (size_t i = 0; i < c.samples.size(); ++i) worst = std::max(worst, std::abs(back->samples[i] - c.samples[i]));
    CHECK(worst < 2.0f / 32767.0f);
    // Anything that is not a WAV is turned away, not read as one.
    const std::string junk = (dir / "gk_test_junk.wav").string();
    {
        std::ofstream f(junk, std::ios::binary);
        f << "RIFF\x10\0\0\0WAVEfmt nonsense";
    }
    CHECK_FALSE(load_wav(junk).has_value());
    CHECK_FALSE(load_wav((dir / "gk_test_missing.wav").string()).has_value());
    std::filesystem::remove(path);
    std::filesystem::remove(junk);
}

TEST_CASE("Doppler raises a kart coming closer and lowers one going away") {
    constexpr float c = 70.0f;
    CHECK(doppler_factor(0.0f, 0.0f, c) == doctest::Approx(1.0f));
    CHECK(doppler_factor(10.0f, 0.0f, c) == doctest::Approx(70.0f / 60.0f));
    CHECK(doppler_factor(-10.0f, 0.0f, c) == doctest::Approx(70.0f / 80.0f));
    CHECK(doppler_factor(0.0f, 10.0f, c) == doctest::Approx(80.0f / 70.0f));
    CHECK(doppler_factor(0.0f, -10.0f, c) == doctest::Approx(60.0f / 70.0f));
    // Passing at 5 m/s over the pack, the cartoon speed of sound makes a jump you can hear.
    CHECK(doppler_factor(5.0f, 0.0f, c) / doppler_factor(-5.0f, 0.0f, c) > 1.1f);
    const float wild = doppler_factor(1000.0f, -1000.0f, c);
    CHECK(std::isfinite(wild));
    CHECK(wild <= 2.0f);
    CHECK(doppler_factor(-1000.0f, -1000.0f, c) >= 0.5f);
}

TEST_CASE("a recorded engine plays about as loud as the synth it replaces") {
    Mixer synth;
    synth.set_volumes({1.0f, 0.0f, 1.0f});
    EngineState e;
    e.active = true;
    e.speed = 0.5f;
    e.throttle = 1.0f;
    synth.set_engine(e);
    const float synth_level = level_for(synth);

    Mixer voiced;
    voiced.set_volumes({1.0f, 0.0f, 1.0f});
    e.voiced = true;
    voiced.set_engine(e);
    auto clip = std::make_shared<const EngineClip>(make_loop(fake_ahh(0.3f, 1.2f, 0.3f), kRate));
    KartEngine k;
    k.clip = clip;
    k.speed = 0.5f;
    k.throttle = 1.0f;
    k.gain = kLocalEngineGain;
    voiced.set_kart_engines({&k, 1});
    const float voiced_level = level_for(voiced);
    CAPTURE(synth_level);
    CAPTURE(voiced_level);
    CHECK(voiced_level > synth_level * 0.6f);
    CHECK(voiced_level < synth_level * 1.6f);
}

TEST_CASE("a kart engine rises in pitch with speed and Doppler, and fades when it goes") {
    auto pitch_of = [](float speed, float doppler) {
        Mixer m;
        m.set_volumes({1.0f, 0.0f, 1.0f});
        KartEngine k;
        k.clip = sine_clip(200.0f);
        k.speed = speed;
        k.throttle = 1.0f;
        k.doppler = doppler;
        k.gain = 1.0f;
        m.set_kart_engines({&k, 1});
        run(m, kRate / 2);
        return crossings(run(m, kRate), kRate);
    };
    const float idle = pitch_of(0.0f, 1.0f), flat_out = pitch_of(1.0f, 1.0f);
    CHECK(flat_out == doctest::Approx(200.0f).epsilon(0.02)); // flat out on the gas: the voice's own pitch
    CHECK(idle == doctest::Approx(200.0f * engine_revs(0.0f, 1.0f)).epsilon(0.02));
    CHECK(idle < 0.8f * flat_out);                              // standing still: lower
    CHECK(pitch_of(1.15f, 1.0f) > flat_out);                    // boosting: a touch higher
    CHECK(pitch_of(1.15f, 1.0f) < 1.1f * flat_out);
    CHECK(pitch_of(0.0f, 1.2f) / idle == doctest::Approx(1.2f).epsilon(0.02));

    Mixer m;
    m.set_volumes({1.0f, 0.0f, 1.0f});
    std::vector<KartEngine> all(kKartEngines);
    for (int i = 0; i < kKartEngines; ++i) {
        all[size_t(i)].clip = std::make_shared<const EngineClip>(make_bot_clip(uint32_t(i), kRate));
        all[size_t(i)].gain = 0.6f;
        all[size_t(i)].speed = 1.2f;
        all[size_t(i)].throttle = 1.0f;
        all[size_t(i)].pan = float(i) / 6.0f - 1.0f;
    }
    m.set_kart_engines(all);
    const auto loud = run(m, kRate / 2);
    CHECK(finite(loud));
    CHECK(rms(loud) > 0.05f);
    for (float x : loud) CHECK(std::abs(x) <= 1.0f);
    m.set_kart_engines({});
    const auto fading = run(m, kRate / 2);
    CHECK(rms(fading, 0, 960) > 0.01f);               // not cut off: it fades
    CHECK(rms(fading, fading.size() - 9600) < 1e-3f); // and is silent by the end
}
