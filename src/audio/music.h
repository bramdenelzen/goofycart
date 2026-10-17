#pragma once
// A procedurally written chiptune loop: chord progression, bass line, lead
// melody, arpeggio and drums, all derived from a seed and a mood.
#include "gk/audio/mixer.h"
#include "synth.h"

#include <array>
#include <vector>

namespace gk::audio {

struct Mixer::Song {
    Song(uint32_t seed, Mood mood, int rate);

    // One stereo sample; `speed` scales the tempo.
    void sample(float speed, float& l, float& r);

    float bpm = 120.0f;
    static constexpr int kBars = 8;
    static constexpr int kSteps = kBars * 16; // 16th notes in the loop
    std::array<int8_t, kSteps> lead{};        // MIDI note starting on this step, or -1
    std::array<uint8_t, kSteps> lead_len{};   // in steps
    std::array<int8_t, kSteps> bass{};
    std::array<std::array<int8_t, 3>, kBars> chords{};
    std::array<uint16_t, 2> kick{}, snare{}, hat{}; // [0] normal bar, [1] the last bar (a fill)
    bool arp = true;
    int lead_wave = 0; // 0 pulse, 1 triangle, 2 saw
    float lead_duty = 0.25f;

private:
    struct Tone {
        Osc osc;
        float freq = 0.0f, t = 0.0f, len = 0.0f;
        bool on = false;
    };
    void trigger_step(int step);

    int rate_;
    double clock_ = 0.0; // in steps
    int last_step_ = -1;
    Tone lead_v_, bass_v_, arp_v_;
    Osc vib_;
    Noise noise_;
    OnePole snare_hp_, hat_hp_, lead_lp_;
    float kick_t_ = 1.0f, snare_t_ = 1.0f, hat_t_ = 1.0f;
    Osc kick_osc_, snare_osc_;
};

} // namespace gk::audio
