#include "upkg/Texture.h"

#include "core/Ascii.h"
#include "upkg/ByteReader.h"
#include "upkg/Properties.h"
#include "upkg/ReadSupport.h"

#include <array>
#include <string>
#include <utility>

namespace uta::upkg {
namespace {

/// A spark is eight bytes. FireTexture stores its own count and that many
/// sparks after the mip chain; the count is the ARRAY's and is not the
/// `NumSparks` property, which differs (SS 4.6).
constexpr std::size_t SPARK_BYTES = 8;

using detail::malformed;

/// The classes SS 4.6 models. Recognised by NAME, not by ancestry, although
/// readAncestry (Class.h, UTA-0005) could now say "descends from Texture": a
/// subclass outside this list may serialise more than its parent, so INV-8
/// refuses it rather than reading it as its nearest modelled ancestor.
constexpr std::array<std::string_view, 6> MODELLED_CLASSES{
    "Texture", "WetTexture", "IceTexture", "ScriptedTexture", "FireTexture", "WaveTexture"};

Result<Mip> readMip(ByteReader& reader, std::uint16_t packageVersion,
                    std::size_t serialOffset) {
    Mip mip;

    // Present from version 63. Redundant -- it is the offset just past this
    // mip's data, so it equals the data start plus the size -- which makes it
    // a free cross-check, and INV-5 spends it.
    std::uint32_t widthOffset = 0;
    const bool hasWidthOffset = packageVersion >= detail::OFFSET_FIELD_MIN_VERSION;
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
    // One byte per mip is the floor.
    UTA_CHECK(detail::checkCount(reader, count, 1, "a texture", "mips"));
    mips.reserve(count);
    for (std::uint8_t index = 0; index < count; ++index) {
        UTA_TRY(Mip mip, readMip(reader, packageVersion, serialOffset));
        mips.push_back(std::move(mip));
    }
    return mips;
}

/// True when the property list holds a Bool property `wanted` that is true.
bool boolProperty(const std::vector<Property>& properties, const Package& package,
                  std::string_view wanted) {
    for (const Property& property : properties) {
        const Result<std::string_view> name = package.name(property.nameIndex);
        if (!name.has_value() || *name != wanted) {
            continue;
        }
        if (const auto* flag = std::get_if<bool>(&property.value)) {
            return *flag;
        }
    }
    return false;
}

/// True when the export's property list says a compressed chain follows.
bool hasCompressedChain(const std::vector<Property>& properties, const Package& package) {
    return boolProperty(properties, package, "bHasComp");
}

} // namespace

bool isModelledTextureClass(std::string_view className) noexcept {
    // Ignoring case, as UE1 compares names (UTA-0405).
    for (const std::string_view modelled : MODELLED_CLASSES) {
        if (equalsIgnoringAsciiCase(className, modelled)) {
            return true;
        }
    }
    return false;
}

Result<Texture> readTexture(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const std::string_view className, package.objectName(entry.objectClass));
    if (!isModelledTextureClass(className)) {
        // Refused by name rather than read as its nearest relative (INV-8):
        // a layout nobody has measured could consume the wrong bytes and
        // still end at the right place.
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

    if (equalsIgnoringAsciiCase(className, "FireTexture")) {
        UTA_TRY(const std::int32_t sparks, reader.readIndex());
        UTA_CHECK(detail::checkCount(reader, sparks, SPARK_BYTES, "a FireTexture", "sparks"));
        const auto wanted = static_cast<std::size_t>(sparks);
        texture.sparks.reserve(wanted);
        for (std::size_t i = 0; i < wanted; ++i) {
            std::array<std::uint8_t, SPARK_BYTES> bytes{};
            for (std::uint8_t& byte : bytes) {
                UTA_TRY(byte, reader.readU8());
            }
            texture.sparks.push_back(
                Spark{bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]});
        }
    }

    UTA_CHECK(detail::expectConsumed(reader, "a texture"));
    return texture;
}

Result<bool> isMaskedTexture(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    return boolProperty(list.properties, package, "bMasked");
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
    constexpr std::size_t ENTRY_BYTES = 4;
    UTA_CHECK(detail::checkCount(reader, count, ENTRY_BYTES, "a Palette", "entries"));
    const auto wanted = static_cast<std::size_t>(count);

    palette.entries.reserve(wanted);
    for (std::int32_t index = 0; index < count; ++index) {
        PaletteEntry colour;
        UTA_TRY(colour.r, reader.readU8());
        UTA_TRY(colour.g, reader.readU8());
        UTA_TRY(colour.b, reader.readU8());
        UTA_TRY(colour.a, reader.readU8());
        palette.entries.push_back(colour);
    }

    UTA_CHECK(detail::expectConsumed(reader, "a Palette"));
    return palette;
}

} // namespace uta::upkg
