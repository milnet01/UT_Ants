// Sounds.
//
// docs/specs/UTA-0004-typed-level-content.md SS 4.8.
//
// LIFETIME: `data` is a VIEW of the caller's package bytes and copies nothing
// (SS 3.3 item 3, INV-7). The caller must keep those bytes alive for as long
// as it reads it.
//
// The payload is a complete RIFF WAV in stock content and is returned exactly
// as it sits in the file. Decoding audio is `uaudio`'s (docs/design.md
// SS The parts); `formatName` is carried through so a caller can tell what it
// holds rather than guessing from the bytes.

#ifndef UTA_UPKG_SOUND_H
#define UTA_UPKG_SOUND_H

#include "core/Error.h"
#include "upkg/Package.h"

#include <cstdint>
#include <span>

namespace uta::upkg {

struct Sound {
    /// Name-table index of the format -- "WAV" in stock content.
    std::uint32_t formatName = 0;
    std::span<const std::byte> data;
};

/// A `Sound` export: its format name and its payload.
[[nodiscard]] Result<Sound> readSound(const Package& package, const ExportEntry& entry);

} // namespace uta::upkg

#endif // UTA_UPKG_SOUND_H
