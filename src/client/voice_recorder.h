#pragma once
// Records from the default microphone through SDL, for the player's engine
// sound: OPTIONS > RECORD ENGINE. The take is raw; gk_audio's make_loop turns
// it into an engine.
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace gk {

class VoiceRecorder {
public:
    ~VoiceRecorder() { stop(); }

    // Opens the default recording device, mono at `rate`. False (with
    // error()) when there is no microphone or it may not be used.
    bool start(int rate);
    // Takes what the microphone captured since the last call. `keep`: add it
    // to the take (false while counting in, which also warms the device up).
    void update(bool keep);
    void stop();

    bool active() const { return stream_ != nullptr; }
    float level() const { return level_; } // recent peak 0..1, for the meter
    float heard() const { return heard_; } // loudest kept so far: ~0 means a muted or blocked microphone
    const std::vector<float>& take() const { return take_; }
    int rate() const { return rate_; }
    const std::string& error() const { return error_; }

private:
    SDL_AudioStream* stream_ = nullptr;
    std::vector<float> take_, scratch_;
    int rate_ = 48000;
    float level_ = 0.0f, heard_ = 0.0f;
    std::string error_;
};

} // namespace gk
