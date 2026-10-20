#include "voice_recorder.h"

#include "gk/core/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace gk {

namespace {
constexpr float kMaxTakeSeconds = 10.0f; // never keep more than this, whatever happens to the menu
}

bool VoiceRecorder::start(int rate) {
    stop();
    error_.clear();
    take_.clear();
    level_ = heard_ = 0.0f;
    rate_ = rate;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error_ = SDL_GetError();
        return false;
    }
    // Mono floats at our rate; SDL converts from whatever the microphone gives.
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, rate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
    if (!stream_) {
        error_ = SDL_GetError();
        log_warn("No microphone: {}", error_);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_); // opened paused
    log_info("Recording an engine from the default microphone");
    return true;
}

void VoiceRecorder::update(bool keep) {
    if (!stream_) return;
    level_ *= 0.85f; // the meter falls back between frames
    for (;;) {
        const int bytes = SDL_GetAudioStreamAvailable(stream_);
        if (bytes < int(sizeof(float))) break;
        scratch_.resize(size_t(bytes) / sizeof(float));
        const int got = SDL_GetAudioStreamData(stream_, scratch_.data(), int(scratch_.size() * sizeof(float)));
        if (got <= 0) break;
        const size_t n = size_t(got) / sizeof(float);
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(scratch_[i]));
        level_ = std::max(level_, std::min(1.0f, peak));
        if (!keep) continue;
        heard_ = std::max(heard_, peak);
        const size_t room = size_t(kMaxTakeSeconds * float(rate_)) - std::min(take_.size(), size_t(kMaxTakeSeconds * float(rate_)));
        take_.insert(take_.end(), scratch_.begin(), scratch_.begin() + std::ptrdiff_t(std::min(n, room)));
    }
}

void VoiceRecorder::stop() {
    if (!stream_) return;
    SDL_DestroyAudioStream(stream_); // closes the device it opened
    stream_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

} // namespace gk
