#pragma once
// Engine sounds made from a voice: a microphone take turned into a seamless
// loop, a sung buzz of its own for every bot, the wire format that carries a
// player's recording to everyone else in an online race, and the Doppler
// shift. Plain C++ with no device, like the mixer, so all of it is tested.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gk::audio {

// A mono loop the mixer plays as a kart's engine, faster the faster it goes.
struct EngineClip {
    std::vector<float> samples;
    int rate = 48000;
    float seconds() const { return rate > 0 ? float(samples.size()) / float(rate) : 0.0f; }
};

inline constexpr float kMinSoundSeconds = 0.15f; // shorter than this is a click or a cough, not an engine
inline constexpr float kMaxLoopSeconds = 2.0f;   // the longest loop kept, and sent

// Turns a raw take into an engine loop: whatever was said, a held "ahhhh",
// one word or a few, from its first sound to its last with any pauses in
// between, looped round. Rumble and DC are filtered out, the silence around
// it trimmed, its end crossfaded into its start so the loop has no click,
// and the loudness evened out. Past kMaxLoopSeconds the rest is cut.
// Empty when nothing usable was heard: too quiet, or too short.
EngineClip make_loop(std::span<const float> take, int rate);

// A bot's own engine: a buzzing voice singing one vowel at its own pitch,
// with its own wobble and grit, all picked from `seed`, so every seat sounds
// different from every other.
EngineClip make_bot_clip(uint32_t seed, int rate);

// The wire format: 8-bit mu-law at kWireRate, at most kMaxWireBytes.
inline constexpr int kWireRate = 16000;
inline constexpr size_t kMaxWireBytes = size_t(kWireRate) * 2; // 2 s: the longest loop
std::vector<uint8_t> encode_clip(const EngineClip& c);
EngineClip decode_clip(std::span<const uint8_t> wire);

// Mono 16-bit WAV, for keeping the player's engine between sessions. Reading
// also takes stereo, 24-bit and 32-bit float files (mixed down to mono), so
// any sound can be imported; it gives up on anything over a minute.
bool save_wav(const std::string& path, const EngineClip& c);
std::optional<EngineClip> load_wav(const std::string& path);

// The pitch ratio heard when the source moves towards the listener at
// `source_toward` m/s and the listener towards the source at
// `listener_toward` m/s (negative: apart), with sound at `speed_of_sound`.
float doppler_factor(float source_toward, float listener_toward, float speed_of_sound);

} // namespace gk::audio
