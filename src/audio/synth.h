#pragma once
// Tiny building blocks for the synthesiser: oscillators with their own phase,
// white noise and one-pole filters.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gk::audio {

inline constexpr float kTau = 6.28318530718f;

// PolyBLEP correction: smooths the jump of a saw or square so it does not
// alias into the harsh, buzzy "8-bit" sound.
inline float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

struct Osc {
    float phase = 0.0f; // 0..1

    float advance(float freq, int rate) {
        const float p = phase;
        phase += freq / float(rate);
        phase -= std::floor(phase);
        return p;
    }
    float sine(float f, int rate) { return std::sin(kTau * advance(f, rate)); }
    float saw(float f, int rate) { return 2.0f * advance(f, rate) - 1.0f; }
    float pulse(float f, float duty, int rate) { return advance(f, rate) < duty ? 1.0f : -1.0f; }
    float triangle(float f, int rate) {
        const float p = advance(f, rate);
        return 4.0f * std::abs(p - 0.5f) - 1.0f;
    }
    // Band-limited saw and square.
    float saw_bl(float f, int rate) {
        const float dt = std::fmin(0.5f, f / float(rate));
        const float p = advance(f, rate);
        return 2.0f * p - 1.0f - poly_blep(p, dt);
    }
    float square_bl(float f, float duty, int rate) {
        const float dt = std::fmin(0.5f, f / float(rate));
        const float p = advance(f, rate);
        float v = p < duty ? 1.0f : -1.0f;
        v += poly_blep(p, dt);
        float q = p - duty;
        if (q < 0.0f) q += 1.0f;
        v -= poly_blep(q, dt);
        return v;
    }
};

// Two-operator FM: a sine whose phase is pushed by another sine at `ratio`
// times its frequency, `index` deep. Bells, electric pianos, plucks.
struct Fm {
    Osc car, mod;
    float sample(float f, float ratio, float index, int rate) {
        const float m = std::sin(kTau * mod.advance(f * ratio, rate));
        const float p = car.advance(f, rate);
        return std::sin(kTau * p + index * m);
    }
};

// State-variable filter (Chamberlin, oversampled twice for stability):
// low, band and high outputs, cutoff in Hz, resonance 0 (none) .. ~0.95.
struct Svf {
    float low = 0.0f, band = 0.0f;
    float lp(float x, float cutoff, float res, int rate) {
        run(x, cutoff, res, rate);
        return low;
    }
    float bp(float x, float cutoff, float res, int rate) {
        run(x, cutoff, res, rate);
        return band;
    }
    float hp(float x, float cutoff, float res, int rate) {
        const float q = 1.0f - std::fmin(res, 0.97f);
        run(x, cutoff, res, rate);
        return x - low - q * band;
    }

private:
    void run(float x, float cutoff, float res, int rate) {
        const float f = 2.0f * std::sin(3.14159265f * std::fmin(cutoff, float(rate) * 0.2f) / (2.0f * float(rate)));
        const float q = 1.0f - std::fmin(res, 0.97f);
        for (int i = 0; i < 2; ++i) {
            low += f * band;
            const float high = x - low - q * band;
            band += f * high;
        }
    }
};

// A small stereo reverb: four combs and two all-passes per side, with
// different lengths left and right for width (Freeverb, cut down).
struct Reverb {
    struct Comb {
        std::vector<float> buf;
        size_t i = 0;
        float store = 0.0f;
        float process(float x, float feedback, float damp) {
            const float y = buf[i];
            store = y * (1.0f - damp) + store * damp;
            buf[i] = x + store * feedback;
            if (++i >= buf.size()) i = 0;
            return y;
        }
    };
    struct AllPass {
        std::vector<float> buf;
        size_t i = 0;
        float process(float x) {
            const float y = buf[i];
            buf[i] = x + y * 0.5f;
            if (++i >= buf.size()) i = 0;
            return y - x;
        }
    };
    Comb cl[4], cr[4];
    AllPass al[2], ar[2];
    float feedback = 0.8f, damp = 0.35f;

    explicit Reverb(int rate = 48000, float size = 1.0f) {
        static constexpr int kComb[4] = {1116, 1188, 1277, 1356}, kAll[2] = {556, 441};
        const float k = float(rate) / 44100.0f * size;
        for (int j = 0; j < 4; ++j) {
            cl[j].buf.assign(size_t(float(kComb[j]) * k), 0.0f);
            cr[j].buf.assign(size_t(float(kComb[j] + 23) * k), 0.0f);
        }
        for (int j = 0; j < 2; ++j) {
            al[j].buf.assign(size_t(float(kAll[j]) * k), 0.0f);
            ar[j].buf.assign(size_t(float(kAll[j] + 23) * k), 0.0f);
        }
    }
    void process(float in, float& l, float& r) {
        l = r = 0.0f;
        for (int j = 0; j < 4; ++j) {
            l += cl[j].process(in, feedback, damp);
            r += cr[j].process(in, feedback, damp);
        }
        for (int j = 0; j < 2; ++j) {
            l = al[j].process(l);
            r = ar[j].process(r);
        }
        l *= 0.25f;
        r *= 0.25f;
    }
};

// A delay line in samples, for echoes.
struct Delay {
    std::vector<float> buf;
    size_t i = 0;
    explicit Delay(size_t n = 1) : buf(std::max<size_t>(n, 1), 0.0f) {}
    float process(float x, float feedback) {
        const float y = buf[i];
        buf[i] = x + y * feedback;
        if (++i >= buf.size()) i = 0;
        return y;
    }
};

struct Noise {
    uint32_t state = 0x9e3779b9u;
    float next() { // -1..1
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state >> 8) / float(1u << 23) - 1.0f;
    }
};

struct OnePole {
    float z = 0.0f;
    float low(float x, float k) { // k in 0..1: higher = brighter
        z += (x - z) * std::fmin(1.0f, std::fmax(0.0f, k));
        return z;
    }
    float high(float x, float k) { return x - low(x, k); }
};

inline float midi_hz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

} // namespace gk::audio
