#include <utility>
#include "gk/audio/mixer.h"
#include "music.h"
#include "synth.h"

#include <algorithm>
#include <cmath>

namespace gk::audio {

// The local kart's engine: two pulse oscillators an octave apart through a
// low-pass, plus tyre squeal while drifting, spark shimmer per mini-turbo
// tier and off-road rumble. Parameters glide so changes never click.
struct Mixer::EngineSynth {
    Osc a, b, c, shimmer, trem;
    Svf body;
    Noise noise{0xabcdefu};
    OnePole tone, squeal_lp, squeal_hp, rumble;
    float level = 0.0f, speed = 0.0f, throttle = 0.0f, drift = 0.0f, rough = 0.0f, spark = 0.0f, air = 0.0f;
    float spark_freq = 1800.0f;

    void sample(const EngineState& e, int rate, float& out) {
        const float k = 1.0f / (0.04f * float(rate)); // ~40 ms glide
        auto glide = [k](float& v, float target) { v += (target - v) * k; };
        glide(level, e.active ? 1.0f : 0.0f);
        glide(speed, e.speed + (e.boosting ? 0.15f : 0.0f));
        glide(throttle, e.throttle);
        glide(drift, e.drift_tier >= 0 && !e.airborne ? 1.0f : 0.0f);
        glide(spark, e.drift_tier >= 1 && !e.airborne ? 1.0f : 0.0f);
        glide(rough, e.airborne ? 0.0f : e.rough);
        glide(air, e.airborne ? 1.0f : 0.0f);
        if (e.drift_tier >= 1) spark_freq = e.drift_tier == 1 ? 1700.0f : e.drift_tier == 2 ? 2300.0f : 3000.0f;
        if (level < 1e-4f) {
            out = 0.0f;
            return;
        }
        const float f = 48.0f + 105.0f * speed + 22.0f * throttle + 18.0f * air;
        // A band-limited saw for the cylinders over a sine an octave down,
        // filtered warmer at low throttle.
        const float raw = 0.55f * a.saw_bl(f, rate) + 0.6f * b.sine(f * 0.5f, rate) + 0.15f * c.square_bl(f * 2.0f, 0.3f, rate);
        const float engine = body.lp(raw, 300.0f + 900.0f * throttle + 600.0f * speed, 0.3f, rate) * (0.35f + 0.65f * throttle);
        const float squeal = squeal_hp.high(squeal_lp.low(noise.next(), 0.6f), 0.15f) * drift * std::min(1.0f, speed * 2.0f);
        const float sparks = shimmer.sine(spark_freq, rate) * (0.6f + 0.4f * trem.sine(13.0f, rate)) * spark;
        const float grit = rumble.low(noise.next(), 0.03f) * rough * std::min(1.0f, speed * 1.5f);
        out = level * (0.55f * engine + 0.12f * squeal + 0.05f * sparks + 0.9f * grit);
    }
};

Mixer::Mixer(int sample_rate) : rate_(sample_rate), engine_(new EngineSynth) {
    for (int i = 0; i < kSfxCount; ++i) sfx_[size_t(i)] = make_sfx(Sfx(i), rate_);
}

Mixer::~Mixer() {
    delete song_;
    delete held_;
    delete engine_;
}

void Mixer::play(Sfx s, float gain, float pan, float pitch) {
    if (s == Sfx::Count || gain <= 0.0f) return;
    std::lock_guard lock(mutex_);
    // A free voice, else the one closest to finishing.
    Voice* best = &voices_[0];
    float best_left = 1e30f;
    for (Voice& v : voices_) {
        if (!v.buf) {
            best = &v;
            break;
        }
        const float left = float(double(v.buf->size()) - v.pos) / v.step;
        if (left < best_left) best_left = left, best = &v;
    }
    pan = std::clamp(pan, -1.0f, 1.0f);
    best->buf = &sfx_[size_t(s)];
    best->pos = 0.0;
    best->step = std::max(0.1f, pitch);
    best->gain_l = gain * std::sqrt(0.5f * (1.0f - pan));
    best->gain_r = gain * std::sqrt(0.5f * (1.0f + pan));
}

void Mixer::set_engine(const EngineState& e) {
    std::lock_guard lock(mutex_);
    engine_target_ = e;
}

void Mixer::play_music(uint32_t seed, Mood mood) {
    Song* fresh = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (song_ && seed == song_seed_ && mood == song_mood_) return;
        // Back from an interruption (the star): pick the song up where it was.
        if (held_ && seed == held_seed_ && mood == held_mood_) {
            std::swap(song_, held_);
            std::swap(song_seed_, held_seed_);
            std::swap(song_mood_, held_mood_);
            music_fade_ = 0.0f;
            fresh = held_; // the interruption, deleted below
            held_ = nullptr;
        }
    }
    if (fresh) {
        delete fresh;
        return;
    }
    fresh = new Song(seed, mood, rate_); // outside the lock: the audio thread keeps running
    Song* old_song = nullptr;
    Song* old_held = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (mood == Mood::Star && song_ && song_mood_ != Mood::Star) {
            old_held = held_;
            held_ = song_; // paused, not deleted
            held_seed_ = song_seed_;
            held_mood_ = song_mood_;
        } else {
            old_song = song_;
            if (mood != Mood::Star) old_held = std::exchange(held_, nullptr); // a new race: forget it
        }
        song_ = fresh;
        song_seed_ = seed;
        song_mood_ = mood;
        if (mood != Mood::Star) music_speed_ = 1.0f;
        music_fade_ = mood == Mood::Star ? 0.8f : 0.0f;
    }
    delete old_song;
    delete old_held;
}

void Mixer::stop_music() {
    Song* old = nullptr;
    {
        std::lock_guard lock(mutex_);
        old = song_;
        song_ = nullptr;
        delete held_;
        held_ = nullptr;
    }
    delete old;
}

void Mixer::set_music_speed(float speed) {
    std::lock_guard lock(mutex_);
    music_speed_ = speed;
}

void Mixer::set_volumes(const Volumes& v) {
    std::lock_guard lock(mutex_);
    vol_ = v;
}

Volumes Mixer::volumes() const {
    std::lock_guard lock(mutex_);
    return vol_;
}

void Mixer::render(float* out, int frames) {
    std::lock_guard lock(mutex_);
    const float fade_step = 1.0f / (0.5f * float(rate_));
    for (int i = 0; i < frames; ++i) {
        float l = 0.0f, r = 0.0f;
        for (Voice& v : voices_) {
            if (!v.buf) continue;
            const size_t i0 = size_t(v.pos);
            if (i0 + 1 >= v.buf->size()) {
                v.buf = nullptr;
                continue;
            }
            const float frac = float(v.pos - double(i0));
            const float s = (*v.buf)[i0] * (1.0f - frac) + (*v.buf)[i0 + 1] * frac;
            l += s * v.gain_l;
            r += s * v.gain_r;
            v.pos += double(v.step);
        }
        float eng = 0.0f;
        engine_->sample(engine_target_, rate_, eng);
        l = (l + eng) * vol_.sfx;
        r = (r + eng) * vol_.sfx;
        if (song_) {
            float ml = 0.0f, mr = 0.0f;
            song_->sample(music_speed_, ml, mr);
            music_fade_ = std::min(1.0f, music_fade_ + fade_step);
            l += ml * vol_.music * music_fade_;
            r += mr * vol_.music * music_fade_;
        }
        // Gentle limiter so a pile-up of effects saturates instead of wrapping.
        out[2 * i] = std::tanh(l * vol_.master);
        out[2 * i + 1] = std::tanh(r * vol_.master);
    }
}

} // namespace gk::audio
