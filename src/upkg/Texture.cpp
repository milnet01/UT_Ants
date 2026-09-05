#include "upkg/Texture.h"

#include "upkg/ByteReader.h"
#include "upkg/Properties.h"

#include <array>
#include <string>
#include <utility>

namespace uta::upkg {
namespace {

/// The version from which a mip's WidthOffset and a sound's NextOffset are
/// present. Measured across the reference install: stock content is not all
/// version 68, so this branch is exercised by real packages rather than being
/// an edge case (SS 2.1).
constexpr std::uint16_t OFFSET_FIELD_MIN_VERSION = 63;

/// A spark is eight bytes. FireTexture stores its own count and that many
/// sparks after the mip chain; the count is the ARRAY's and is not the
/// `NumSparks` property, which differs (SS 4.6).
constexpr std::size_t SPARK_BYTES = 8;

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// The classes SS 4.6 models. Recognised by NAME rather than by ancestry:
/// resolving "is this class a Texture?" properly needs the class table, which
/// is UTA-0005 and is not built (SS 3.3 item 4).
constexpr std::array<std::string_view, 5> MODELLED_CLASSES{
    "Texture", "WetTexture", "IceTexture", "ScriptedTexture", "FireTexture"};

Result<Mip> readMip(ByteReader& reader, std::uint16_t packageVersion,
                    std::size_t serialOffset) {
    Mip mip;

    // Present from version 63. Redundant -- it is the offset just past this
    // mip's data, so it equals the data start plus the size -- which makes it
    // a free cross-check, and INV-5 spends it.
    std::uint32_t widthOffset = 0;
    const bool hasWidthOffset = packageVersion >= OFFSET_FIELD_MIN_VERSION;
    if (hasWidthOffset) {
        UTA_TRY(widthOffset, reader.readU32());
    }

    UTA_TRY(const std::int32_t size, reader.readIndex());
    if (size < 0) {
        return std::unexpected(
            malformed("a mip declares " + std::to_string(size) + " bytes"));
    }
    const std::size_t dataStart = reader.position();
    UTA_TRY(mip.pixels, reader.readBytes(static_cast<std::size_t>(size)));

    if (hasWidthOffset) {
        // WidthOffset is an offset into the WHOLE package file, not into the
        // export -- so the serialOffset term is what makes the comparison
        // meaningful. Forget it and every real texture is refused.
        const std::size_t expected = serialOffset + dataStart + static_cast<std::size_t>(size);
        if (widthOffset != expected) {
            return std::unexpected(malformed(
                "a mip's WidthOffset is " + std::to_string(widthOffset) +
                " where its data ends at " + std::to_string(expected)));
        }
    }

    UTA_TRY(mip.width, reader.readU32());
    UTA_TRY(mip.height, reader.readU32());
    UTA_TRY(mip.bitsWidth, reader.readU8());
    UTA_TRY(mip.bitsHeight, reader.readU8());
    return mip;
}

Result<std::vector<Mip>> readChain(ByteReader& reader, std::uint16_t packageVersion,
                                   std::size_t serialOffset) {
    UTA_TRY(const std::uint8_t count, reader.readU8());
    std::vector<Mip> mips;
    // One byte per mip is the floor, so the count is checked against what
    // remains before the vector is grown (INV-4).
    if (static_cast<std::size_t>(count) > reader.remaining()) {
        return std::unexpected(malformed(
            "a texture declares " + std::to_string(count) + " mips, more than the " +
            std::to_string(reader.remaining()) + " bytes remaining can hold"));
    }
    mips.reserve(count);
    for (std::uint8_t index = 0; index < count; ++index) {
        UTA_TRY(Mip mip, readMip(reader, packageVersion, serialOffset));
        mips.push_back(std::move(mip));
    }
    return mips;
}

/// True when the export's property list says a compressed chain follows.
bool hasCompressedChain(const std::vector<Property>& properties, const Package& package) {
    for (const Property& property : properties) {
        const Result<std::string_view> name = package.name(property.nameIndex);
        if (!name.has_value() || *name != "bHasComp") {
            continue;
        }
        if (const auto* flag = std::get_if<bool>(&property.value)) {
            return *flag;
        }
    }
    return false;
}

} // namespace

bool isModelledTextureClass(std::string_view className) noexcept {
    for (const std::string_view modelled : MODELLED_CLASSES) {
        if (className == modelled) {
            return true;
        }
    }
    return false;
}

Result<Texture> readTexture(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const std::string_view className, package.objectName(entry.objectClass));
    if (!isModelledTextureClass(className)) {
        // Refused by name rather than read as its nearest relative (INV-8).
        // WaveTexture shares the mip chain and then stores something further
        // that this item does not describe; reading its mips and leaving the
        // rest unread would defeat SS 4.3 for every caller at once.
        return std::unexpected(Error(
            ErrorCode::InvalidArgument,
            "class " + std::string(className) + " is not a texture class this reader models"));
    }

    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));

    Texture texture;
    if (data.empty()) {
        return texture; // SS 6: a sizeless export is ordinary, not an error
    }

    ByteReader reader{data, list.nativeOffset};
    UTA_TRY(texture.mips, readChain(reader, package.header().packageVersion, entry.serialOffset));

    if (hasCompressedChain(list.properties, package)) {
        UTA_TRY(texture.compressedMips,
                readChain(reader, package.header().packageVersion, entry.serialOffset));
    }

    if (className == "FireTexture") {
        UTA_TRY(const std::int32_t sparks, reader.readIndex());
        if (sparks < 0) {
            return std::unexpected(
                malformed("a FireTexture declares " + std::to_string(sparks) + " sparks"));
        }
        const auto wanted = static_cast<std::size_t>(sparks);
        if (wanted > reader.remaining() / SPARK_BYTES) {
            return std::unexpected(malformed(
                "a FireTexture declares " + std::to_string(sparks) +
                " sparks, more than the " + std::to_string(reader.remaining()) +
                " bytes remaining can hold"));
        }
        UTA_CHECK(reader.skip(wanted * SPARK_BYTES));
    }

    if (reader.remaining() != 0) {
        return std::unexpected(malformed(
            "a texture left " + std::to_string(reader.remaining()) +
            " bytes unread; the layout does not match the export"));
    }
    return texture;
}

