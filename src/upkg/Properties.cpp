#include "upkg/Properties.h"

#include "upkg/ByteReader.h"

#include <string>

namespace uta::upkg {
namespace {

constexpr std::uint8_t TYPE_MASK = 0x0F;
constexpr std::uint8_t SIZE_CODE_SHIFT = 4;
constexpr std::uint8_t SIZE_CODE_MASK = 0x07;
/// Bit 7 is the array flag for every type but Bool, where it is the value.
constexpr std::uint8_t HIGH_BIT = 0x80;

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// Sizes 0 to 4 are fixed widths; 5, 6 and 7 mean the size follows as a u8,
/// u16 or u32.
Result<std::size_t> readSize(ByteReader& reader, std::uint8_t sizeCode) {
    switch (sizeCode) {
    case 0: return std::size_t{1};
    case 1: return std::size_t{2};
    case 2: return std::size_t{4};
    case 3: return std::size_t{12};
    case 4: return std::size_t{16};
    case 5: {
        UTA_TRY(const std::uint8_t size, reader.readU8());
        return static_cast<std::size_t>(size);
    }
    case 6: {
        UTA_TRY(const std::uint16_t size, reader.readU16());
        return static_cast<std::size_t>(size);
    }
    default: {
        UTA_TRY(const std::uint32_t size, reader.readU32());
        return static_cast<std::size_t>(size);
    }
    }
}

/// A property tag's array index, which is NOT a compact index: the marker bits
/// lead and the value's high bits follow them, so this reads most-significant
/// byte first -- the one place the format is not little-endian (SS 4.8).
Result<std::uint32_t> readArrayIndex(ByteReader& reader) {
    UTA_TRY(const std::uint8_t first, reader.readU8());
    if ((first & HIGH_BIT) == 0) {
        return static_cast<std::uint32_t>(first);
    }
    if ((first & 0xC0u) == 0x80u) {
        UTA_TRY(const std::uint8_t second, reader.readU8());
        return (static_cast<std::uint32_t>(first & 0x7Fu) << 8) |
               static_cast<std::uint32_t>(second);
    }
    UTA_TRY(const std::span<const std::byte> rest, reader.readBytes(3));
    return (static_cast<std::uint32_t>(first & 0x3Fu) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(rest[0])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(rest[1])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(rest[2]));
}

std::string readCountedString(std::span<const std::byte> chars) {
    std::string text;
    text.reserve(chars.size());
    for (const std::byte byte : chars) {
        const auto value = static_cast<std::uint8_t>(byte);
        if (value == 0) {
            break; // the terminator, and anything after it, is not the name
        }
        text.push_back(static_cast<char>(value));
    }
    return text;
}

/// The UTF-16 form of a counted string, encoded to UTF-8. A Str whose length
/// is NEGATIVE is this: the magnitude counts 16-bit code units rather than
/// bytes, so the body is twice as long as the count.
///
/// Measured 2026-09-06 against `MapVoteULv1_2_SB.u` in the reference install,
/// where a map-vote list stores its entries this way -- and every one of them
/// satisfies `declared size == 1 + 2 * count`, which is what confirms the
/// reading rather than the shape merely looking plausible. Class defaults are
/// where this form shows up: UTA-0004 read level content and never met it.
///
/// Surrogate pairs are not recombined. Nothing in this content carries one,
/// and inventing a pairing rule for a case no measurement has seen would be a
/// guess in a decoder that otherwise only writes down what was observed.
std::string readCountedString16(std::span<const std::byte> units) {
    std::string text;
    text.reserve(units.size());
    for (std::size_t at = 0; at + 1 < units.size(); at += 2) {
        const auto low = static_cast<std::uint32_t>(static_cast<std::uint8_t>(units[at]));
        const auto high = static_cast<std::uint32_t>(static_cast<std::uint8_t>(units[at + 1]));
        const std::uint32_t unit = low | (high << 8);
        if (unit == 0) {
            break; // the terminator, and anything after it, is not the name
        }
        if (unit < 0x80) {
            text.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            text.push_back(static_cast<char>(0xC0u | (unit >> 6)));
            text.push_back(static_cast<char>(0x80u | (unit & 0x3Fu)));
        } else {
            text.push_back(static_cast<char>(0xE0u | (unit >> 12)));
            text.push_back(static_cast<char>(0x80u | ((unit >> 6) & 0x3Fu)));
            text.push_back(static_cast<char>(0x80u | (unit & 0x3Fu)));
        }
    }
    return text;
}

/// Decode the value bytes for the types whose layout the format fixes.
/// Everything else keeps its bytes -- see this file's header comment.
Result<PropertyValue> readValue(ByteReader& reader, const Package& package,
                                PropertyType type, std::size_t size) {
    switch (type) {
    case PropertyType::Byte: {
        UTA_TRY(const std::uint8_t value, reader.readU8());
        return PropertyValue{value};
    }
    case PropertyType::Int: {
        UTA_TRY(const std::int32_t value, reader.readI32());
        return PropertyValue{value};
    }
    case PropertyType::Float: {
        UTA_TRY(const float value, reader.readFloat());
        return PropertyValue{value};
    }
    case PropertyType::Object:
    case PropertyType::Class: {
        UTA_TRY(const std::int32_t raw, reader.readIndex());
        return PropertyValue{ObjectReference{raw}};
    }
    case PropertyType::Name: {
        UTA_TRY(const std::int32_t index, reader.readIndex());
        if (index < 0 || static_cast<std::size_t>(index) >= package.names().size()) {
            return std::unexpected(malformed(
                "a Name property names index " + std::to_string(index) +
                ", which is outside the name table"));
        }
        return PropertyValue{NameRef{static_cast<std::uint32_t>(index)}};
    }
    case PropertyType::Str: {
        // A length as a compact index, then that many CHARACTERS including the
        // terminator. A negative length is not malformed: it means the
        // characters are 16 bits wide, so the body is twice the magnitude.
        UTA_TRY(const std::int32_t length, reader.readIndex());
        if (length < 0) {
            // Negate through a wider type: the magnitude of INT32_MIN does not
            // fit in an int32_t, and this value comes from the file.
            const auto units = static_cast<std::size_t>(-static_cast<std::int64_t>(length));
            UTA_TRY(const std::span<const std::byte> chars, reader.readBytes(units * 2));
            return PropertyValue{readCountedString16(chars)};
        }
        UTA_TRY(const std::span<const std::byte> chars,
                reader.readBytes(static_cast<std::size_t>(length)));
        return PropertyValue{readCountedString(chars)};
    }
    case PropertyType::String: {
        // Exactly `size` bytes.
        UTA_TRY(const std::span<const std::byte> chars, reader.readBytes(size));
        return PropertyValue{readCountedString(chars)};
    }
    case PropertyType::Vector: {
        Vector3 value;
        UTA_TRY(value.x, reader.readFloat());
        UTA_TRY(value.y, reader.readFloat());
        UTA_TRY(value.z, reader.readFloat());
        return PropertyValue{value};
    }
    case PropertyType::Rotator: {
        Rotator value;
        UTA_TRY(value.pitch, reader.readI32());
        UTA_TRY(value.yaw, reader.readI32());
        UTA_TRY(value.roll, reader.readI32());
        return PropertyValue{value};
    }
    default: {
        UTA_TRY(const std::span<const std::byte> raw, reader.readBytes(size));
        return PropertyValue{raw};
    }
    }
}

} // namespace

Result<void> skipExecutionStackFrame(ByteReader& reader) {
    UTA_TRY(const std::int32_t node, reader.readIndex());
    UTA_TRY([[maybe_unused]] const std::int32_t stateNode, reader.readIndex());
    UTA_TRY([[maybe_unused]] const std::int64_t probeMask, reader.readI64());
    UTA_TRY([[maybe_unused]] const std::int32_t latentAction, reader.readI32());
    // The trailing offset is present only when the first reference is non-null.
    if (node != 0) {
        UTA_TRY([[maybe_unused]] const std::int32_t offset, reader.readIndex());
    }
    return {};
}

Result<std::vector<Property>> readPropertiesAt(const Package& package,
                                               ByteReader& reader) {
    std::vector<Property> properties;
    for (;;) {
        UTA_TRY(const std::int32_t rawName, reader.readIndex());
        if (rawName < 0 || static_cast<std::size_t>(rawName) >= package.names().size()) {
            return std::unexpected(malformed(
                "a property tag names index " + std::to_string(rawName) +
                ", which is outside the name table"));
        }

        UTA_TRY(const std::string_view tagName,
                package.name(static_cast<std::uint32_t>(rawName)));
        // The list ends at `None` and at no other condition. A loop that
        // stopped at the end of the buffer would return a truncated object as
        // a complete one (INV-9).
        if (tagName == "None") {
            return properties;
        }

        Property property;
        property.nameIndex = static_cast<std::uint32_t>(rawName);

        UTA_TRY(const std::uint8_t info, reader.readU8());
        const auto rawType = static_cast<std::uint8_t>(info & TYPE_MASK);
        if (rawType == 0 || rawType > static_cast<std::uint8_t>(PropertyType::FixedArray)) {
            return std::unexpected(malformed("a property tag declares type " +
                                             std::to_string(rawType) +
                                             ", which the format does not define"));
        }
        property.type = static_cast<PropertyType>(rawType);
        const auto sizeCode =
            static_cast<std::uint8_t>((info >> SIZE_CODE_SHIFT) & SIZE_CODE_MASK);

        // The struct name comes between the info byte and the size.
        if (property.type == PropertyType::Struct) {
            UTA_TRY(const std::int32_t structName, reader.readIndex());
            if (structName < 0 ||
                static_cast<std::size_t>(structName) >= package.names().size()) {
                return std::unexpected(malformed(
                    "a Struct property names struct index " + std::to_string(structName) +
                    ", which is outside the name table"));
            }
            property.structNameIndex = static_cast<std::uint32_t>(structName);
        }

        // Step 4 runs for a Bool too: only its VALUE bytes are skipped, never
        // the size field, because every Bool tag measured in real content
        // carries size code 5 and so a trailing size byte is present.
        UTA_TRY(const std::size_t size, readSize(reader, sizeCode));

        if (property.type == PropertyType::Bool) {
            // The value is bit 7 of the info byte, and no value bytes follow.
            property.value = (info & HIGH_BIT) != 0;
            properties.push_back(std::move(property));
            continue;
        }

        if ((info & HIGH_BIT) != 0) {
            UTA_TRY(property.arrayIndex, readArrayIndex(reader));
        }

        // The two structs whose layout the format fixes arrive spelled either
        // way -- as their own type, or as Struct with that struct name -- and
        // both decode identically.
        PropertyType decodeAs = property.type;
        if (property.type == PropertyType::Struct) {
            UTA_TRY(const std::string_view structName, package.name(property.structNameIndex));
            if (structName == "Vector") {
                decodeAs = PropertyType::Vector;
            } else if (structName == "Rotator") {
                decodeAs = PropertyType::Rotator;
            }
        }

        // Every non-Bool type consumes exactly the bytes its size field
        // declares (INV-10). Decoding then seeking to the declared end is what
        // keeps an undecoded property from desynchronising the list, and what
        // stops a decoded one drifting if its width is not what we assumed.
        const std::size_t bodyStart = reader.position();
        UTA_TRY(property.value, readValue(reader, package, decodeAs, size));
        // Rewind and skip rather than seeking to bodyStart + size: the sum of
        // two sizes read from the file can wrap, and skip() compares against
        // what remains instead of adding.
        UTA_CHECK(reader.seek(bodyStart));
        UTA_CHECK(reader.skip(size));

        properties.push_back(std::move(property));
    }
}

Result<PropertyList> readPropertyList(const Package& package,
                                      const ExportEntry& entry) {
    // A class object does not begin with a property list at all, and its
    // export is recognised by a NULL class reference -- not by one naming
    // `Class`, which no package writes. Class objects are UTA-0005, which
    // reaches their defaults through readPropertiesAt above.
    if (entry.objectClass.kind() == ObjectReferenceKind::Null) {
        return std::unexpected(Error(
            ErrorCode::InvalidArgument,
            "this export is a class, whose serialised data is a class table rather "
            "than a property list"));
    }

    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));
    PropertyList list;
    if (data.empty()) {
        return list;
    }

    ByteReader reader{data};
    if ((entry.objectFlags & OBJECT_FLAG_HAS_STACK) != 0) {
        UTA_CHECK(skipExecutionStackFrame(reader));
    }

    UTA_TRY(list.properties, readPropertiesAt(package, reader));
    // Where the list ended IS the cursor's position, so nothing re-parses to
    // recompute it.
    list.nativeOffset = reader.position();
    return list;
}

Result<std::vector<Property>> readProperties(const Package& package,
                                             const ExportEntry& entry) {
    UTA_TRY(PropertyList list, readPropertyList(package, entry));
    return std::move(list.properties);
}

} // namespace uta::upkg
