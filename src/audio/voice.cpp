#include "gk/audio/voice.h"

#include "synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace gk::audio {
namespace {

inline constexpr float kLoopRms = 0.22f; // every engine loop plays at this loudness

float rms(std::span<const float> s) {
    if (s.empty()) return 0.0f;
    double sum = 0.0;
    for (float x : s) sum += double(x) * double(x);
    return float(std::sqrt(sum / double(s.size())));
}

// Evens out the loudness: every engine plays at the same level whether it was
// whispered or shouted, and no peak clips.
void normalise(std::vector<float>& v, float target_rms) {
    const float r = rms(v);
    if (r < 1e-6f) return;
    float peak = 0.0f;
    for (float x : v) peak = std::max(peak, std::abs(x));
    const float g = std::min(target_rms / r, 0.95f / peak);
    for (float& x : v) x *= g;
}

// A seamless loop from `s`: s[0, n), with the run-on just past its end,
// s[n, n + fade), crossfaded into its start so the end flows on into the
// beginning. n is moved back (by up to `search`) to where that run-on lines
// up best with the start, so the crossfade blends two waves in step rather
// than two that beat against each other.
std::vector<float> seamless(std::span<const float> s, int fade, int search) {
    const int total = int(s.size());
    fade = std::clamp(fade, 1, std::max(1, total / 3));
    search = std::clamp(search, 0, std::max(0, total / 3));
    const int window = std::min(fade, 1024);
    int best_n = total - fade;
    double best = -2.0;
    for (int n = total - fade - search; n <= total - fade; ++n) {
        if (n < fade) continue;
        double dot = 0.0, ea = 0.0, eb = 0.0;
        for (int i = 0; i < window; ++i) {
            const double a = s[size_t(i)], b = s[size_t(n + i)];
            dot += a * b;
            ea += a * a;
            eb += b * b;
        }
        const double c = dot / std::sqrt(ea * eb + 1e-12);
        if (c > best) best = c, best_n = n;
    }
    std::vector<float> out(s.begin(), s.begin() + best_n);
    for (int i = 0; i < fade && i < best_n; ++i) {
        // Raised cosine: the two weights sum to one, which suits waves in step.
        const float w = 0.5f - 0.5f * std::cos(0.5f * kTau * (float(i) + 0.5f) / float(fade));
        out[size_t(i)] = s[size_t(i)] * w + s[size_t(best_n + i)] * (1.0f - w);
    }
    return out;
}

// Resamples a loop to `rate`, treating it as periodic so its seam survives.
// Going down, a low-pass first so the voice's top end does not fold back.
std::vector<float> resample_loop(const EngineClip& c, int rate) {
    const size_t n = c.samples.size();
    if (n < 2 || c.rate <= 0 || rate <= 0) return {};
    std::vector<float> src = c.samples;
    if (rate < c.rate) {
        // Two gentle low-passes, run round the loop twice so its start is
        // filtered as if the end had just played.
        const float cutoff = 0.4f * float(rate);
        Svf a, b;
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < n; ++i) {
                const float y = b.lp(a.lp(c.samples[i], cutoff, 0.0f, c.rate), cutoff, 0.0f, c.rate);
                if (pass == 1) src[i] = y;
            }
    }
    const size_t m = std::max<size_t>(2, size_t(std::llround(double(n) * double(rate) / double(c.rate))));
    std::vector<float> out(m);
    for (size_t j = 0; j < m; ++j) {
        const double p = double(j) * double(n) / double(m);
        const size_t i0 = size_t(p) % n, i1 = (i0 + 1) % n;
        const float f = float(p - std::floor(p));
        out[j] = src[i0] * (1.0f - f) + src[i1] * f;
    }
    return out;
}

// G.711 mu-law: 8 bits that keep quiet detail and loud peaks alike.
uint8_t mulaw(float x) {
    int s = int(std::clamp(x, -1.0f, 1.0f) * 32635.0f);
    const int sign = s < 0 ? 0x80 : 0;
    s = std::abs(s) + 0x84;
    int exponent = 7;
    for (int mask = 0x4000; (s & mask) == 0 && exponent > 0; mask >>= 1) --exponent;
    return uint8_t(~(sign | (exponent << 4) | ((s >> (exponent + 3)) & 0x0f)));
}
float unmulaw(uint8_t u) {
    u = uint8_t(~u);
    const int exponent = (u >> 4) & 7;
    const int s = (((u & 0x0f) << 3) + 0x84) << exponent;
    return float((u & 0x80) ? 0x84 - s : s - 0x84) / 32635.0f;
}

void put16(std::ofstream& f, uint32_t v) {
    const char b[2] = {char(v & 0xff), char((v >> 8) & 0xff)};
    f.write(b, 2);
}
void put32(std::ofstream& f, uint32_t v) {
    put16(f, v);
    put16(f, v >> 16);
}

} // namespace

