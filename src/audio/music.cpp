#include "music.h"

#include <algorithm>
#include <cmath>

namespace gk::audio {
namespace {

struct MoodStyle {
    std::array<int, 7> scale;
    float bpm;
    int root;        // MIDI note of the key
    int lead_wave;   // 0 pulse, 1 triangle, 2 saw
    float lead_duty;
    float density;   // chance of a lead note on an off-beat
    bool arp;
    uint16_t kick, snare, hat;
};

constexpr std::array<int, 7> kMajor{0, 2, 4, 5, 7, 9, 11}, kMinor{0, 2, 3, 5, 7, 8, 10},
    kDorian{0, 2, 3, 5, 7, 9, 10}, kMixo{0, 2, 4, 5, 7, 9, 10}, kLydian{0, 2, 4, 6, 7, 9, 11},
    kHarmonic{0, 2, 3, 5, 7, 8, 11}, kPhrygDom{0, 1, 4, 5, 7, 8, 10};

constexpr uint16_t bits(std::initializer_list<int> steps) {
    uint16_t m = 0;
    for (int s : steps) m = uint16_t(m | (1u << s));
    return m;
}

MoodStyle style(Mood m) {
    const uint16_t backbeat = bits({4, 12}), eighths = bits({0, 2, 4, 6, 8, 10, 12, 14});
    switch (m) {
    case Mood::Bright: return {kMajor, 140, 60, 0, 0.25f, 0.45f, true, bits({0, 8, 10}), backbeat, eighths};
    case Mood::Dusty: return {kPhrygDom, 116, 62, 0, 0.5f, 0.35f, false, bits({0, 6, 8}), backbeat, eighths};
    case Mood::Breezy: return {kMixo, 128, 65, 1, 0.5f, 0.4f, true, bits({0, 7, 8}), backbeat, eighths};
    case Mood::Frosty: return {kMajor, 108, 67, 1, 0.5f, 0.3f, true, bits({0, 8}), backbeat, bits({0, 4, 8, 12})};
    case Mood::Driving: return {kDorian, 148, 57, 2, 0.5f, 0.5f, true, bits({0, 4, 8, 12}), backbeat, 0xffff};
    case Mood::Fiery: return {kHarmonic, 156, 56, 2, 0.5f, 0.5f, false, bits({0, 3, 8, 11}), backbeat, eighths};
    case Mood::Airy: return {kLydian, 120, 64, 1, 0.5f, 0.35f, true, bits({0, 10}), backbeat, eighths};
    case Mood::Night: return {kMinor, 132, 58, 2, 0.5f, 0.45f, true, bits({0, 4, 8, 12}), backbeat, bits({2, 6, 10, 14})};
    case Mood::Menu: return {kMajor, 100, 62, 0, 0.125f, 0.3f, true, bits({0, 8}), bits({12}), bits({0, 4, 8, 12})};
    case Mood::Results: return {kMajor, 92, 60, 1, 0.5f, 0.25f, true, bits({0}), 0, bits({0, 8})};
    // The invincibility jingle: fast, bright and busy, four on the floor.
    case Mood::Star: return {kMixo, 184, 64, 0, 0.25f, 0.9f, true, bits({0, 4, 8, 12}), backbeat, 0xffff};
    }
    return style(Mood::Bright);
}

struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    int range(int n) { return int(next() % uint32_t(n)); }
    float unit() { return float(next() >> 8) / float(1u << 24); }
};

// MIDI note of scale degree `d` (any integer) above `root`.
int degree_note(const std::array<int, 7>& scale, int root, int d) {
    const int oct = d >= 0 ? d / 7 : -((-d + 6) / 7);
    return root + 12 * oct + scale[size_t(d - 7 * oct)];
}

} // namespace

Mood mood_for_theme(const std::string& theme) {
    if (theme == "desert") return Mood::Dusty;
    if (theme == "harbor" || theme == "beach") return Mood::Breezy;
    if (theme == "snow") return Mood::Frosty;
    if (theme == "canyon") return Mood::Driving;
    if (theme == "volcano") return Mood::Fiery;
    if (theme == "sky") return Mood::Airy;
    if (theme == "night") return Mood::Night;
    return Mood::Bright;
}

