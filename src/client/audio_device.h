#pragma once
// Plays the mixer through the default SDL audio device. Without a device (a
// headless machine, --mute) the mixer still exists and calls are harmless.
#include "gk/audio/mixer.h"

struct SDL_AudioStream;

namespace gk {

class AudioDevice {
public:
    bool open(); // false: no sound, but the game runs on
    void close();
    ~AudioDevice() { close(); }

    audio::Mixer& mixer() { return mixer_; }
    bool active() const { return stream_ != nullptr; }

private:
    static void callback(void* self, SDL_AudioStream* stream, int additional, int total);

    audio::Mixer mixer_{48000};
    SDL_AudioStream* stream_ = nullptr;
    std::vector<float> scratch_;
};

} // namespace gk
