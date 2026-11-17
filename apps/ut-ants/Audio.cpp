#include "Audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <span>

namespace uta::client {

Audio::Audio() {
    // SS 4.8: a headless run with no sound server is the common failure, so
    // it is one line and the game carries on.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "ut-ants: sound is off: " << SDL_GetError() << "\n";
        return;
    }
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 2, .freq = static_cast<int>(uaudio::Mixer::RATE)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::fill, this);
    if (stream_ == nullptr || !SDL_ResumeAudioStreamDevice(stream_)) {
        std::cerr << "ut-ants: sound is off: " << SDL_GetError() << "\n";
        if (stream_ != nullptr) SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

Audio::~Audio() {
    if (stream_ == nullptr) return;
    SDL_DestroyAudioStream(stream_); // stops the callback before it returns
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void Audio::lock() noexcept {
    if (stream_ != nullptr) SDL_LockAudioStream(stream_);
}

void Audio::unlock() noexcept {
    if (stream_ != nullptr) SDL_UnlockAudioStream(stream_);
}

// SDL's audio thread, with the stream's lock held. `additional` is the bytes
// SDL wants now.
void Audio::fill(void* self, SDL_AudioStream* stream, int additional, int /*total*/) {
    Audio& audio = *static_cast<Audio*>(self);
    constexpr std::size_t FRAME_BYTES = 2 * sizeof(float);
    auto frames = static_cast<std::size_t>(std::max(additional, 0)) / FRAME_BYTES;
    while (frames > 0) {
        const std::size_t now = std::min(frames, audio.buffer_.size() / 2);
        const std::span<float> chunk(audio.buffer_.data(), now * 2);
        audio.mixer_.mix(chunk);
        SDL_PutAudioStreamData(stream, chunk.data(), static_cast<int>(chunk.size_bytes()));
        frames -= now;
    }
}

} // namespace uta::client
