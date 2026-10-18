#pragma once
// A procedurally written song loop: chord progression, bass line, lead
// melody, arpeggio, a pad and drums, all derived from a seed and a mood.
// Voiced with band-limited analogue-style synths, FM plucks and bells, a
// resonant filter, echo on the lead and a stereo reverb.
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
        Osc osc, osc2, sub;
        Fm fm;
        Svf filter;
        float freq = 0.0f, t = 0.0f, len = 0.0f;
        bool on = false;
    };
    void trigger_step(int step);

    int rate_;
    double clock_ = 0.0; // in steps
    int last_step_ = -1;
    int arp_count_ = 0;
    Tone lead_v_, bass_v_, arp_v_;
    Osc vib_, pad_lfo_;
    std::array<Osc, 6> pad_osc_{};
    std::array<float, 3> pad_freq_{}, pad_target_{};
    Svf pad_filter_;
    float pad_amp_ = 0.0f;
    Noise noise_;
    Svf snare_bp_, hat_hp_, kick_click_;
    std::array<Osc, 6> hat_osc_{};
    float kick_t_ = 1.0f, snare_t_ = 1.0f, hat_t_ = 1.0f;
    Osc kick_osc_, snare_osc_, snare_osc2_;
    Reverb reverb_;
    Delay echo_l_, echo_r_;
};

} // namespace gk::audio
