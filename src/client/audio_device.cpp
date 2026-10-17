#include "audio_device.h"

#include "gk/core/log.h"

#include <SDL3/SDL.h>

namespace gk {

bool AudioDevice::open() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log_warn("No audio: {}", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, mixer_.sample_rate()};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::callback, this);
    if (!stream_) {
        log_warn("No audio device: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    log_info("Audio: {} Hz stereo through the {} driver", mixer_.sample_rate(), SDL_GetCurrentAudioDriver());
    return true;
}

void AudioDevice::close() {
    if (!stream_) return;
    SDL_DestroyAudioStream(stream_); // stops the callback before returning
    stream_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioDevice::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<AudioDevice*>(user);
    const int frames = additional / int(2 * sizeof(float));
    if (frames <= 0) return;
    self->scratch_.resize(size_t(frames) * 2);
    self->mixer_.render(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, self->scratch_.data(), frames * int(2 * sizeof(float)));
}

} // namespace gk