Result<Palette> readPalette(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));

    Palette palette;
    if (data.empty()) {
        return palette;
    }

    ByteReader reader{data, list.nativeOffset};
    // The count is read rather than assumed to be 256: it is a value from the
    // file, and SS 2 item 1 governs every one of those.
    UTA_TRY(const std::int32_t count, reader.readIndex());
    if (count < 0) {
        return std::unexpected(
            malformed("a Palette declares " + std::to_string(count) + " entries"));
    }
    constexpr std::size_t ENTRY_BYTES = 4;
    const auto wanted = static_cast<std::size_t>(count);
    if (wanted > reader.remaining() / ENTRY_BYTES) {
        return std::unexpected(malformed(
            "a Palette declares " + std::to_string(count) +
            " entries, more than the " + std::to_string(reader.remaining()) +
            " bytes remaining can hold"));
    }

    palette.entries.reserve(wanted);
    for (std::int32_t index = 0; index < count; ++index) {
        PaletteEntry colour;
        UTA_TRY(colour.r, reader.readU8());
        UTA_TRY(colour.g, reader.readU8());
        UTA_TRY(colour.b, reader.readU8());
        UTA_TRY(colour.a, reader.readU8());
        palette.entries.push_back(colour);
    }

    if (reader.remaining() != 0) {
        return std::unexpected(malformed(
            "a Palette left " + std::to_string(reader.remaining()) +
            " bytes unread; the layout does not match the export"));
    }
    return palette;
}

} // namespace uta::upkg