Mixer::Song::Song(uint32_t seed, Mood mood, int rate) : rate_(rate), reverb_(rate, 1.15f) {
    Rng rng{seed * 2654435761u + uint32_t(mood) * 40503u + 1u};
    for (int i = 0; i < 4; ++i) rng.next();
    const MoodStyle st = style(mood);
    bpm = st.bpm + float(rng.range(9)) - 4.0f;
    const int root = st.root + rng.range(5) - 2;
    arp = st.arp;
    lead_wave = st.lead_wave;
    lead_duty = st.lead_duty;
    kick = {st.kick, st.kick};
    snare = {st.snare, uint16_t(st.snare | bits({13, 14, 15}))};
    hat = {st.hat, st.hat};

    // Chords: two four-bar progressions of scale degrees.
    static constexpr int kProgs[][4] = {{0, 4, 5, 3}, {0, 5, 3, 4}, {0, 3, 4, 0}, {5, 3, 0, 4},
                                        {0, 3, 5, 4}, {0, 6, 5, 4}, {3, 4, 0, 0}, {0, 2, 3, 4}};
    const int pa = rng.range(8), pb = rng.range(8);
    std::array<int, kBars> degs{};
    for (int b = 0; b < 4; ++b) degs[size_t(b)] = kProgs[pa][b], degs[size_t(b + 4)] = kProgs[pb][b];
    degs[kBars - 1] = 4; // end on the dominant so the loop turns around
    for (int b = 0; b < kBars; ++b)
        for (int k = 0; k < 3; ++k) chords[size_t(b)][size_t(k)] = int8_t(degree_note(st.scale, root, degs[size_t(b)] + 2 * k));

    // Bass: one pattern per song (root, octave, fifth or rest per step).
    static constexpr int8_t kBassPat[][16] = {
        {0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1},
        {0, -1, 12, -1, 0, -1, 12, -1, 0, -1, 12, -1, 0, -1, 12, -1},
        {0, -1, -1, 0, -1, -1, 7, -1, 0, -1, -1, 0, -1, -1, 7, -1},
        {0, -1, -1, -1, 7, -1, -1, -1, 12, -1, -1, -1, 7, -1, -1, -1},
        {0, 0, -1, 12, 0, -1, 7, -1, 0, 0, -1, 12, 0, -1, 10, -1},
    };
    const int bp = mood == Mood::Results || mood == Mood::Frosty ? 3 : rng.range(5);
    for (int s = 0; s < kSteps; ++s) {
        const int8_t v = kBassPat[bp][s % 16];
        const int8_t r = int8_t(chords[size_t(s / 16)][0] - 24);
        bass[size_t(s)] = int8_t(v < 0 ? -1 : (v == 7 ? chords[size_t(s / 16)][2] - 24 : r + v));
    }

    // Lead: two-bar phrases in the form A A' B A'', a random walk over the
    // scale that lands on chord tones on the beat.
    auto phrase = [&](int bar0, int start_deg, bool end_on_root) {
        int d = start_deg;
        for (int s = 0; s < 32; ++s) {
            const int step = bar0 * 16 + s;
            const bool beat = s % 4 == 0;
            lead[size_t(step)] = -1;
            if (!(beat ? rng.unit() < 0.8f : rng.unit() < st.density * (s % 2 == 0 ? 1.0f : 0.4f))) continue;
            d = std::clamp(d + rng.range(5) - 2, 4, 13);
            int note = degree_note(st.scale, root, d);
            if (beat) { // nearest chord tone
                int best = note, bd = 99;
                for (int8_t c : chords[size_t(step / 16)])
                    for (int o = 0; o <= 24; o += 12)
                        if (std::abs(c + o - note) < bd) bd = std::abs(c + o - note), best = c + o;
                note = best;
            }
            if (end_on_root && s >= 24) {
                if (s == 24) note = chords[size_t(step / 16)][0] + 12;
                else continue;
            }
            lead[size_t(step)] = int8_t(note);
        }
    };
    lead.fill(-1);
    phrase(0, 7 + rng.range(3), false);                 // A
    phrase(2, 8, false);                                // A': A's first bar, new second bar
    std::copy_n(lead.begin(), 16, lead.begin() + 32);
    phrase(4, 9 + rng.range(3), false);                 // B
    phrase(6, 7, true);                                 // A'': ends on the root
    std::copy_n(lead.begin(), 16, lead.begin() + 96);
    // Lengths: hold until the next note, up to a beat.
    for (int s = 0; s < kSteps; ++s) {
        if (lead[size_t(s)] < 0) continue;
        int n = 1;
        while (n < 4 && lead[size_t((s + n) % kSteps)] < 0) ++n;
        lead_len[size_t(s)] = uint8_t(n);
    }
    // Echo on the lead: a dotted eighth on the left, a quarter on the right.
    const float step_s = 60.0f / bpm / 4.0f;
    echo_l_ = Delay(size_t(3.0f * step_s * float(rate_)));
    echo_r_ = Delay(size_t(4.0f * step_s * float(rate_)));
    for (int k = 0; k < 3; ++k) pad_freq_[size_t(k)] = pad_target_[size_t(k)] = midi_hz(float(chords[0][size_t(k)]));
    reverb_.feedback = mood == Mood::Airy || mood == Mood::Frosty || mood == Mood::Night ? 0.84f : 0.78f;
}