EngineClip make_loop(std::span<const float> take, int rate) {
    EngineClip clip;
    clip.rate = rate;
    if (rate <= 0 || float(take.size()) < float(rate) * kMinSoundSeconds) return clip;
    // Rumble, wind and the microphone's DC offset out: a high-pass near 70 Hz.
    std::vector<float> x(take.begin(), take.end());
    OnePole dc;
    const float k = 1.0f - std::exp(-kTau * 70.0f / float(rate));
    for (float& v : x) v = dc.high(v, k);
    // Loudness in 10 ms windows. What was said runs from the first window
    // clearly above the room to the last, gaps between syllables included;
    // soft consonants count, so the line sits well below the loudest part.
    const int win = std::max(1, rate / 100);
    const int windows = int(x.size()) / win;
    std::vector<float> env(static_cast<size_t>(windows));
    for (int w = 0; w < windows; ++w) env[size_t(w)] = rms(std::span<const float>(x).subspan(size_t(w * win), size_t(win)));
    std::vector<float> sorted = env;
    std::sort(sorted.begin(), sorted.end());
    const float loudest = sorted.back(), room = sorted[sorted.size() / 20];
    if (loudest < 0.01f) return clip; // nothing but the room
    // Never far up the loudest part: when the talking fills the whole take,
    // "the room" is really the softest bit of the word, and a line set from
    // it would cut the word's soft start and end off.
    const float threshold = std::min(std::max(room * 3.0f, loudest * 0.08f), loudest * 0.15f);
    int a = 0;
    while (a < windows && env[size_t(a)] < threshold) ++a;
    int b = windows;
    while (b > a && env[size_t(b - 1)] < threshold) --b;
    if (float((b - a) * win) < float(rate) * kMinSoundSeconds) return clip;
    // A breath of room either side, so a soft start or end is not clipped.
    const int pad = 2;
    const int start = std::max(0, a - pad) * win;
    int end = std::min(windows, b + pad) * win;
    // The end crossfades into the start; past the longest loop the rest is cut.
    const int fade = rate * 3 / 100, search = rate / 100;
    end = std::min(end, start + int(float(rate) * kMaxLoopSeconds) + fade);
    clip.samples = seamless(std::span<const float>(x).subspan(size_t(start), size_t(end - start)), fade, search);
    normalise(clip.samples, kLoopRms);
    return clip;
}

EngineClip make_bot_clip(uint32_t seed, int rate) {
    // The seed picks a singer: the vowel (its three formants), the pitch, how
    // big the voice is (a smaller head lifts the formants), a vibrato,
    // sometimes a two-stroke putter, and how breathy and reedy it sounds. The
    // vowel and the pitch step through seats so no two of twelve match.
    struct Vowel {
        float f1, f2, f3;
    };
    static constexpr Vowel kVowels[] = {
        {730.0f, 1090.0f, 2440.0f}, // ah
        {530.0f, 1840.0f, 2480.0f}, // eh
        {270.0f, 2290.0f, 3010.0f}, // ee
        {570.0f, 840.0f, 2410.0f},  // oh
        {300.0f, 870.0f, 2240.0f},  // oo
        {660.0f, 1720.0f, 2410.0f}, // a, as in cat
        {490.0f, 1350.0f, 1690.0f}, // er
    };
    Noise dice{seed * 2654435761u + 0x6d2b79f5u};
    for (int i = 0; i < 4; ++i) dice.next(); // stir, so neighbouring seeds start far apart
    auto pick = [&] { return 0.5f * (dice.next() + 1.0f); };
    const Vowel v = kVowels[(seed * 3u) % 7u];
    const float f0 = 75.0f * std::exp2(float((seed * 7u) % 24u) / 12.0f) * (1.0f + 0.02f * (pick() - 0.5f));
    const float size = 0.9f + 0.25f * pick();
    const float vib_rate = 4.0f + 3.0f * pick(), vib_depth = 0.004f + 0.018f * pick();
    const float putter = pick() < 0.4f ? 12.0f + 18.0f * pick() : 0.0f;
    const float breath = 0.02f + 0.1f * pick();
    const float reed = pick();

    Osc saw, pulse, vib, put;
    Svf s1, s2, s3;
    Noise air{seed ^ 0xa5a5a5a5u};
    const int settle = rate * 3 / 10;                       // filters warm up before anything is kept
    const int length = rate * 9 / 10 + rate / 10 + rate / 50; // a 0.9 s loop, its crossfade and the search
    std::vector<float> out;
    out.reserve(size_t(length));
    for (int i = 0; i < settle + length; ++i) {
        const float f = f0 * (1.0f + vib_depth * vib.sine(vib_rate, rate));
        const float glottal = saw.saw_bl(f, rate) + reed * 0.6f * pulse.square_bl(f, 0.25f, rate);
        const float exc = glottal + breath * air.next();
        float y = s1.bp(exc, v.f1 * size, 0.88f, rate) + 0.7f * s2.bp(exc, v.f2 * size, 0.9f, rate) +
                  0.3f * s3.bp(exc, v.f3 * size, 0.9f, rate);
        if (putter > 0.0f) y *= 0.55f + 0.45f * std::abs(put.sine(putter, rate));
        if (i >= settle) out.push_back(y);
    }
    EngineClip clip;
    clip.rate = rate;
    clip.samples = seamless(out, rate / 10, rate / 50);
    normalise(clip.samples, kLoopRms);
    return clip;
}

