// Decoded sounds by path -- docs/specs/UTA-0478-baked-sounds.md SS 4.5.

#include "uaudio/SoundBank.h"

#include <utility>

namespace uta::uaudio {

Result<void> SoundBank::add(std::string path, std::span<const std::byte> wav) {
    if (clips_.contains(path)) return {};
    UTA_TRY(Clip clip, decodeWav(wav));
    clips_.emplace(std::move(path), std::move(clip));
    return {};
}

const Clip* SoundBank::find(std::string_view path) const {
    const auto found = clips_.find(path);
    return found == clips_.end() ? nullptr : &found->second;
}

} // namespace uta::uaudio
