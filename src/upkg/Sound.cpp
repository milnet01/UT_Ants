#include "upkg/Sound.h"

#include "upkg/ByteReader.h"
#include "upkg/Properties.h"
#include "upkg/ReadSupport.h"

#include <string>
#include <utility>

namespace uta::upkg {
namespace {

using detail::malformed;

} // namespace

Result<Sound> readSound(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));

    Sound sound;
    if (data.empty()) {
        return sound; // SS 6: a sizeless export is ordinary, not an error
    }

    ByteReader reader{data, list.nativeOffset};

    UTA_TRY(const std::int32_t format, reader.readIndex());
    UTA_TRY(sound.formatName,
            detail::checkNameIndex(format, package.names().size(), "a Sound names format"));

    // Present from version 63, and redundant: it is the offset just past the
    // payload. INV-5 spends it as a cross-check, exactly as SS 4.6 spends the
    // mip chain's WidthOffset.
    std::uint32_t nextOffset = 0;
    const bool hasNextOffset =
        package.header().packageVersion >= detail::OFFSET_FIELD_MIN_VERSION;
    if (hasNextOffset) {
        UTA_TRY(nextOffset, reader.readU32());
    }

    UTA_TRY(const std::int32_t size, reader.readIndex());
    if (size < 0) {
        return std::unexpected(
            malformed("a Sound declares " + std::to_string(size) + " bytes"));
    }
    const std::size_t dataStart = reader.position();
    UTA_TRY(sound.data, reader.readBytes(static_cast<std::size_t>(size)));

    if (hasNextOffset) {
        // An offset into the WHOLE package file, so serialOffset is what makes
        // the comparison meaningful -- the same term SS 4.6 needs.
        const std::size_t expected =
            entry.serialOffset + dataStart + static_cast<std::size_t>(size);
        if (nextOffset != expected) {
            return std::unexpected(malformed(
                "a Sound's NextOffset is " + std::to_string(nextOffset) +
                " where its data ends at " + std::to_string(expected)));
        }
    }

    UTA_CHECK(detail::expectConsumed(reader, "a Sound"));
    return sound;
}

} // namespace uta::upkg