std::vector<uint8_t> encode_clip(const EngineClip& c) {
    std::vector<float> s = resample_loop(c, kWireRate);
    // Longer than any loop make_loop keeps: loop the first part again rather
    // than cut it, which would leave a seam.
    if (s.size() > kMaxWireBytes) s = seamless(std::span<const float>(s).first(kMaxWireBytes), kWireRate / 20, kWireRate / 50);
    std::vector<uint8_t> out(s.size());
    for (size_t i = 0; i < s.size(); ++i) out[i] = mulaw(s[i]);
    return out;
}

EngineClip decode_clip(std::span<const uint8_t> wire) {
    EngineClip c;
    c.rate = kWireRate;
    c.samples.resize(std::min(wire.size(), kMaxWireBytes));
    for (size_t i = 0; i < c.samples.size(); ++i) c.samples[i] = unmulaw(wire[i]);
    // Someone else's recording: level it like our own, so nobody can blast.
    normalise(c.samples, kLoopRms);
    return c;
}

bool save_wav(const std::string& path, const EngineClip& c) {
    if (c.samples.empty() || c.rate <= 0) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t bytes = uint32_t(c.samples.size() * 2);
    f.write("RIFF", 4);
    put32(f, 36 + bytes);
    f.write("WAVEfmt ", 8);
    put32(f, 16);
    put16(f, 1); // PCM
    put16(f, 1); // mono
    put32(f, uint32_t(c.rate));
    put32(f, uint32_t(c.rate) * 2);
    put16(f, 2);
    put16(f, 16);
    f.write("data", 4);
    put32(f, bytes);
    for (float x : c.samples) put16(f, uint32_t(uint16_t(int16_t(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f)))));
    return bool(f);
}

std::optional<EngineClip> load_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8; };
    auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return std::nullopt;
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t data_at = 0, data_len = 0;
    for (size_t at = 12; at + 8 <= d.size();) {
        const size_t body = at + 8;
        const size_t len = u32(at + 4);
        const bool is_data = std::memcmp(d.data() + at, "data", 4) == 0;
        if (len > d.size() - body) { // a cut-off last chunk: take what is there
            if (is_data) data_at = body, data_len = d.size() - body;
            break;
        }
        if (std::memcmp(d.data() + at, "fmt ", 4) == 0 && len >= 16) {
            format = int(u16(body));
            channels = int(u16(body + 2));
            rate = u32(body + 4);
            bits = int(u16(body + 14));
            if (format == 0xFFFE && len >= 26) format = int(u16(body + 24)); // extensible: the sub-format's tag
        } else if (is_data) {
            data_at = body, data_len = len;
        }
        at = body + len + (len & 1); // chunks pad to an even size
    }
    const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool flt = format == 3 && bits == 32;
    if (!(pcm || flt) || channels < 1 || channels > 8 || rate < 4000 || rate > 192000 || data_at == 0) return std::nullopt;
    const size_t width = size_t(bits / 8), frame = size_t(channels) * width;
    const size_t frames = std::min(data_len / frame, size_t(rate) * 60);
    EngineClip c;
    c.rate = int(rate);
    c.samples.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const size_t p = data_at + i * frame + size_t(ch) * width;
            float s = 0.0f;
            if (flt) {
                const uint32_t u = u32(p);
                float v;
                std::memcpy(&v, &u, 4);
                s = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
            } else if (bits == 8) {
                s = (float(d[p]) - 128.0f) / 128.0f;
            } else if (bits == 16) {
                s = float(int16_t(uint16_t(u16(p)))) / 32768.0f;
            } else if (bits == 24) {
                const int32_t v = int32_t((uint32_t(d[p]) << 8) | (uint32_t(d[p + 1]) << 16) | (uint32_t(d[p + 2]) << 24)) >> 8;
                s = float(v) / 8388608.0f;
            } else {
                s = float(int32_t(u32(p))) / 2147483648.0f;
            }
            sum += s;
        }
        c.samples[i] = sum / float(channels);
    }
    return c;
}

float doppler_factor(float source_toward, float listener_toward, float speed_of_sound) {
    const float c = std::max(speed_of_sound, 1.0f);
    // A source closing at nearly the speed of sound would send the pitch to
    // infinity; nothing in a kart race should, so both speeds are held well short.
    const float s = std::min(source_toward, 0.8f * c);
    const float l = std::max(listener_toward, -0.8f * c);
    return std::clamp((c + l) / (c - s), 0.5f, 2.0f);
}

} // namespace gk::audio
