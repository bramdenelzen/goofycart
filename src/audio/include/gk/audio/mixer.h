#pragma once
// Everything the game hears, synthesised at start-up: no sample files. The
// mixer is plain C++ so it can be tested without an audio device; the client
// feeds its output to SDL from the audio thread.
#include "gk/audio/voice.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace gk::audio {

enum class Sfx : uint8_t {
    MenuMove, MenuAccept, MenuBack,
    CountBeep, CountGo,
    Boost, MiniTurbo, RocketStart, Trick, Land, WallHit, Bump, Respawn, GliderOpen,
    ItemBox, RouletteTick, ItemGet, Throw, Drop, Hit, Shrink, Explosion, Blocked,
    Lightning, Horn, Coin, Ink, Star, Ghost,
    Lap, FinalLap, Finish,
    Crunch, Boing, Pause,
    Count
};
inline constexpr int kSfxCount = int(Sfx::Count);

// The local kart's engine, updated every frame from the simulation.
struct EngineState {
    bool active = false;
    float speed = 0.0f;     // 0 stopped .. 1 top speed (above 1 while boosting)
    float throttle = 0.0f;  // 0 .. 1
    int drift_tier = -1;    // -1 not drifting, 0 no spark yet, 1 blue, 2 orange, 3 purple
    float rough = 0.0f;     // 0 tarmac .. 1 heavy off-road
    bool airborne = false;
    bool boosting = false;
    // The engine's tone comes from the player's recording (a KartEngine); the
    // synth then adds only the tyres, sparks and off-road grit.
    bool voiced = false;
};

// One kart's engine as a looped clip, placed by the client every frame: the
// local kart's when the player recorded one, and every other kart's.
struct KartEngine {
    std::shared_ptr<const EngineClip> clip; // none: silent
    float speed = 0.0f;    // 0 stopped .. 1 top speed (above 1 while boosting)
    float throttle = 0.0f; // 0 .. 1
    float doppler = 1.0f;  // pitch ratio from how the kart and the listener move
    float gain = 0.0f;     // after distance
    float pan = 0.0f;      // -1 left .. 1 right
    float muffle = 0.0f;   // 0 clear .. 1 far off and dull
};
inline constexpr int kKartEngines = 12;
// How fast a kart engine plays its clip, before Doppler: the voice's own
// pitch flat out on the gas, lower as the kart slows (0.72 standing on the
// gas), a touch higher on a boost (its speed reads above 1).
inline float engine_revs(float speed, float throttle) { return 0.67f + 0.28f * std::max(0.0f, speed) + 0.05f * throttle; }
// The local kart's recorded engine at this gain plays about as loud as the
// synth's tone it replaces (tests/test_voice.cpp holds it to that).
inline constexpr float kLocalEngineGain = 1.2f;

// Mood of a procedurally written song: picked from a track theme.
enum class Mood : uint8_t { Bright, Dusty, Breezy, Frosty, Driving, Fiery, Airy, Night, Menu, Results, Star };
Mood mood_for_theme(const std::string& theme);

struct Volumes {
    float master = 0.8f, music = 0.6f, sfx = 0.9f;
};

class Mixer {
public:
    explicit Mixer(int sample_rate = 48000);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    int sample_rate() const { return rate_; }

    // Thread-safe controls (main thread).
    void play(Sfx s, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f);
    void set_engine(const EngineState& e);
    // Up to kKartEngines; the slots not given fall silent. A slot keeps its
    // place in its clip while the clip stays the same, so call every frame.
    void set_kart_engines(std::span<const KartEngine> engines);
    // Starts a new song unless the same (seed, mood) is already playing.
    void play_music(uint32_t seed, Mood mood);
    void stop_music();
    void set_music_speed(float speed); // 1 normal, ~1.1 on the final lap
    void set_volumes(const Volumes& v);
    Volumes volumes() const;

    // Audio thread: interleaved stereo float, `frames` frames.
    void render(float* out, int frames);

    // For tests: the pre-rendered effect.
    const std::vector<float>& sfx_buffer(Sfx s) const { return sfx_[size_t(s)]; }

private:
    struct Voice {
        const std::vector<float>* buf = nullptr;
        double pos = 0.0;
        float step = 1.0f, gain_l = 0.0f, gain_r = 0.0f;
    };
    struct Song;
    struct EngineSynth;
    // A kart engine playing: the target from the client, and where the
    // playback is and how it is gliding towards that target.
    struct EngineVoice {
        KartEngine target;
        double pos = 0.0;
        float step = 0.0f, gain_l = 0.0f, gain_r = 0.0f, muffle = 0.0f, lp = 0.0f;
    };

    int rate_;
    std::array<std::vector<float>, kSfxCount> sfx_;
    mutable std::mutex mutex_;
    std::array<Voice, 32> voices_{};
    Volumes vol_;
    EngineState engine_target_;
    EngineSynth* engine_;
    std::array<EngineVoice, kKartEngines> kart_engines_{};
    Song* song_ = nullptr;
    Song* held_ = nullptr; // paused under the star jingle
    uint32_t held_seed_ = 0;
    Mood held_mood_ = Mood::Menu;
    uint32_t song_seed_ = 0;
    Mood song_mood_ = Mood::Menu;
    float music_speed_ = 1.0f;
    float music_fade_ = 0.0f; // fades a new song in
};

// The effects, rendered to mono buffers at `rate`. Exposed for tests.
std::vector<float> make_sfx(Sfx s, int rate);

} // namespace gk::audio
