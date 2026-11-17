// A stored sound to a playable clip -- docs/specs/UTA-0056-audio-mixer.md
// SS 4.2.
//
// UT keeps its sounds as whole RIFF WAV files inside packages (upkg::Sound
// hands the bytes over undecoded). Stock content is all 8- or 16-bit PCM but
// six sounds; those are refused, naming their format tag, and play as
// silence.

#pragma once

#include "core/Error.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::uaudio {

/// A decoded sound, ready to mix.
struct Clip {
    std::uint32_t rate = 0;     ///< frames a second, 4000 to 96000
    std::uint32_t channels = 0; ///< 1 or 2
    /// Interleaved when stereo, each in [-1, 1].
    std::vector<float> samples;
    /// How many voices of this clip may play at once (SS 4.5 item 2). The
    /// mixer reads it clamped to 1..64.
    int voiceCap = 4;

    [[nodiscard]] std::size_t frames() const noexcept {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

/// RIFF WAV bytes to a Clip. Accepts PCM (format tag 1), 8-bit unsigned or
/// 16-bit signed, mono or stereo, 4000 to 96000 Hz; refuses anything else,
/// and any chunk that runs past the bytes, with an error naming the reason.
[[nodiscard]] Result<Clip> decodeWav(std::span<const std::byte> bytes);

} // namespace uta::uaudio
