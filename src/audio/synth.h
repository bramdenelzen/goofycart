#pragma once
// Tiny building blocks for the synthesiser: oscillators with their own phase,
// white noise and one-pole filters.
#include <cmath>
#include <cstdint>

namespace gk::audio {

inline constexpr float kTau = 6.28318530718f;

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