void Mixer::Song::trigger_step(int step) {
    const float step_s = 60.0f / bpm / 4.0f;
    if (const int8_t n = lead[size_t(step)]; n >= 0) {
        lead_v_.freq = midi_hz(n);
        lead_v_.t = 0.0f;
        lead_v_.len = float(lead_len[size_t(step)]) * step_s * 0.92f;
        lead_v_.on = true;
    }
    if (const int8_t n = bass[size_t(step)]; n >= 0) {
        bass_v_.freq = midi_hz(n);
        bass_v_.t = 0.0f;
        bass_v_.len = step_s * 1.7f;
        bass_v_.on = true;
    }
    if (arp && step % 2 == 0) {
        const auto& c = chords[size_t(step / 16)];
        static constexpr int kOrder[4] = {0, 1, 2, 1};
        const int k = kOrder[arp_count_++ % 4];
        arp_v_.freq = midi_hz(float(c[size_t(k)] + 12));
        arp_v_.t = 0.0f;
        arp_v_.len = step_s * 1.5f;
        arp_v_.on = true;
    }
    if (step % 16 == 0) // the pad moves to the bar's chord
        for (int k = 0; k < 3; ++k) pad_target_[size_t(k)] = midi_hz(float(chords[size_t(step / 16)][size_t(k)]));
    const int bar = step / 16, s = step % 16;
    const int v = bar == kBars - 1 ? 1 : 0;
    if (kick[size_t(v)] >> s & 1) kick_t_ = 0.0f;
    if (snare[size_t(v)] >> s & 1) snare_t_ = 0.0f;
    if (hat[size_t(v)] >> s & 1) hat_t_ = 0.0f;
}

