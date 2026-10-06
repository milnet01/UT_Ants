// Cli.h says what this is for and what it does not do.

#include "Cli.h"

#include "core/FileSystem.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "umat/Resolve.h"
#include "upkg/Level.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"
#include "ut-ants/Png.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace uta::mappic {

namespace {

namespace stdfs = std::filesystem;

std::string foldCase(std::string_view text) {
    std::string folded{text};
    for (char& ch : folded) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return folded;
}

/// The LevelInfo the level's own actor list names, or null -- ut-dump's rule:
/// an editor can leave other LevelInfo exports behind, and only the level's
/// own one is the map's.
const upkg::ExportEntry* levelInfoOf(const upkg::Package& map, const upkg::Level& level) {
    for (const upkg::ObjectReference slot : level.actors) {
        if (slot.kind() != upkg::ObjectReferenceKind::Export || slot.index() >= map.exports().size())
            continue;
        const upkg::ExportEntry& entry = map.exports()[slot.index()];
        const auto name = map.objectName(entry.objectClass);
        if (name.has_value() && *name == "LevelInfo") return &entry;
    }
    return nullptr;
}

/// The property `want`, compared case-insensitively, at array index 0.
const upkg::Property* propertyNamed(const upkg::Package& holder,
                                    const std::vector<upkg::Property>& properties, std::string_view want) {
    for (const upkg::Property& property : properties) {
        const auto name = holder.name(property.nameIndex);
        if (name.has_value() && property.arrayIndex == 0 && foldCase(*name) == want) return &property;
    }
    return nullptr;
}

/// The non-null object reference `want` holds, if it holds one.
std::optional<upkg::ObjectReference> referenceNamed(const upkg::Package& holder,
                                                    const std::vector<upkg::Property>& properties,
                                                    std::string_view want) {
    const upkg::Property* const property = propertyNamed(holder, properties, want);
    if (property == nullptr) return std::nullopt;
    const auto* const reference = std::get_if<upkg::ObjectReference>(&property->value);
    if (reference == nullptr || reference->kind() == upkg::ObjectReferenceKind::Null) return std::nullopt;
    return *reference;
}

Preview none(std::string why) {
    return Preview{std::nullopt, std::move(why)};
}

} // namespace

Result<Preview> previewOf(const upkg::Package& map, std::string_view mapName,
                          const upkg::PackageResolver& resolver) {
    UTA_TRY(const upkg::ExportEntry* const levelExport, ubake::detail::findLevel(map, mapName));
    UTA_TRY(const upkg::Level level, upkg::readLevel(map, *levelExport));

    const upkg::ExportEntry* const levelInfo = levelInfoOf(map, level);
    if (levelInfo == nullptr) return none("the level holds no LevelInfo");
    const auto infoProperties = upkg::readProperties(map, *levelInfo);
    if (!infoProperties.has_value())
        return none("its LevelInfo's properties do not read: " + std::string(infoProperties.error().message()));
    const auto screenshot = referenceNamed(map, *infoProperties, "screenshot");
    if (!screenshot.has_value()) return none("its LevelInfo names no Screenshot");

    // The baker's own route to a texture, which follows an import into the
    // package it names; and to the texture's palette, which can be an import
    // of yet another package (UTA-0155).
    const auto texture = ubake::detail::resolveTexture(map, mapName, *screenshot, resolver);
    if (!texture.has_value() || texture->holder == nullptr)
        return none("its Screenshot does not resolve to a texture in the install");
    const upkg::Package& holder = *texture->holder;

    const auto properties = upkg::readProperties(holder, *texture->entry);
    if (!properties.has_value())
        return none("its Screenshot's properties do not read: " + std::string(properties.error().message()));
    // Format 0 is TEXF_P8, the only format decoded here. It is the default and
    // so is rarely stored, but a stored 0 is still palettised.
    if (const upkg::Property* const format = propertyNamed(holder, *properties, "format")) {
        const auto* const value = std::get_if<std::uint8_t>(&format->value);
        if (value == nullptr || *value != 0)
            return none("its Screenshot is stored in a format other than 8-bit palettised");
    }
    const auto paletteReference = referenceNamed(holder, *properties, "palette");
    if (!paletteReference.has_value()) return none("its Screenshot names no palette");
    const auto palette = ubake::detail::resolveTexture(holder, mapName, *paletteReference, resolver);
    if (!palette.has_value() || palette->holder == nullptr)
        return none("its Screenshot's palette does not resolve in the install");
    const auto colours = upkg::readPalette(*palette->holder, *palette->entry);
    if (!colours.has_value())
        return none("its Screenshot's palette does not read: " + std::string(colours.error().message()));

    const auto mips = upkg::readTexture(holder, *texture->entry);
    if (!mips.has_value())
        return none("its Screenshot does not read as a texture: " + std::string(mips.error().message()));
    if (mips->mips.empty() || mips->mips[0].pixels.empty())
        return none("its Screenshot stores no pixels");
    auto image = umat::resolve(mips->mips[0], *colours, false);
    if (!image.has_value())
        return none("its Screenshot does not decode: " + std::string(image.error().message()));
    return Preview{std::move(*image), {}};
}

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    if (args.size() != 3) {
        err << "usage: map-pic <install> <map.unr> <out.png>\n"
               "Saves the picture the map ships with -- its LevelInfo's Screenshot\n"
               "texture -- as a PNG and prints \"preview\", or prints \"none\" and writes\n"
               "nothing when it has none. The install resolves a picture kept in\n"
               "another package.\n";
        return 2;
    }
    const stdfs::path installDir{std::string{args[0]}};
    const stdfs::path mapPath{std::string{args[1]}};
    const stdfs::path outPath{std::string{args[2]}};

    auto install = ubake::Install::open(installDir);
    if (!install.has_value()) {
        err << "map-pic: " << fs::utf8(installDir) << ": " << install.error().message() << "\n";
        return 1;
    }
    const auto mapped = fs::MappedFile::open(mapPath);
    if (!mapped.has_value() || mapped->bytes().empty()) {
        err << "map-pic: " << fs::utf8(mapPath) << ": unreadable or empty\n";
        return 1;
    }
    const auto map = upkg::Package::open(mapped->bytes());
    if (!map.has_value()) {
        err << "map-pic: " << fs::utf8(mapPath) << ": " << map.error().message() << "\n";
        return 1;
    }

    const upkg::PackageResolver resolver = install->resolver();
    const auto preview = previewOf(*map, mapPath.stem().string(), resolver);
    if (!preview.has_value()) {
        err << "map-pic: " << fs::utf8(mapPath) << ": " << preview.error().message() << "\n";
        return 1;
    }
    if (!preview->image.has_value()) {
        err << "map-pic: " << fs::utf8(mapPath) << ": no preview: " << preview->none << "\n";
        out << "none\n";
        return 0;
    }

    const umat::Image& image = *preview->image;
    const auto png = client::encodePng(image.pixels, image.width, image.height);
    if (!png.has_value()) {
        err << "map-pic: " << png.error().message() << "\n";
        return 1;
    }
    if (outPath.has_parent_path()) {
        std::error_code ignored; // a failure here surfaces as the write's own error
        stdfs::create_directories(outPath.parent_path(), ignored);
    }
    if (const auto wrote = fs::writeFileAtomically(outPath, *png); !wrote.has_value()) {
        err << "map-pic: " << fs::utf8(outPath) << ": " << wrote.error().message() << "\n";
        return 1;
    }
    out << "preview\n";
    return 0;
}

} // namespace uta::mappic
