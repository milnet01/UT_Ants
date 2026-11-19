// The SNDS section: the sounds a map's actors name, or the weapon set's --
// docs/specs/UTA-0478-baked-sounds.md SS 4.1.
//
// One vector of path and payload. The payload is the Sound export's bytes as
// the package holds them; nothing here decodes one. Every rule is the
// validator's, on both paths.

#include "Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// A string's u32 length and a payload's u32 count, with no bytes behind
/// either: the smallest a sound encodes to, and readVector's divisor.
constexpr std::uint64_t SOUND_ENTRY = 8;

[[nodiscard]] Result<BakedSound> readSound(Cursor& cursor) {
    BakedSound sound;
    UTA_TRY(sound.path, readString(cursor));
    UTA_TRY(const std::uint32_t count, cursor.readU32());
    // readBytes bounds the count by the section, so it sizes nothing unchecked.
    UTA_TRY(const std::span<const std::byte> raw, cursor.readBytes(count));
    sound.data.assign(raw.begin(), raw.end());
    return sound;
}

void putSound(Sink& sink, const BakedSound& sound) {
    sink.putString(sound.path);
    sink.putCount(sound.data.size());
    sink.append(sound.data);
}

} // namespace

Result<std::vector<BakedSound>> readSounds(Cursor& cursor) {
    return readVector<BakedSound>(cursor, SOUND_ENTRY, "sounds", readSound);
}

Result<std::vector<std::byte>> encodeSounds(const std::vector<BakedSound>& sounds) {
    Sink sink;
    sink.putVector(sounds, putSound);
    return std::move(sink).finish("SNDS");
}

Result<void> validateSounds(const std::vector<BakedSound>& sounds, ErrorCode code) {
    if (sounds.size() > SOUND_LIMIT)
        return fail(code, "SNDS holds " + std::to_string(sounds.size()) + " sounds, past the limit of "
                              + std::to_string(SOUND_LIMIT));
    for (std::size_t i = 0; i < sounds.size(); ++i) {
        const BakedSound& sound = sounds[i];
        const std::string where = "SNDS: sound " + std::to_string(i);
        if (sound.path.empty()) return fail(code, where + " has no path");
        if (sound.data.empty()) return fail(code, where + " has no bytes");
        // Strictly ascending: no path twice, and one set of sounds always
        // encodes to the same bytes.
        if (i > 0 && !(sounds[i - 1].path < sound.path))
            return fail(code, where + "'s path is not after the one before it");
    }
    return {};
}

} // namespace uta::ubundle::detail
