// Decoded sounds by path -- docs/specs/UTA-0478-baked-sounds.md SS 4.5.
//
// The client fills one from a map's bundle and the weapon set's, and a
// player of sounds looks each up by the path a bundle's actor property
// carries. It takes spans, not a Bundle: uaudio links core alone
// (UTA-0056 INV-11).

#pragma once

#include "core/Error.h"
#include "uaudio/Wav.h"

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace uta::uaudio {

class SoundBank {
public:
    /// Decodes `wav` and keeps the clip under `path`. A path already held
    /// keeps its first clip. A payload decodeWav refuses is not kept, and
    /// its error is returned.
    Result<void> add(std::string path, std::span<const std::byte> wav);

    /// Null where `path` is not held. The pointer stays good until the bank
    /// is destroyed, whatever is added after: a mixer voice holds one.
    [[nodiscard]] const Clip* find(std::string_view path) const;

    [[nodiscard]] std::size_t size() const noexcept { return clips_.size(); }

private:
    /// A map and not a vector: its nodes never move, which is what keeps
    /// find's pointers good.
    std::map<std::string, Clip, std::less<>> clips_;
};

} // namespace uta::uaudio
