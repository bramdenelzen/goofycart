// Sound effects built from oscillators, noise and envelopes. Each one is a
// short recipe; they are rendered once when the mixer starts.
#include "gk/audio/mixer.h"
#include "synth.h"

#include <algorithm>
#include <cmath>

namespace gk::audio {
namespace {

// Renders `seconds` of f(t) and normalises the peak to `peak`.
template <class F>
std::vector<float> render(int rate, float seconds, float peak, F&& f) {
    std::vector<float> out(size_t(seconds * float(rate)));
    for (size_t i = 0; i < out.size(); ++i) out[i] = f(float(i) / float(rate));
    float m = 0.0f;
    for (float v : out) m = std::max(m, std::abs(v));
    if (m > 0.0f)
        for (float& v : out) v *= peak / m;
    // 3 ms fade at both ends against clicks.
    const size_t fade = std::min(out.size() / 2, size_t(rate * 0.003f));
    for (size_t i = 0; i < fade; ++i) {
        const float g = float(i) / float(fade);
        out[i] *= g;
        out[out.size() - 1 - i] *= g;
    }
    return out;
}

// A sequence of chimes: (frequency, length in seconds) pairs, each an FM
// bell that rings on under the next, with a short room tail. `duty` picks
// the timbre (brighter for larger values).
std::vector<float> notes(int rate, std::initializer_list<std::pair<float, float>> seq, float duty, float decay,
                         float peak) {
    float total = 0.0f;
    for (auto& n : seq) total += n.second;
    std::vector<std::pair<float, float>> v(seq);
    std::vector<Fm> fm(v.size());
    std::vector<Osc> body(v.size());
    Reverb room(rate, 0.6f);
    room.feedback = 0.7f;
    const float ring = std::min(decay, 9.0f);
    return render(rate, total + 0.45f, peak, [&, rate](float t) {
        float start = 0.0f, dry = 0.0f;
        for (size_t k = 0; k < v.size(); ++k) {
            const auto [f, len] = v[k];
            if (t >= start) {
                const float lt = t - start;
                const float env = std::min(1.0f, lt * 600.0f) * std::exp(-lt * ring);
                const float index = (0.8f + 2.5f * duty) * std::exp(-lt * 12.0f);
                dry += (fm[k].sample(f, 2.0f, index, rate) + 0.3f * body[k].sine(f * 0.5f, rate)) * env;
            }
            start += len;
        }
        float rl = 0.0f, rr = 0.0f;
        room.process(dry, rl, rr);
        return dry + 0.5f * (rl + rr);
    });
}

} // namespace

std::vector<float> make_sfx(Sfx s, int rate) {
    Noise noise{0x1234u + uint32_t(s) * 7919u};
    Osc a, b, c;
    OnePole lp, hp;
    const auto sr = rate;
    switch (s) {
    case Sfx::MenuMove: return notes(rate, {{1046.5f, 0.045f}}, 0.25f, 40.0f, 0.35f);
    case Sfx::MenuAccept: return notes(rate, {{784.0f, 0.06f}, {1174.7f, 0.12f}}, 0.25f, 14.0f, 0.4f);
    case Sfx::MenuBack: return notes(rate, {{698.5f, 0.06f}, {466.2f, 0.1f}}, 0.25f, 16.0f, 0.35f);
    case Sfx::CountBeep:
        return render(rate, 0.3f, 0.5f, [&](float t) {
            return (a.sine(554.4f, sr) + 0.25f * b.sine(1108.7f, sr) + 0.1f * c.sine(1663.2f, sr)) * std::min(1.0f, t * 300.0f) * (t < 0.22f ? 1.0f : std::exp(-(t - 0.22f) * 40));
        });
    case Sfx::CountGo:
        return render(rate, 0.8f, 0.55f, [&](float t) {
            return (a.sine(1108.7f, sr) + 0.25f * b.sine(2217.5f, sr) + 0.1f * c.sine(3326.0f, sr)) * std::min(1.0f, t * 300.0f) * (t < 0.55f ? 1.0f : std::exp(-(t - 0.55f) * 20));
        });
    case Sfx::Boost:
        return render(rate, 0.7f, 0.6f, [&](float t) {
            const float f = 180.0f + 520.0f * std::min(1.0f, t / 0.25f);
            const float whoosh = lp.low(noise.next(), 0.05f + 0.25f * std::min(1.0f, t * 4.0f));
            return (0.5f * a.saw_bl(f, sr) + 1.4f * whoosh) * std::exp(-t * 4.0f);
        });
    case Sfx::MiniTurbo:
        return render(rate, 0.45f, 0.5f, [&](float t) {
            const float f = 300.0f + 900.0f * std::min(1.0f, t / 0.12f);
            return (0.6f * a.square_bl(f, 0.3f, sr) + 0.8f * lp.low(noise.next(), 0.3f)) * std::exp(-t * 7.0f);
        });
    case Sfx::RocketStart:
        return render(rate, 0.9f, 0.6f, [&](float t) {
            const float f = 120.0f + 700.0f * std::min(1.0f, t / 0.3f);
            return (0.6f * a.saw_bl(f, sr) + 0.4f * b.saw_bl(f * 1.01f, sr) + 0.8f * lp.low(noise.next(), 0.2f)) *
                   std::exp(-t * 3.0f);
        });
    case Sfx::Trick:
        return render(rate, 0.16f, 0.4f, [&](float t) {
            return a.square_bl(600.0f + 9000.0f * t, 0.25f, sr) * std::exp(-t * 18.0f);
        });
    case Sfx::Land:
        return render(rate, 0.22f, 0.6f, [&](float t) {
            return (a.sine(95.0f - 160.0f * t, sr) + 0.5f * lp.low(noise.next(), 0.08f)) * std::exp(-t * 22.0f);
        });
    case Sfx::WallHit:
        return render(rate, 0.25f, 0.65f, [&](float t) {
            return (0.7f * lp.low(noise.next(), 0.25f) + 0.5f * a.square_bl(110.0f, 0.5f, sr)) * std::exp(-t * 20.0f);
        });
    case Sfx::Bump:
        return render(rate, 0.18f, 0.5f, [&](float t) {
            return (a.sine(160.0f - 300.0f * t, sr) + 0.4f * lp.low(noise.next(), 0.15f)) * std::exp(-t * 25.0f);
        });
    case Sfx::Respawn:
        return render(rate, 0.7f, 0.4f, [&](float t) {
            const float f = 400.0f * std::pow(2.0f, t * 2.6f);
            return (a.sine(f, sr) + 0.4f * b.sine(f * 1.5f, sr)) * std::exp(-t * 3.0f) * std::min(1.0f, t * 50.0f);
        });
    case Sfx::GliderOpen:
        return render(rate, 0.5f, 0.45f, [&](float t) {
            return lp.low(noise.next(), 0.04f + 0.1f * t) * std::sin(3.14159f * std::min(1.0f, t / 0.5f));
        });
    case Sfx::ItemBox:
        return render(rate, 0.5f, 0.45f, [&](float t) {
            return (a.sine(1318.5f, sr) + 0.7f * b.sine(1975.5f, sr) + 0.4f * c.sine(2637.0f, sr)) *
                   std::exp(-t * 8.0f) + 0.3f * hp.high(noise.next(), 0.3f) * std::exp(-t * 60.0f);
        });
    case Sfx::RouletteTick: return notes(rate, {{1760.0f, 0.02f}}, 0.5f, 120.0f, 0.18f);
    case Sfx::ItemGet: return notes(rate, {{1396.9f, 0.07f}, {2093.0f, 0.2f}}, 0.5f, 10.0f, 0.4f);
    case Sfx::Throw:
        return render(rate, 0.25f, 0.45f, [&](float t) {
            return lp.low(noise.next(), 0.35f - t) * std::sin(3.14159f * t / 0.25f);
        });
    case Sfx::Drop:
        return render(rate, 0.15f, 0.4f, [&](float t) { return a.sine(300.0f - 900.0f * t, sr) * std::exp(-t * 25.0f); });
    case Sfx::Hit:
        return render(rate, 0.7f, 0.65f, [&](float t) {
            return (0.8f * lp.low(noise.next(), 0.3f) * std::exp(-t * 14.0f) +
                    0.5f * a.square_bl(700.0f * std::exp(-t * 3.0f), 0.5f, sr) * std::exp(-t * 3.0f));
        });
    case Sfx::Shrink:
        return render(rate, 0.8f, 0.45f, [&](float t) {
            return a.square_bl(900.0f * std::exp(-t * 2.0f) * (1.0f + 0.1f * std::sin(t * 70.0f)), 0.5f, sr) *
                   std::exp(-t * 2.5f);
        });
    case Sfx::Explosion:
        return render(rate, 1.2f, 0.8f, [&](float t) {
            return lp.low(noise.next(), 0.06f + 0.2f * std::exp(-t * 6.0f)) * std::exp(-t * 3.5f) +
                   0.6f * a.sine(55.0f - 20.0f * t, sr) * std::exp(-t * 4.0f);
        });
    case Sfx::Blocked:
        return render(rate, 0.35f, 0.5f, [&](float t) {
            return (a.sine(420.0f, sr) + 0.8f * b.sine(1130.0f, sr) + 0.5f * c.sine(2210.0f, sr)) * std::exp(-t * 14.0f);
        });
    case Sfx::Lightning:
        return render(rate, 1.0f, 0.75f, [&](float t) {
            const float crackle = (noise.next() > 0.6f ? 1.0f : 0.0f) * noise.next();
            return 0.7f * crackle * std::exp(-t * 2.5f) + 0.5f * lp.low(noise.next(), 0.05f) * std::exp(-t * 2.0f) +
                   0.4f * a.saw_bl(1500.0f * std::exp(-t * 4.0f), sr) * std::exp(-t * 6.0f);
        });
    case Sfx::Horn:
        return render(rate, 0.6f, 0.7f, [&](float t) {
            const float env = std::min(1.0f, t * 40.0f) * (t < 0.45f ? 1.0f : std::exp(-(t - 0.45f) * 25.0f));
            return (a.saw_bl(311.1f, sr) + b.saw_bl(370.0f, sr) + 0.5f * c.square_bl(155.6f, 0.5f, sr)) * env;
        });
    case Sfx::Coin: return notes(rate, {{1174.7f, 0.06f}, {1760.0f, 0.22f}}, 0.5f, 9.0f, 0.4f);
    case Sfx::Ink:
        return render(rate, 0.4f, 0.5f, [&](float t) {
            return lp.low(noise.next(), 0.1f) * std::exp(-t * 9.0f) + 0.4f * a.sine(140.0f - 200.0f * t, sr) * std::exp(-t * 10.0f);
        });
    case Sfx::Star:
        return render(rate, 0.9f, 0.4f, [&](float t) {
            static constexpr float k[] = {1046.5f, 1318.5f, 1568.0f, 2093.0f, 1568.0f, 2093.0f, 2637.0f, 2093.0f};
            const float lt = std::fmod(t, 1.0f / 12.0f);
            return a.sine(k[int(t * 12.0f) % 8], sr) * (0.6f + 0.4f * std::exp(-lt * 30.0f)) * std::exp(-t * 1.5f) +
                   0.2f * b.sine(k[int(t * 12.0f) % 8] * 2.0f, sr) * std::exp(-t * 3.0f);
        });
    case Sfx::Ghost:
        return render(rate, 0.8f, 0.4f, [&](float t) {
            return a.sine(500.0f + 250.0f * std::sin(t * 18.0f) - 300.0f * t, sr) * std::sin(3.14159f * t / 0.8f);
        });
    case Sfx::Lap: return notes(rate, {{880.0f, 0.1f}, {1108.7f, 0.1f}, {1318.5f, 0.25f}}, 0.25f, 5.0f, 0.4f);
    case Sfx::FinalLap:
        return notes(rate, {{659.3f, 0.09f}, {830.6f, 0.09f}, {987.8f, 0.09f}, {1318.5f, 0.09f}, {1661.2f, 0.4f}}, 0.25f, 3.0f,
                     0.45f);
    case Sfx::Finish:
        return notes(rate,
                     {{523.3f, 0.12f}, {659.3f, 0.12f}, {784.0f, 0.12f}, {1046.5f, 0.24f}, {880.0f, 0.12f}, {1046.5f, 0.7f}},
                     0.5f, 2.0f, 0.45f);
    case Sfx::Count: break;
    }
    return {};
}

} // namespace gk::audio