void Mixer::Song::sample(float speed, float& l, float& r) {
    const double steps_per_sample = double(bpm) * 4.0 / 60.0 / double(rate_) * double(speed);
    const int step = int(clock_) % kSteps;
    if (step != last_step_) {
        trigger_step(step);
        last_step_ = step;
    }
    clock_ += steps_per_sample;
    if (clock_ >= double(kSteps)) clock_ -= double(kSteps);
    const float dt = 1.0f / float(rate_);

    float lead = 0.0f, bass = 0.0f, arp = 0.0f, pad = 0.0f, kick = 0.0f, snare = 0.0f, hat = 0.0f;
    if (lead_v_.on) {
        Tone& v = lead_v_;
        const float vib = 1.0f + 0.006f * std::min(1.0f, std::max(0.0f, v.t - 0.12f) * 4.0f) * vib_.sine(5.2f, rate_);
        const float f = v.freq * vib;
        const float release = v.t < v.len ? 1.0f : std::exp(-(v.t - v.len) * 14.0f);
        const float env = std::min(1.0f, v.t * 120.0f) * (0.75f + 0.25f * std::exp(-v.t * 6.0f)) * release;
        float w;
        if (lead_wave == 1) { // soft FM flute-bell
            w = v.fm.sample(f, 1.0f, 1.2f * std::exp(-v.t * 5.0f) + 0.25f, rate_);
        } else if (lead_wave == 2) { // two detuned saws through a sweeping filter
            const float raw = 0.5f * (v.osc.saw_bl(f * 1.004f, rate_) + v.osc2.saw_bl(f * 0.996f, rate_));
            w = v.filter.lp(raw, 900.0f + 3200.0f * std::exp(-v.t * 7.0f), 0.35f, rate_);
        } else { // warm square with a touch of sub
            const float raw = 0.7f * v.osc.square_bl(f, std::max(lead_duty, 0.3f), rate_) + 0.3f * v.sub.sine(f * 0.5f, rate_);
            w = v.filter.lp(raw, 1400.0f + 2400.0f * std::exp(-v.t * 9.0f), 0.25f, rate_);
        }
        lead = w * env;
        v.t += dt;
        if (v.t > v.len + 0.5f) v.on = false;
    }
    if (bass_v_.on) {
        Tone& v = bass_v_;
        const float env = std::min(1.0f, v.t * 400.0f) * std::exp(-v.t * 2.5f) * (v.t < v.len ? 1.0f : std::exp(-(v.t - v.len) * 30.0f));
        const float raw = 0.6f * v.osc.saw_bl(v.freq, rate_) + 0.7f * v.sub.sine(v.freq, rate_);
        bass = std::tanh(1.4f * v.filter.lp(raw, 220.0f + 1300.0f * std::exp(-v.t * 14.0f), 0.3f, rate_)) * env;
        v.t += dt;
        if (v.t > v.len + 0.2f) v.on = false;
    }
    if (arp_v_.on) { // FM pluck
        Tone& v = arp_v_;
        arp = v.fm.sample(v.freq, 2.0f, 2.2f * std::exp(-v.t * 14.0f), rate_) * std::exp(-v.t * 7.0f);
        v.t += dt;
        if (v.t > 0.8f) v.on = false;
    }
    { // pad: the chord on detuned saws, gliding between chords
        const float glide = 1.0f - std::exp(-dt / 0.08f);
        float raw = 0.0f;
        for (int k = 0; k < 3; ++k) {
            pad_freq_[size_t(k)] += (pad_target_[size_t(k)] - pad_freq_[size_t(k)]) * glide;
            raw += pad_osc_[size_t(2 * k)].saw_bl(pad_freq_[size_t(k)] * 1.005f, rate_) + pad_osc_[size_t(2 * k + 1)].saw_bl(pad_freq_[size_t(k)] * 0.995f, rate_);
        }
        pad_amp_ = std::min(1.0f, pad_amp_ + dt * 0.6f);
        const float swell = 0.85f + 0.15f * pad_lfo_.sine(0.13f, rate_);
        pad = pad_filter_.lp(raw / 6.0f, 750.0f + 350.0f * swell, 0.2f, rate_) * pad_amp_ * swell;
    }
    if (kick_t_ < 0.5f) {
        const float body = kick_osc_.sine(48.0f + 130.0f * std::exp(-kick_t_ * 32.0f), rate_) * std::exp(-kick_t_ * 8.0f);
        const float click = kick_click_.hp(noise_.next(), 2500.0f, 0.0f, rate_) * std::exp(-kick_t_ * 350.0f);
        kick = std::tanh(1.6f * body) + 0.25f * click;
        kick_t_ += dt;
    }
    if (snare_t_ < 0.4f) {
        const float body = (0.6f * snare_osc_.sine(185.0f, rate_) + 0.4f * snare_osc2_.sine(330.0f, rate_)) * std::exp(-snare_t_ * 28.0f);
        const float rattle = snare_bp_.bp(noise_.next(), 2400.0f, 0.3f, rate_) * std::exp(-snare_t_ * 15.0f);
        snare = 0.55f * body + 1.1f * rattle;
        snare_t_ += dt;
    }
    if (hat_t_ < 0.15f) { // six square waves at inharmonic ratios, high-passed: a metallic hat
        static constexpr float kRatios[6] = {2.0f, 3.0f, 4.16f, 5.43f, 6.79f, 8.21f};
        float metal = 0.0f;
        for (int k = 0; k < 6; ++k) metal += hat_osc_[size_t(k)].square_bl(102.0f * kRatios[k], 0.5f, rate_);
        hat = hat_hp_.hp(metal / 6.0f + 0.4f * noise_.next(), 7500.0f, 0.2f, rate_) * std::exp(-hat_t_ * 55.0f);
        hat_t_ += dt;
    }

    // Echo on the lead, ping-ponged; reverb on everything but kick and bass.
    const float echo_l = echo_l_.process(lead, 0.32f), echo_r = echo_r_.process(lead, 0.32f);
    const float arp_pan = (arp_count_ & 1) ? 0.35f : -0.35f;
    float rev_l = 0.0f, rev_r = 0.0f;
    reverb_.process(0.35f * lead + 0.5f * arp + 0.45f * pad + 0.22f * snare + 0.08f * hat, rev_l, rev_r);
    const float centre = 0.25f * bass + 0.36f * kick + 0.22f * snare;
    l = centre + 0.15f * lead + 0.06f * echo_l + 0.10f * arp * (1.0f - arp_pan) + 0.12f * pad + 0.11f * hat * 0.8f + 0.35f * rev_l;
    r = centre + 0.15f * lead + 0.06f * echo_r + 0.10f * arp * (1.0f + arp_pan) + 0.12f * pad + 0.11f * hat * 1.2f + 0.35f * rev_r;
}

} // namespace gk::audio
