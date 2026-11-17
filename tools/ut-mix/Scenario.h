// The flak-announcer scenario -- docs/specs/UTA-0056-audio-mixer.md SS 4.7.
//
// The flak cannon's primary fired for 4 s, each shot's eight chunk impacts
// following it, and the announcer's "five minutes" line at 1 s, all mixed
// from the install's own sounds. ut-mix renders it to a file; RealMixTest
// holds its three figures to SS 4.7's targets.

#pragma once

#include "core/Error.h"
#include "uaudio/Mixer.h"

#include <filesystem>
#include <vector>

namespace uta::tools::mix {

/// SS 4.7's three figures, and what the mix cost.
struct Figures {
    /// Speech's RMS over the other three groups' summed RMS, after the
    /// attack, in dB. Target: at least 6.
    double speechMarginDb = 0;
    /// The limiter's deepest gain reduction, in dB. Target: at most 6.
    double limiterReductionDb = 0;
    /// The most chunk-impact voices playing at once. Target: at most 4.
    int mostImpactVoices = 0;
    /// Wall time spent in Mixer::mix, and the sound it made.
    double mixSeconds = 0;
    double soundSeconds = 0;
};

struct Rendered {
    std::vector<float> frames; ///< interleaved stereo at Mixer::RATE
    Figures figures;
};

/// Render the scenario from `install`'s Sounds/ under `settings`. An error
/// names the package and the sound it could not read.
[[nodiscard]] Result<Rendered> renderFlakAnnouncer(const std::filesystem::path& install,
                                                   const uaudio::Settings& settings);

} // namespace uta::tools::mix
