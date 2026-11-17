// The client's sound device -- docs/specs/UTA-0056-audio-mixer.md SS 4.8.
//
// Opens the default playback device as an SDL audio stream at the mixer's
// format, 48000 Hz stereo float, and fills it from a uaudio::Mixer in SDL's
// stream callback, on SDL's audio thread. If the device does not open, it
// says so in one line and the client runs silent.
//
// The mixer is not thread-safe: every play, stop or settings change goes
// through withMixer, which holds the stream's lock. SDL holds that same lock
// around the callback, so the two never overlap. Keep each call short; a
// long hold underruns the device.
//
// Nothing plays yet: the runtime has no sounds until UTA-0478 bakes them into
// the bundle.

#pragma once

#include "uaudio/Mixer.h"

#include <array>
#include <utility>

struct SDL_AudioStream;

namespace uta::client {

class Audio {
public:
    /// Opens the device. SDL must be initialised; this starts its audio
    /// subsystem.
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    [[nodiscard]] bool open() const noexcept { return stream_ != nullptr; }

    /// Run `change(mixer)` with the callback excluded. With no device it
    /// still runs, on a mixer nothing reads.
    template <class Change>
    void withMixer(Change&& change) {
        lock();
        std::forward<Change>(change)(mixer_);
        unlock();
    }

private:
    static void fill(void* self, SDL_AudioStream* stream, int additional, int total);
    void lock() noexcept;
    void unlock() noexcept;

    uaudio::Mixer mixer_;
    SDL_AudioStream* stream_ = nullptr;
    /// The callback's buffer, so it allocates nothing of its own.
    std::array<float, 1024 * 2> buffer_{};
};

} // namespace uta::client
