#include "upkg/Level.h"

#include "upkg/ByteReader.h"
#include "upkg/Properties.h"

#include <string>
#include <utility>

namespace uta::upkg {
namespace {

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// The smallest a reach-spec record can be: three int32s and a byte are fixed,
/// and the two object references are compact indices of at least one byte.
/// Checked against what is left before the vector is grown -- the reason is
/// Geometry.cpp's, that reserving from an unchecked count is how a four-byte
/// edit asks for gigabytes.
constexpr std::size_t REACH_SPEC_MIN_BYTES = 4 + 1 + 1 + 4 + 4 + 4 + 1;

/// Fields following the reach-spec array, before the run of zeroes the export
/// ends with. Derived over the reference install rather than assumed: see
/// docs/specs/UTA-0057-level-tail-and-reachspecs.md SS 4.5.
constexpr int TRAILER_INDEX_COUNT = 18;

/// A length-prefixed string, consumed and discarded. The level's URL is not
/// returned (Level.h), so its four strings and its option list are read only
/// to put the cursor on the byte after them.
Result<void> skipString(ByteReader& reader) {
    UTA_TRY(const std::int32_t length, reader.readIndex());
    if (length < 0) {
        return std::unexpected(
            malformed("a string in a level's URL declares length " + std::to_string(length)));
    }
    return reader.skip(static_cast<std::size_t>(length));
}

/// The level's `FURL`: protocol, host, map and portal, an option list, then
/// the port and a validity flag. UTA-0004 SS 4.9 verified this shape; this
/// reader consumes it because SS 4.3 admits no partial read.
Result<void> skipURL(ByteReader& reader) {
    for (int field = 0; field < 4; ++field) {
        UTA_CHECK(skipString(reader));
    }
    UTA_TRY(const std::int32_t options, reader.readIndex());
    if (options < 0) {
        return std::unexpected(malformed("a level's URL declares " + std::to_string(options) +
                                         " options"));
    }
    if (static_cast<std::size_t>(options) > reader.remaining()) {
        return std::unexpected(malformed(
            "a level's URL declares " + std::to_string(options) + " options, more than the " +
            std::to_string(reader.remaining()) + " bytes remaining can hold"));
    }
    for (std::int32_t option = 0; option < options; ++option) {
        UTA_CHECK(skipString(reader));
    }
    UTA_TRY([[maybe_unused]] const std::int32_t port, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::int32_t valid, reader.readI32());
    return {};
}

/// What follows the reach-spec array: a float, then TRAILER_INDEX_COUNT
/// compact indices, then zeroes to the end of the export.
///
/// The float and the indices are what 837 of 837 maps in the reference
/// install carry; one of the indices is an object reference to a `TextBuffer`
/// on the 200 maps where it is not null, which is what identifies the region
/// as the engine's editor text blocks rather than merely fitting its width.
///
/// The trailing run is TEN zero bytes on every map but one, where it is
/// eleven. That one byte is not explained, so it is not modelled as a field:
/// what is required is that every byte of the run be ZERO. A non-zero byte
/// there is a layout this reader does not describe, and is refused.
///
/// This consumes to the end of the export, so it is what makes UTA-0004
/// INV-1 hold for this reader -- see readLevel's last statement.
Result<void> readTrailer(ByteReader& reader) {
    UTA_TRY([[maybe_unused]] const float timeSeconds, reader.readFloat());
    for (int field = 0; field < TRAILER_INDEX_COUNT; ++field) {
        UTA_TRY([[maybe_unused]] const std::int32_t value, reader.readIndex());
    }
    const std::size_t trailing = reader.remaining();
    UTA_TRY(const std::span<const std::byte> rest, reader.readBytes(trailing));
    for (std::size_t offset = 0; offset < rest.size(); ++offset) {
        if (rest[offset] != std::byte{0}) {
            return std::unexpected(malformed(
                "a level ends with " + std::to_string(trailing) +
                " bytes of which the one at offset " + std::to_string(offset) +
                " is not zero, so its tail is a layout this reader does not describe"));
        }
    }
    return {};
}

} // namespace

Result<Level> readLevel(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));

    // A sizeless `Level` is refused rather than returned empty, which is where
    // this reader departs from readPolys. INV-4: an empty reach-spec array is
    // returned only when the file STATES one, and an export with no bytes
    // states nothing. Every map in the reference install carries a Level
    // export with data, so this refuses no real content.
    if (data.empty()) {
        return std::unexpected(malformed("a Level export has no serialised data"));
    }

    Level level;
    ByteReader reader{data, list.nativeOffset};

    // Num, then Max. Max is the array's allocated size and is not used: it is
    // read so the cursor lands on the first actor, which SS 4.3 requires.
    UTA_TRY(const std::int32_t slots, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::int32_t capacity, reader.readI32());
    if (slots < 0) {
        return std::unexpected(
            malformed("a Level declares " + std::to_string(slots) + " actor slots"));
    }
    // One byte per slot is the floor: an object reference is a compact index.
    if (static_cast<std::size_t>(slots) > reader.remaining()) {
        return std::unexpected(malformed(
            "a Level declares " + std::to_string(slots) + " actor slots, more than the " +
            std::to_string(reader.remaining()) + " bytes remaining can hold"));
    }
    level.rawSlotCount = static_cast<std::uint32_t>(slots);
    for (std::int32_t slot = 0; slot < slots; ++slot) {
        UTA_TRY(const std::int32_t reference, reader.readIndex());
        const ObjectReference actor{reference};
        // Null slots are dropped and counted, never returned -- UTA-0004
        // INV-9. A stock map's array is largely holes.
        if (actor.kind() != ObjectReferenceKind::Null) {
            level.actors.push_back(actor);
        }
    }

    UTA_CHECK(skipURL(reader));

    // The level's Model, returned unresolved. UTA-0057 consumed it; UTA-0011
    // SS 4.5 returns it, because its baker builds rooms from the export this
    // names.
    UTA_TRY(const std::int32_t model, reader.readIndex());
    level.model = ObjectReference{model};

    UTA_TRY(const std::int32_t specs, reader.readIndex());
    if (specs < 0) {
        return std::unexpected(
            malformed("a Level declares " + std::to_string(specs) + " reach specs"));
    }
    if (static_cast<std::size_t>(specs) > reader.remaining() / REACH_SPEC_MIN_BYTES) {
        return std::unexpected(malformed(
            "a Level declares " + std::to_string(specs) + " reach specs, more than the " +
            std::to_string(reader.remaining()) + " bytes remaining can hold"));
    }
    level.reachSpecs.reserve(static_cast<std::size_t>(specs));

    for (std::int32_t index = 0; index < specs; ++index) {
        ReachSpec spec;
        UTA_TRY(spec.distance, reader.readI32());
        UTA_TRY(const std::int32_t start, reader.readIndex());
        UTA_TRY(const std::int32_t end, reader.readIndex());
        spec.start = ObjectReference{start};
        spec.end = ObjectReference{end};
        UTA_TRY(spec.collisionRadius, reader.readI32());
        UTA_TRY(spec.collisionHeight, reader.readI32());
        UTA_TRY(spec.reachFlags, reader.readI32());
        UTA_TRY(spec.pruned, reader.readU8());
        level.reachSpecs.push_back(spec);
    }

    // UTA-0004 INV-1 is STRUCTURAL here rather than asserted: readTrailer's
    // last act consumes the rest of the export, so this reader cannot end
    // anywhere but its end. A trailing-byte check after it would be
    // unreachable. What that shifts onto readTrailer is the whole of the
    // rule -- it refuses any byte it cannot account for rather than skipping
    // to the end, which is what keeps the guarantee worth having.
    UTA_CHECK(readTrailer(reader));
    return level;
}

} // namespace uta::upkg
