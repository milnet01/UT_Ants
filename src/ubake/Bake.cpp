// One bake and writing it -- docs/specs/UTA-0011-map-baker.md SS 4.5 to SS 4.7.

#include "ubake/Bake.h"

#include "core/FileSystem.h"
#include "core/Sha256.h"
#include "ubake/Actors.h"
#include "ubake/Collision.h"
#include "ubake/FireStill.h"
#include "ubake/Flames.h"
#include "ubake/Geometry.h"
#include "ubake/LightModel.h"
#include "ubake/Liquids.h"
#include "ubake/LightProbes.h"
#include "ubake/Occlusion.h"
#include "ubake/Movers.h"
#include "ubake/Name.h"
#include "ubake/Strips.h"
#include "ubake/SurfaceRays.h"
#include "ubake/Zones.h"
#include "umat/Derive.h"
#include "umat/Enlarge.h"
#include "umat/Fingerprint.h"
#include "umat/Generate.h"
#include "umat/Material.h"
#include "umat/Resolve.h"
#include "unav/Build.h"
#include "upkg/Class.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"
#include "urecipe/Lookup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <expected>
#include <format>
#include <mutex>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <optional>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <system_error>
#include <utility>
#include <variant>

namespace uta::ubake {
namespace {

std::string prefixFor(std::string_view mapName) {
    return "baking " + std::string(mapName);
}

Error malformed(std::string_view mapName, const std::string& message) {
    return Error(ErrorCode::MalformedData, prefixFor(mapName) + ": " + message);
}

/// `result`, its error naming the map -- SS 4.5's refusals all do.
template <class T>
Result<T> naming(Result<T> result, std::string_view mapName) {
    if (!result.has_value()) return std::unexpected(result.error().withContext(prefixFor(mapName)));
    return result;
}

/// The class an export is an instance of, folded; empty for a class export.
std::string classOf(const upkg::Package& package, const upkg::ExportEntry& entry) {
    if (entry.objectClass.kind() == upkg::ObjectReferenceKind::Null) return {};
    const auto name = package.objectName(entry.objectClass);
    return name.has_value() ? detail::fold(*name) : std::string{};
}

std::string nameOf(const upkg::Package& package, std::uint32_t nameIndex) {
    return std::string(package.name(nameIndex).value_or(""));
}

/// A recipe assignment as the override umat::applied takes. urecipe mirrors
/// the fields rather than linking umat, which the runtime may not (UTA-0113).
umat::CuratedOverride overrideOf(const urecipe::MaterialAssignment& assignment) {
    return {.metallic = assignment.metallic,
            .baseRoughness = assignment.baseRoughness,
            .emissive = assignment.emissive,
            .emissiveThreshold = assignment.emissiveThreshold,
            .parallaxDepth = assignment.parallaxDepth};
}

// ------------------------------------------------------------------ SS 4.6

/// Where a surface's texture reference leads. The id's two parts come from
/// the reference itself, so a texture that does not resolve still names the
/// variant it skipped.
struct TextureSite {
    std::string package; ///< materialId's `package`
    std::string path;    ///< materialId's `path`
    const upkg::Package* holder = nullptr;
    const upkg::ExportEntry* entry = nullptr;
    std::string unresolved; ///< why `holder` is null
};

/// The names of `entry`'s outers, outermost first, then its own, joined by
/// '.' -- UTA-0009 SS 4.6. Bounded by the export count, so a cyclic outer
/// chain cannot hang it.
std::string exportPath(const upkg::Package& package, const upkg::ExportEntry& entry) {
    std::string path = nameOf(package, entry.objectName);
    upkg::ObjectReference outer = entry.outer;
    for (std::size_t depth = 0; outer.kind() == upkg::ObjectReferenceKind::Export
                                && outer.index() < package.exports().size()
                                && depth < package.exports().size();
         ++depth) {
        const upkg::ExportEntry& group = package.exports()[outer.index()];
        path = nameOf(package, group.objectName) + "." + path;
        outer = group.outer;
    }
    return path;
}

/// An import followed to the package it lives in -- used for a surface's
/// texture and, since UTA-0155, for a texture's palette.
struct ImportedObject {
    std::string package;            ///< the package the chain ends at, as the name table spells it
    std::vector<std::string> names; ///< the object's name first, its outermost group last
    std::string broken;             ///< why the chain is unusable; empty when it is sound
    /// UTA-0176: the class the import names its object by, folded. UT99's
    /// linker finds an import by class as well as by name, and a package can
    /// hold two objects of one name: NaliFX's SHANEFX group holds TORCHES2 the
    /// FireTexture and TORCHES2 its Palette.
    std::string className;

    /// `<group>.<name>`, outermost first.
    [[nodiscard]] std::string path() const {
        std::string out;
        for (auto name = names.rbegin(); name != names.rend(); ++name) out += (out.empty() ? "" : ".") + *name;
        return out;
    }
};

/// Walks `import`'s outer chain in `from`, collecting names on the way. The
/// chain is bounded by the import table.
ImportedObject walkImport(const upkg::Package& from, const upkg::ImportEntry& import) {
    ImportedObject out;
    out.className = detail::fold(nameOf(from, import.className));
    const std::span<const upkg::ImportEntry> imports = from.imports();
    const upkg::ImportEntry* current = &import;
    for (std::size_t hops = 0;; ++hops) {
        if (current->outer.kind() == upkg::ObjectReferenceKind::Null) {
            out.package = nameOf(from, current->objectName);
            break;
        }
        out.names.push_back(nameOf(from, current->objectName));
        if (current->outer.kind() != upkg::ObjectReferenceKind::Import
            || current->outer.index() >= imports.size() || hops > imports.size()) {
            out.broken = "its import's outer chain does not end at a package";
            break;
        }
        current = &imports[current->outer.index()];
    }
    return out;
}

/// Whether `candidate`'s own name and its chain of outer names match `chain`
/// -- the texture's name first, its outermost group last -- compared folded,
/// with nothing outside the chain.
bool matchesChain(const upkg::Package& holder, const upkg::ExportEntry& candidate,
                  const std::vector<std::string>& chain) {
    const upkg::ExportEntry* current = &candidate;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (detail::fold(nameOf(holder, current->objectName)) != chain[i]) return false;
        const upkg::ObjectReference outer = current->outer;
        if (i + 1 == chain.size()) return outer.kind() == upkg::ObjectReferenceKind::Null;
        if (outer.kind() != upkg::ObjectReferenceKind::Export
            || outer.index() >= holder.exports().size())
            return false;
        current = &holder.exports()[outer.index()];
    }
    return false;
}

/// The export of `holder` an import leads to: the object's name first and its
/// outermost group last, of the class the import names -- or null where
/// `holder` holds none. Matching the class is UT99's own rule (UTA-0176); by
/// name alone 1,924 of the reference install's FireTexture imports found
/// their Palette of the same name first.
const upkg::ExportEntry* exportNamed(const upkg::Package& holder, const ImportedObject& imported) {
    std::vector<std::string> chain;
    for (const std::string& name : imported.names) chain.push_back(detail::fold(name));
    for (const upkg::ExportEntry& candidate : holder.exports()) {
        if (!matchesChain(holder, candidate, chain)) continue;
        const auto classOf = holder.objectName(candidate.objectClass); // "None" for a class export
        const std::string className = candidate.objectClass.kind() == upkg::ObjectReferenceKind::Null
                                          ? std::string("class")
                                          : detail::fold(classOf.has_value() ? std::string(*classOf) : std::string());
        if (className == imported.className) return &candidate;
    }
    return nullptr;
}

/// An object one of a texture's properties names, and the package holding it.
struct ObjectSite {
    const upkg::Package* holder = nullptr;
    const upkg::ExportEntry* entry = nullptr;
};

/// The object `reference` names from `from`: an export of `from`, or an import
/// followed through the resolver into the package it lives in -- a texture's
/// palette, or its SourceTexture (UTA-0155). `what` names it in a refusal.
/// A null reference is the caller's to refuse.
std::expected<ObjectSite, std::string> objectAt(const upkg::Package& from, upkg::ObjectReference reference,
                                                const upkg::PackageResolver& resolver, const std::string& what) {
    if (reference.kind() == upkg::ObjectReferenceKind::Export) {
        if (reference.index() >= from.exports().size())
            return std::unexpected("its " + what + " names an export past its package's export table");
        return ObjectSite{&from, &from.exports()[reference.index()]};
    }
    if (reference.index() >= from.imports().size())
        return std::unexpected("its " + what + " names an import past its package's import table");
    const ImportedObject imported = walkImport(from, from.imports()[reference.index()]);
    const std::string package = detail::fold(imported.package);
    if (!imported.broken.empty()) return std::unexpected("its " + what + ": " + imported.broken);
    if (imported.names.empty())
        return std::unexpected("its " + what + " reference names the package " + package + ", not an object");
    const auto found = resolver(package);
    if (!found.has_value())
        return std::unexpected("its " + what + "'s package " + package + " did not open: "
                               + std::string(found.error().message()));
    if (*found == nullptr)
        return std::unexpected("its " + what + "'s package " + package + " is not in the install, or does not open");
    const upkg::ExportEntry* const entry = exportNamed(**found, imported);
    if (entry == nullptr)
        return std::unexpected("its " + what + "'s package " + package + " holds no " + imported.path());
    return ObjectSite{*found, entry};
}

/// The object reference a property list carries under `wanted` (folded), if any.
std::optional<upkg::ObjectReference> objectProperty(const upkg::Package& holder,
                                                    std::span<const upkg::Property> properties,
                                                    std::string_view wanted) {
    for (const upkg::Property& property : properties) {
        if (detail::fold(nameOf(holder, property.nameIndex)) != wanted) continue;
        if (const auto* const reference = std::get_if<upkg::ObjectReference>(&property.value)) return *reference;
    }
    return std::nullopt;
}

/// SS 4.6 "Which textures": an export reference is that export of the map; an
/// import reference resolves its outermost outer through the resolver, then
/// takes the export of that package whose name and outer names match, of the
/// class the import names (UTA-0176).
Result<TextureSite> siteOf(const upkg::Package& map, std::string_view mapName,
                           upkg::ObjectReference reference,
                           const upkg::PackageResolver& resolver) {
    TextureSite site;

    if (reference.kind() == upkg::ObjectReferenceKind::Export) {
        site.package = std::string(mapName);
        // A surface's texture comes out of the Model's DATA, which
        // Package::open did not validate.
        if (reference.index() >= map.exports().size()) {
            site.path = "export" + std::to_string(reference.index());
            site.unresolved = "the surface names export " + std::to_string(reference.index())
                              + ", past the map's export table";
            return site;
        }
        site.holder = &map;
        site.entry = &map.exports()[reference.index()];
        site.path = exportPath(map, *site.entry);
        return site;
    }

    // An import: walk to the root of its outer chain.
    const std::span<const upkg::ImportEntry> imports = map.imports();
    if (reference.index() >= imports.size()) {
        site.package = std::string(mapName);
        site.path = "import" + std::to_string(reference.index());
        site.unresolved = "the surface names import " + std::to_string(reference.index())
                          + ", past the map's import table";
        return site;
    }
    const ImportedObject imported = walkImport(map, imports[reference.index()]);
    site.package = imported.package;
    site.path = imported.path();

    if (!imported.broken.empty()) {
        if (site.package.empty()) site.package = std::string(mapName);
        site.unresolved = imported.broken;
        return site;
    }
    if (imported.names.empty()) {
        site.path = site.package;
        site.unresolved = "the surface's texture reference names a package, not a texture";
        return site;
    }

    UTA_TRY(const upkg::Package* const holder, resolver(detail::fold(site.package)));
    if (holder == nullptr) {
        site.unresolved = "package " + site.package + " is not in the install, or does not open";
        return site;
    }
    if (const upkg::ExportEntry* const entry = exportNamed(*holder, imported)) {
        site.holder = holder;
        site.entry = entry;
        return site;
    }
    site.unresolved = "package " + site.package + " holds no " + site.path;
    return site;
}

/// UTA-0177: the palette entry nearest the palette's mean colour, which a
/// texture with no picture anywhere is filled with. Alpha is not weighed.
std::size_t nearestToMean(const upkg::Palette& palette) {
    std::array<long, 3> sum{};
    for (const upkg::PaletteEntry& entry : palette.entries) {
        sum[0] += entry.r;
        sum[1] += entry.g;
        sum[2] += entry.b;
    }
    const auto count = static_cast<long>(palette.entries.size());
    std::size_t nearest = 0;
    long best = std::numeric_limits<long>::max();
    for (std::size_t i = 0; i < palette.entries.size(); ++i) {
        const upkg::PaletteEntry& entry = palette.entries[i];
        const long dr = entry.r - sum[0] / count;
        const long dg = entry.g - sum[1] / count;
        const long db = entry.b - sum[2] / count;
        if (const long distance = dr * dr + dg * dg + db * db; distance < best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

/// UTA-0263 SS 4.2: the palette at eight evenly spaced heats, coldest first,
/// decoded to linear. A fire's texel is its heat, 0 to 255, and indexes the
/// palette directly; a shorter palette's last entry stands for the heats past
/// it. `palette` has at least one entry.
/// UTA-0286 SS 4.3: a non-flame FireTexture's fire look, or why it has none.
std::expected<ubundle::FireLook, std::string> fireLookOf(const upkg::Mip& base, std::span<const upkg::Spark> sparks,
                                                         const FireSettings& settings, const upkg::Palette& palette,
                                                         bool masked) {
    if (palette.entries.size() < 256)
        return std::unexpected(std::format("its palette has {} entries, not 256", palette.entries.size()));
    if (base.width == 0 || base.height == 0 || base.width > ubundle::FIRE_SIZE_MAX
        || base.height > ubundle::FIRE_SIZE_MAX)
        return std::unexpected(std::format("its size {}x{} is outside 1 to {} a side", base.width, base.height,
                                           ubundle::FIRE_SIZE_MAX));
    if (sparks.size() > ubundle::FIRE_SPARKS_MAX)
        return std::unexpected(std::format("it has {} sparks, more than {}", sparks.size(), ubundle::FIRE_SPARKS_MAX));
    if (!std::isfinite(settings.maxFrameRate) || settings.maxFrameRate < 0)
        return std::unexpected(std::string("its MaxFrameRate is negative or not finite"));
    ubundle::FireLook look;
    look.size = {static_cast<std::uint16_t>(base.width), static_cast<std::uint16_t>(base.height)};
    look.renderHeat = settings.renderHeat;
    look.rising = settings.rising ? 1 : 0;
    look.masked = masked ? 1 : 0;
    look.sparksLimit = settings.sparksLimit;
    look.maxFrameRate = settings.maxFrameRate;
    for (std::size_t i = 0; i < look.palette.size(); ++i)
        look.palette[i] = {palette.entries[i].r, palette.entries[i].g, palette.entries[i].b};
    for (const upkg::Spark& spark : sparks)
        look.sparks.push_back({spark.type, spark.heat, spark.x, spark.y, spark.byteA, spark.byteB, spark.byteC,
                               spark.byteD});
    return look;
}

ubundle::FlameLook flameLookOf(const upkg::Palette& palette) {
    ubundle::FlameLook look;
    constexpr std::size_t HOTTEST = 255;
    const std::size_t last = look.ramp.size() - 1;
    for (std::size_t i = 0; i < look.ramp.size(); ++i) {
        const std::size_t heat = (i * HOTTEST + last / 2) / last; // rounded
        const upkg::PaletteEntry& entry = palette.entries[std::min(heat, palette.entries.size() - 1)];
        look.ramp[i] = {static_cast<float>(linearOf(entry.r)), static_cast<float>(linearOf(entry.g)),
                        static_cast<float>(linearOf(entry.b))};
    }
    return look;
}

/// UTA-0105 SS 4.2: the merged defaults of the class `entry` is an instance
/// of, or why they cannot be read. A chain that did not reach its root -- its
/// package missing, Fire.u among them -- is refused, since a default it would
/// have set reads as 0.
std::expected<std::vector<upkg::EffectiveProperty>, std::string> classDefaultsOf(
    const upkg::Package& holder, std::string_view packageName, const upkg::ExportEntry& entry,
    const upkg::PackageResolver& resolver) {
    const auto site = upkg::resolveClass(holder, packageName, entry.objectClass, resolver);
    if (!site.has_value()) return std::unexpected("its class does not resolve: " + std::string(site.error().message()));
    if (site->resolved.package == nullptr)
        return std::unexpected("its class " + site->package + "." + site->name + " is not in the install");
    const auto ancestry = upkg::readAncestry(*site->resolved.package, *site->resolved.entry, resolver);
    if (!ancestry.has_value())
        return std::unexpected("its class's ancestry does not read: " + std::string(ancestry.error().message()));
    if (ancestry->end != upkg::AncestryEnd::Root)
        return std::unexpected("its class's ancestry stops short of its root");
    auto defaults = upkg::effectiveDefaults(*ancestry);
    if (!defaults.has_value())
        return std::unexpected("its class's defaults do not read: " + std::string(defaults.error().message()));
    return std::move(*defaults);
}

/// A made variant, and the texels one repeat of its texture spans on each axis
/// -- UTA-0109 SS 4.4.
struct MadeVariant {
    umat::Material material;
    double uSize = 0;
    double vSize = 0;
    /// The base level's linear mean, for the bounce -- UTA-0112 SS 4.5. Empty
    /// when no pixel of it is opaque.
    std::optional<Rgb> albedo;
    /// UTA-0263 SS 4.2: set when the picture is a flame's.
    std::optional<ubundle::FlameLook> flame;
    /// UTA-0105 SS 4.2: set for a liquid class whose settings read.
    std::optional<ubundle::LiquidLook> liquid;
    /// UTA-0105 SS 6: why a liquid class carries no look; empty otherwise.
    std::string liquidSkipped;
    /// UTA-0286 SS 4.3: set for a FireTexture that is not a flame.
    std::optional<ubundle::FireLook> fire;
    /// UTA-0286 SS 6: why such a FireTexture carries no look; empty otherwise.
    std::string fireSkipped;
    /// UTA-0161: the emit map's linear mean, which the bounce adds as the
    /// surface's own light. Empty for a material that does not glow, and for a
    /// flame, which scene.frag draws without its emit map.
    std::optional<Rgb> emission;
    /// UTA-0270: an Ice texture's GlassTexture, `<id>:glass`; empty otherwise.
    std::optional<ubundle::CompressedTexture> glass;
    /// UTA-0275: the texture's DetailTexture, `<id>:detail`; empty otherwise.
    std::optional<ubundle::CompressedTexture> detail;
};

/// UTA-0270: an Ice texture's GlassTexture as a one-level BC4 picture of each
/// texel's grey -- its palette colour's Rec. 709 luma, the value the original's
/// frames show the shift follows. None when it names none, does not read, or
/// is not the Ice texture's own size, which Epic's manual requires; the look
/// then pans its source alone, as before.
std::optional<ubundle::CompressedTexture> glassOf(const upkg::Package& holder,
                                                  const std::vector<upkg::Property>& properties,
                                                  const upkg::PackageResolver& resolver, const std::string& id,
                                                  std::uint32_t width, std::uint32_t height, JobSystem& jobs) {
    const auto reference = objectProperty(holder, properties, "glasstexture");
    if (!reference.has_value() || reference->kind() == upkg::ObjectReferenceKind::Null) return std::nullopt;
    const auto site = objectAt(holder, *reference, resolver, "GlassTexture");
    if (!site.has_value()) return std::nullopt;
    const auto glassProperties = upkg::readProperties(*site->holder, *site->entry);
    if (!glassProperties.has_value()) return std::nullopt;
    const auto paletteReference = objectProperty(*site->holder, *glassProperties, "palette");
    if (!paletteReference.has_value() || paletteReference->kind() == upkg::ObjectReferenceKind::Null)
        return std::nullopt;
    const auto paletteSite = objectAt(*site->holder, *paletteReference, resolver, "GlassTexture's palette");
    if (!paletteSite.has_value()) return std::nullopt;
    const auto palette = upkg::readPalette(*paletteSite->holder, *paletteSite->entry);
    if (!palette.has_value()) return std::nullopt;
    const auto texture = upkg::readTexture(*site->holder, *site->entry);
    if (!texture.has_value() || texture->mips.empty()) return std::nullopt;
    const upkg::Mip& level = texture->mips[0];
    const std::size_t count = std::size_t{width} * height;
    if (level.width != width || level.height != height || level.pixels.size() < count) return std::nullopt;
    umat::Image grey{width, height, 1, std::vector<std::byte>(count)};
    for (std::size_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>(level.pixels[i]);
        if (index >= palette->entries.size()) continue; // 0, as a missing colour
        const upkg::PaletteEntry& e = palette->entries[index];
        grey.pixels[i] = static_cast<std::byte>(std::lround(0.2126 * e.r + 0.7152 * e.g + 0.0722 * e.b));
    }
    auto compressed = umat::compress(id + ":glass", std::span<const umat::Image>(&grey, 1), ubundle::BlockFormat::BC4,
                                     static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), jobs);
    if (!compressed.has_value()) return std::nullopt;
    return std::move(*compressed);
}

/// UTA-0275: a texture's DetailTexture as a BC4 picture of each texel's grey,
/// its palette colour's Rec. 709 luma, with the whole mip chain: UT99 draws it
/// over the surface near the camera, multiplied in, so only its light and dark
/// matter and it is seen at many sizes. At its own size, which is not the
/// surface's. None when it names none or does not read; the surface is then
/// drawn without one, as before.
std::optional<ubundle::CompressedTexture> detailOf(const upkg::Package& holder,
                                                   const std::vector<upkg::Property>& properties,
                                                   const upkg::PackageResolver& resolver, const std::string& id,
                                                   JobSystem& jobs) {
    const auto reference = objectProperty(holder, properties, "detailtexture");
    if (!reference.has_value() || reference->kind() == upkg::ObjectReferenceKind::Null) return std::nullopt;
    const auto site = objectAt(holder, *reference, resolver, "DetailTexture");
    if (!site.has_value()) return std::nullopt;
    const auto detailProperties = upkg::readProperties(*site->holder, *site->entry);
    if (!detailProperties.has_value()) return std::nullopt;
    const auto paletteReference = objectProperty(*site->holder, *detailProperties, "palette");
    if (!paletteReference.has_value() || paletteReference->kind() == upkg::ObjectReferenceKind::Null)
        return std::nullopt;
    const auto paletteSite = objectAt(*site->holder, *paletteReference, resolver, "DetailTexture's palette");
    if (!paletteSite.has_value()) return std::nullopt;
    const auto palette = upkg::readPalette(*paletteSite->holder, *paletteSite->entry);
    if (!palette.has_value()) return std::nullopt;
    const auto texture = upkg::readTexture(*site->holder, *site->entry);
    if (!texture.has_value() || texture->mips.empty()) return std::nullopt;
    const upkg::Mip& level = texture->mips[0];
    const std::size_t count = std::size_t{level.width} * level.height;
    if (count == 0 || level.pixels.size() < count || level.width > 0xFFFF || level.height > 0xFFFF)
        return std::nullopt;
    umat::Image grey{level.width, level.height, 1, std::vector<std::byte>(count)};
    for (std::size_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>(level.pixels[i]);
        if (index >= palette->entries.size()) continue; // 0, as a missing colour
        const upkg::PaletteEntry& e = palette->entries[index];
        grey.pixels[i] = static_cast<std::byte>(std::lround(0.2126 * e.r + 0.7152 * e.g + 0.0722 * e.b));
    }
    const std::vector<umat::Image> levels = umat::mipChain(grey);
    auto compressed = umat::compress(id + ":detail", levels, ubundle::BlockFormat::BC4,
                                     static_cast<std::uint16_t>(level.width), static_cast<std::uint16_t>(level.height),
                                     jobs);
    if (!compressed.has_value()) return std::nullopt;
    return std::move(*compressed);
}

/// One variant, or why it cannot be made. The error arm is a SKIP and never a
/// refusal of the bake: every failure SS 4.6 lists belongs to one texture, and
/// the bake goes on without it.
std::expected<MadeVariant, std::string> makeVariant(const TextureSite& site,
                                                    const std::string& id, bool masked,
                                                    const upkg::PackageResolver& resolver, JobSystem& jobs,
                                                    const detail::CuratedLookup& curated,
                                                    const urecipe::MaterialAssignment* assignment,
                                                    TextureCache* textureCache,
                                                    const detail::FlameLookup& isFlame) {
    if (site.holder == nullptr) return std::unexpected(site.unresolved);
    const upkg::Package& holder = *site.holder;

    // Step 1. A Format property skips the texture -- UTA-0010 SS 4.2's rule.
    // Nothing here decodes a format other than palettised, and a block format
    // storing one byte a texel would otherwise be read as palette indices.
    const auto properties = upkg::readProperties(holder, *site.entry);
    if (!properties.has_value())
        return std::unexpected("its properties do not read: "
                               + std::string(properties.error().message()));
    std::optional<upkg::ObjectReference> paletteReference;
    for (const upkg::Property& property : *properties) {
        const std::string name = detail::fold(nameOf(holder, property.nameIndex));
        if (name == "format")
            return std::unexpected(std::string(
                "it carries a Format property, and only palettised textures are decoded"));
        if (name == "palette") {
            if (const auto* reference = std::get_if<upkg::ObjectReference>(&property.value))
                paletteReference = *reference;
        }
    }

    // Step 2. The palette reference is property DATA, so its range is checked.
    // UTA-0155: an import is followed into the package it names, through the
    // resolver the textures take -- 175 of the reference install's textures
    // keep their palette in another package.
    if (!paletteReference.has_value()
        || paletteReference->kind() == upkg::ObjectReferenceKind::Null)
        return std::unexpected(std::string("it names no palette"));
    const auto paletteSite = objectAt(holder, *paletteReference, resolver, "palette");
    if (!paletteSite.has_value()) return std::unexpected(paletteSite.error());
    const auto palette = upkg::readPalette(*paletteSite->holder, *paletteSite->entry);
    if (!palette.has_value())
        return std::unexpected("its palette does not read: " + std::string(palette.error().message()));

    // Step 3. For a procedural texture the base level is its stored still
    // picture (SS 3 decision 5).
    const auto texture = upkg::readTexture(holder, *site.entry);
    if (!texture.has_value())
        return std::unexpected("it does not read as a texture: "
                               + std::string(texture.error().message()));
    if (texture->mips.empty()) return std::unexpected(std::string("it has no mip levels"));
    const upkg::Mip& base = texture->mips[0];

    // UTA-0155: a procedural texture that stores no pixels of its own -- a
    // WetTexture, an IceTexture, a ScriptedTexture -- shows its SourceTexture's
    // picture, through that texture's own palette, as a still image (the user's
    // choice, 2026-09-14; moving it is UTA-0105's). One hop: a source storing
    // no pixels either takes UTA-0177's flat fill, as a texture naming no
    // source does. `base` still sets the size a repeat spans.
    const upkg::Mip* shown = &base;
    const upkg::Palette* shownPalette = &*palette;
    std::optional<upkg::Texture> sourceTexture;
    std::optional<upkg::Palette> sourcePalette;
    // UTA-0176: a FireTexture stores no pixels and names no SourceTexture --
    // UT99 draws it from its sparks every frame -- so its still is simulated,
    // through its own palette. Moving it is UTA-0105's.
    // A picture made here rather than read: the fire's, or UTA-0177's fill.
    std::vector<std::byte> madeIndices;
    upkg::Mip madeLevel;
    // UTA-0177: no picture anywhere -- no pixels, and no source that has any.
    // No engine gives one (SurrealEngine leaves such a texture untouched, and
    // ucc exports a flat buffer), so it is a flat fill in the texture's own
    // colours, never magenta.
    const auto fillFlat = [&]() -> std::expected<void, std::string> {
        if (palette->entries.empty()) return std::unexpected(std::string("its palette has no entries"));
        madeIndices.assign(std::size_t{base.width} * base.height, static_cast<std::byte>(nearestToMean(*palette)));
        madeLevel = base;
        madeLevel.pixels = madeIndices;
        shown = &madeLevel;
        shownPalette = &*palette;
        return {};
    };
    const bool storesNoPixels = base.pixels.size() < std::size_t{base.width} * base.height;
    // A picture made here is as large as the file says, and nothing else bounds
    // that: 65535 a side with no pixels would allocate gigabytes, refusing the
    // whole bake (review-code 2026-09-26). umat refuses above 8192 in any case.
    constexpr std::uint32_t MADE_EDGE_LIMIT = 8192;
    if (storesNoPixels && (base.width > MADE_EDGE_LIMIT || base.height > MADE_EDGE_LIMIT))
        return std::unexpected(std::format("it has no picture and says it is {}x{}, above {} a side",
                                           base.width, base.height, MADE_EDGE_LIMIT));
    const auto className = holder.objectName(site.entry->objectClass);
    const std::string foldedClass = className.has_value() ? detail::fold(*className) : std::string();
    if (storesNoPixels && foldedClass == "firetexture") {
        madeIndices = fireStill(base.width, base.height, texture->sparks, fireSettingsOf(holder, *properties));
        madeLevel = base;
        madeLevel.pixels = madeIndices;
        shown = &madeLevel;
    } else if (storesNoPixels) {
        const auto sourceReference = objectProperty(holder, *properties, "sourcetexture");
        if (!sourceReference.has_value() || sourceReference->kind() == upkg::ObjectReferenceKind::Null) {
            if (auto filled = fillFlat(); !filled.has_value()) return std::unexpected(filled.error());
        } else {
            const auto source = objectAt(holder, *sourceReference, resolver, "SourceTexture");
            if (!source.has_value()) return std::unexpected(source.error());
            const auto sourceProperties = upkg::readProperties(*source->holder, *source->entry);
            if (!sourceProperties.has_value())
                return std::unexpected("its SourceTexture's properties do not read: "
                                       + std::string(sourceProperties.error().message()));
            const auto sourcePaletteReference = objectProperty(*source->holder, *sourceProperties, "palette");
            if (!sourcePaletteReference.has_value()
                || sourcePaletteReference->kind() == upkg::ObjectReferenceKind::Null)
                return std::unexpected(std::string("its SourceTexture names no palette"));
            const auto sourcePaletteSite =
                objectAt(*source->holder, *sourcePaletteReference, resolver, "SourceTexture's palette");
            if (!sourcePaletteSite.has_value()) return std::unexpected(sourcePaletteSite.error());
            auto readSourcePalette = upkg::readPalette(*sourcePaletteSite->holder, *sourcePaletteSite->entry);
            if (!readSourcePalette.has_value())
                return std::unexpected("its SourceTexture's palette does not read: "
                                       + std::string(readSourcePalette.error().message()));
            sourcePalette = std::move(*readSourcePalette);
            auto readSource = upkg::readTexture(*source->holder, *source->entry);
            if (!readSource.has_value())
                return std::unexpected("its SourceTexture does not read as a texture: "
                                       + std::string(readSource.error().message()));
            sourceTexture = std::move(*readSource);
            if (sourceTexture->mips.empty()) return std::unexpected(std::string("its SourceTexture has no mip levels"));
            const upkg::Mip& picture = sourceTexture->mips[0];
            if (picture.pixels.size() < std::size_t{picture.width} * picture.height) {
                // A source that is itself procedural: DamageWet's is a WaveTexture.
                if (auto filled = fillFlat(); !filled.has_value()) return std::unexpected(filled.error());
            } else {
                shown = &picture;
                shownPalette = &*sourcePalette;
            }
        }
    }

    // Step 4. UTA-0010 SS 4.5's order: the defaults, then the curated
    // library's entry for this picture, then the map recipe's assignment.
    umat::MaterialSettings settings{};
    std::optional<ubundle::FlameLook> flame;
    if (const auto fingerprint = umat::pictureFingerprint(*shown, *shownPalette)) {
        if (const umat::CuratedOverride* const entry = curated(*fingerprint))
            settings = umat::applied(settings, *entry);
        // UTA-0263 SS 4.1: the flame list is keyed by the same fingerprint. A
        // flame keeps its still as well, so the steps below run unchanged.
        if (!shownPalette->entries.empty() && isFlame(*fingerprint)) flame = flameLookOf(*shownPalette);
    }
    // UTA-0286 SS 4.3: a FireTexture the flame list does not name replays in
    // the renderer from its sparks; it keeps its still as well.
    std::optional<ubundle::FireLook> fire;
    std::string fireSkipped;
    if (storesNoPixels && foldedClass == "firetexture" && !flame.has_value()) {
        auto look = fireLookOf(base, texture->sparks, fireSettingsOf(holder, *properties), *palette, masked);
        if (look.has_value()) fire = std::move(*look);
        else fireSkipped = std::move(look).error();
    }
    // UTA-0113 SS 4.5: field by field, so a recipe setting only `metallic`
    // keeps a library `emissive`; its upscale is set directly.
    if (assignment != nullptr) {
        settings = umat::applied(settings, overrideOf(*assignment));
        if (assignment->upscale.has_value()) settings.requestedUpscale = *assignment->upscale;
    }

    // UTA-0105 SS 4.1: a liquid class carries its own settings, its class's
    // defaults under them. The still above is unchanged by it.
    std::optional<ubundle::LiquidLook> liquid;
    std::string liquidSkipped;
    if (const auto kind = liquidKindOf(foldedClass)) {
        const auto defaults = classDefaultsOf(holder, site.package, *site.entry, resolver);
        if (!defaults.has_value()) {
            liquidSkipped = defaults.error();
        } else {
            // Array index 0 only: every setting read here is a scalar.
            const LiquidSetting setting = [&](std::string_view wanted) -> std::optional<std::uint8_t> {
                for (const upkg::Property& property : *properties)
                    if (property.arrayIndex == 0 && detail::fold(nameOf(holder, property.nameIndex)) == wanted)
                        if (const auto* value = std::get_if<std::uint8_t>(&property.value)) return *value;
                for (const upkg::EffectiveProperty& effective : *defaults)
                    if (effective.property.arrayIndex == 0 && detail::fold(effective.name) == wanted)
                        if (const auto* value = std::get_if<std::uint8_t>(&effective.property.value)) return *value;
                return std::nullopt;
            };
            liquid = liquidLookOf(*kind, setting, base.width, base.height, *palette);
            // UTA-0270: MoveIce is a Bool, so the byte lookup above cannot read it.
            if (liquid.has_value() && liquid->kind == ubundle::LiquidKind::Ice) {
                std::optional<bool> moveIce;
                for (const upkg::Property& property : *properties)
                    if (property.arrayIndex == 0 && detail::fold(nameOf(holder, property.nameIndex)) == "moveice")
                        if (const auto* value = std::get_if<bool>(&property.value)) moveIce = *value;
                if (!moveIce.has_value())
                    for (const upkg::EffectiveProperty& effective : *defaults)
                        if (effective.property.arrayIndex == 0 && detail::fold(effective.name) == "moveice")
                            if (const auto* value = std::get_if<bool>(&effective.property.value)) moveIce = *value;
                liquid->moveIce = moveIce.value_or(false) ? 1 : 0;
            }
            if (!liquid.has_value())
                liquidSkipped = std::format("its size {}x{} is outside 1 to {} a side", base.width, base.height,
                                            ubundle::LIQUID_SIZE_MAX);
        }
    }

    // Step 5.
    const auto resolved = umat::resolve(*shown, *shownPalette, masked);
    if (!resolved.has_value())
        return std::unexpected("umat::resolve refused it: " + std::string(resolved.error().message()));
    // UTA-0246: a picture that is not a power of two a side -- doom2tex's
    // 64x72 -- is stretched to the next one up; generate takes no other.
    // uSize and vSize below still come from `base`, so it maps as before.
    const auto rgba = umat::toPowerOfTwo(*resolved);
    if (!rgba.has_value())
        return std::unexpected("umat::toPowerOfTwo refused it: " + std::string(rgba.error().message()));
    // UTA-0148: generate is a pure function of id, picture and settings, so a
    // material made by an earlier bake is taken from the cache instead.
    std::optional<TextureCacheKey> key;
    std::optional<umat::Material> kept;
    if (textureCache != nullptr) {
        key = textureCacheKey(id, *rgba, settings);
        kept = textureCache->find(*key, id);
    }
    auto material = kept.has_value() ? Result<umat::Material>(std::move(*kept))
                                     : umat::generate(id, *rgba, settings, jobs);
    if (textureCache != nullptr && !kept.has_value() && material.has_value())
        textureCache->store(*key, *material);
    if (!material.has_value())
        return std::unexpected("umat::generate refused it: "
                               + std::string(material.error().message()));
    const double scale = detail::textureScale(holder, *properties);
    // UTA-0161: the emit map is the picture where its height reaches the
    // threshold (umat's emissiveOf); its mean is taken at the picture's own
    // size, before generate's upscale, as the albedo is.
    std::optional<Rgb> emission;
    if (settings.emissive && !flame.has_value())
        emission = meanAlbedo(umat::emissiveOf(*rgba, umat::heightOf(*rgba), settings.emissiveThreshold));
    std::optional<ubundle::CompressedTexture> glass;
    if (liquid.has_value() && liquid->kind == ubundle::LiquidKind::Ice)
        glass = glassOf(holder, *properties, resolver, id, base.width, base.height, jobs);
    std::optional<ubundle::CompressedTexture> detail = detailOf(holder, *properties, resolver, id, jobs);
    return MadeVariant{std::move(*material), base.width * scale, base.height * scale,
                       meanAlbedo(*rgba), flame, liquid, std::move(liquidSkipped), std::move(fire),
                       std::move(fireSkipped), emission, std::move(glass), std::move(detail)};
}

struct Materials {
    std::vector<ubundle::CompressedTexture> textures;
    /// UTA-0113 SS 4.5: each recipe assignment naming a texture the map does
    /// not use, ascending.
    std::vector<std::string> recipeUnused;
    std::vector<ubundle::MaterialRecord> records;
    std::vector<SkippedTexture> skipped;
    /// UTA-0105 SS 6: liquid variants made with no liquid look, and why.
    std::vector<SkippedTexture> skippedLiquids;
    /// UTA-0286 SS 6: non-flame FireTextures made with no fire look, and why.
    std::vector<SkippedTexture> skippedFires;
    /// What a surface wears, by (texture reference, masked) -- the lookup GEOM
    /// is built with (UTA-0109 SS 4.4). A variant not made has no entry.
    std::map<std::pair<std::int32_t, bool>, SurfaceMaterial> bySurface;
    /// Each made material's reflectance for the bounce, by id -- UTA-0112
    /// SS 4.5. A material whose base level has no opaque pixel holds
    /// DEFAULT_ALBEDO.
    std::map<std::string, Rgb, std::less<>> albedo;
    /// UTA-0161: each glowing material's mean emission, by id. A material
    /// that does not glow has no entry.
    std::map<std::string, Rgb, std::less<>> emission;
    /// Each made material's index in `records`, by id -- what FLAM names.
    std::map<std::string, std::uint32_t, std::less<>> recordIndex;
};

Result<Materials> bakeMaterials(const upkg::Package& map, std::string_view mapName,
                                const std::vector<const upkg::Model*>& models,
                                const upkg::PackageResolver& resolver, JobSystem& jobs,
                                const detail::CuratedLookup& curated, TextureCache* textureCache,
                                const detail::FlameLookup& isFlame, const urecipe::Recipe* recipe) {
    // Which variants each distinct reference needs. A map's surfaces name a
    // few hundred textures thousands of times, so each is resolved once. The
    // level's Model and every mover's contribute alike -- UTA-0119 SS 4.6.
    struct Needs {
        bool opaque = false;
        bool masked = false;
    };
    std::map<std::int32_t, Needs> byReference;
    for (const upkg::Model* model : models)
        for (const upkg::BspSurf& surf : model->surfs) {
            if (surf.texture.kind() == upkg::ObjectReferenceKind::Null) continue;
            Needs& needs = byReference[surf.texture.raw()];
            // The SURFACE decides, not the texture's own bMasked: surfaces
            // sharing one texture disagree about index 0 (UTA-0009 SS 2 item 4).
            if ((surf.polyFlags & PF_MASKED) != 0)
                needs.masked = true;
            else
                needs.opaque = true;
        }

    struct Variant {
        const TextureSite* site = nullptr;
        bool masked = false;
        std::vector<std::int32_t> references; ///< every reference resolving to it
        const urecipe::MaterialAssignment* assignment = nullptr; ///< the recipe's, for both variants
    };
    // UTA-0113: a recipe names a texture without #masked, so both of its
    // variants take the assignment.
    std::map<std::string, const urecipe::MaterialAssignment*, std::less<>> assigned;
    if (recipe != nullptr)
        for (const urecipe::MaterialAssignment& assignment : recipe->materials)
            assigned.emplace(assignment.texture, &assignment);
    std::set<std::string, std::less<>> used; // each texture's unmasked id
    std::vector<TextureSite> sites;
    sites.reserve(byReference.size());
    std::map<std::string, Variant> variants; // by material id: ascending bytewise
    for (const auto& [raw, needs] : byReference) {
        UTA_TRY(TextureSite site, siteOf(map, mapName, upkg::ObjectReference{raw}, resolver));
        sites.push_back(std::move(site));
        const TextureSite* const placed = &sites.back();
        const std::string texture = umat::materialId(placed->package, placed->path, false);
        const auto found = assigned.find(texture);
        const urecipe::MaterialAssignment* const assignment = found == assigned.end() ? nullptr : found->second;
        used.insert(texture);
        // Two references can resolve to one texture, so a variant keeps every
        // reference naming it and each surface still finds its material.
        if (needs.opaque)
            variants.try_emplace(umat::materialId(placed->package, placed->path, false),
                                 Variant{placed, false, {}, assignment})
                .first->second.references.push_back(raw);
        if (needs.masked)
            variants.try_emplace(umat::materialId(placed->package, placed->path, true),
                                 Variant{placed, true, {}, assignment})
                .first->second.references.push_back(raw);
    }

    // UTA-0149's sibling UTA-0148: a few variants are made at once, each also
    // spreading its own work over the job system (a job that waits runs
    // queued work, INV-10 of UTA-0002, so nesting cannot deadlock). The
    // results are still TAKEN in id order, which is what keeps TEXS
    // independent of which job finishes first (INV-1). IN_FLIGHT bounds how
    // many variants' working images exist at once, so peak memory stays near
    // one variant's. The Install resolver opens and caches, and is not safe
    // from two threads, so it is called under a lock; the packages it hands
    // back are only read.
    constexpr std::size_t IN_FLIGHT = 4;
    std::mutex resolving;
    const upkg::PackageResolver locked = [&](std::string_view name) {
        const std::scoped_lock hold(resolving);
        return resolver(name);
    };
    std::vector<std::pair<const std::string*, const Variant*>> ordered;
    ordered.reserve(variants.size());
    for (const auto& [id, variant] : variants) ordered.emplace_back(&id, &variant);

    Materials out;
    for (const auto& [texture, assignment] : assigned)
        if (!used.contains(texture)) out.recipeUnused.push_back(texture);
    for (std::size_t first = 0; first < ordered.size(); first += IN_FLIGHT) {
        const std::size_t count = std::min(IN_FLIGHT, ordered.size() - first);
        std::vector<std::optional<std::expected<MadeVariant, std::string>>> batch(count);
        const std::size_t threw = jobs.parallelFor(count, [&](std::size_t i) {
            const auto& [id, variant] = ordered[first + i];
            batch[i].emplace(makeVariant(*variant->site, *id, variant->masked, locked, jobs, curated,
                                         variant->assignment, textureCache, isFlame));
        });
        if (threw != 0) return fail(ErrorCode::Unknown, "making a material threw");
#ifdef __GLIBC__
        // Measured: the working images are now allocated on worker threads,
        // and glibc keeps each thread's arena at its high water, so without
        // this a bake's peak rose by over 100 MB. Handing freed pages back
        // after each batch keeps it near the one-at-a-time bake's.
        malloc_trim(0);
#endif
        for (std::size_t i = 0; i < count; ++i) {
            const std::string& id = *ordered[first + i].first;
            const Variant& variant = *ordered[first + i].second;
            auto made = std::move(*batch[i]);
            if (!made.has_value()) {
                out.skipped.push_back(SkippedTexture{id, std::move(made).error()});
                continue;
            }
            // UTA-0040 SS 4.3: a masked variant is a cut-out, which parallax would
            // move off its geometry, so it carries no depth.
            out.recordIndex.emplace(id, static_cast<std::uint32_t>(out.records.size()));
            out.records.push_back(ubundle::MaterialRecord{made->material.id, made->material.metallic,
                                                          variant.masked ? std::uint8_t{0} : made->material.parallaxDepth,
                                                          made->flame, made->liquid, std::move(made->fire)});
            if (!made->liquidSkipped.empty())
                out.skippedLiquids.push_back(SkippedTexture{id, std::move(made->liquidSkipped)});
            if (!made->fireSkipped.empty())
                out.skippedFires.push_back(SkippedTexture{id, std::move(made->fireSkipped)});
            out.albedo.emplace(id, made->albedo.value_or(Rgb{DEFAULT_ALBEDO, DEFAULT_ALBEDO, DEFAULT_ALBEDO}));
            if (made->emission.has_value()) out.emission.emplace(id, *made->emission);
            for (ubundle::CompressedTexture& texture : made->material.maps)
                out.textures.push_back(std::move(texture));
            if (made->glass.has_value()) out.textures.push_back(std::move(*made->glass)); // UTA-0270
            if (made->detail.has_value()) out.textures.push_back(std::move(*made->detail)); // UTA-0275
            for (const std::int32_t raw : variant.references)
                out.bySurface.emplace(std::pair{raw, variant.masked},
                                      SurfaceMaterial{id, made->uSize, made->vSize});
        }
    }
    return out;
}

// ------------------------------------------------------------------ SS 4.7

/// A file at `path` whose first sixteen bytes ubundle::readHeader accepts.
/// Only the header is read: a whole bundle can be most of a gigabyte, and
/// the loader validates the rest anyway (SS 8).
bool isCachedBake(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::array<char, ubundle::HEADER_SIZE> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (file.gcount() != static_cast<std::streamsize>(header.size())) return false;
    return ubundle::readHeader(std::as_bytes(std::span<const char>(header))).has_value();
}

} // namespace

Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName, Install& install,
                        JobSystem& jobs) {
    return detail::bake(map, mapName, install.resolver(), jobs, &umat::curated,
                        umat::TEXTURE_BUDGET_BYTES);
}

namespace detail {

// ------------------------------------------------------------ SS 4.5 steps 1-2

Result<const upkg::ExportEntry*> findLevel(const upkg::Package& map, std::string_view mapName) {
    const upkg::ExportEntry* found = nullptr;
    std::size_t count = 0;
    for (const upkg::ExportEntry& entry : map.exports()) {
        if (classOf(map, entry) != "level") continue;
        if (found == nullptr) found = &entry;
        ++count;
    }
    if (count == 0) return std::unexpected(malformed(mapName, "the map has no Level export"));
    if (count > 1)
        return std::unexpected(malformed(mapName, "the map has " + std::to_string(count)
                                                      + " Level exports, and a bake reads one"));
    return found;
}

Result<const upkg::ExportEntry*> findModel(const upkg::Package& map, const upkg::Level& level,
                                           std::string_view mapName) {
    // `Level::model` comes out of the export's DATA, which Package::open did
    // not validate, so its range is checked here rather than trusted.
    if (level.model.kind() != upkg::ObjectReferenceKind::Export
        || level.model.index() >= map.exports().size())
        return std::unexpected(malformed(mapName, "the level names no Model export of the map"));
    const upkg::ExportEntry& entry = map.exports()[level.model.index()];
    if (classOf(map, entry) != "model")
        return std::unexpected(malformed(
            mapName, "the level's Model reference names an export of class '"
                         + std::string(map.objectName(entry.objectClass).value_or("?"))
                         + "', not Model"));
    return &entry;
}

double textureScale(const upkg::Package& holder, std::span<const upkg::Property> properties) {
    for (const upkg::Property& property : properties) {
        // Qualified: inside `detail`, a bare nameOf finds Name.h's
        // detail::nameOf(NameInputs) and stops looking.
        if (fold(ubake::nameOf(holder, property.nameIndex)) != "drawscale") continue;
        const auto* const value = std::get_if<float>(&property.value);
        return value != nullptr && std::isfinite(*value) && *value > 0 ? *value : 1.0;
    }
    return 1.0;
}

Result<TextureExport> resolveTexture(const upkg::Package& map, std::string_view mapName,
                                     upkg::ObjectReference reference,
                                     const upkg::PackageResolver& resolver) {
    UTA_TRY(const TextureSite site, siteOf(map, mapName, reference, resolver));
    return TextureExport{site.holder, site.entry};
}

Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                        const upkg::PackageResolver& resolver, JobSystem& jobs,
                        const CuratedLookup& curated, std::uint64_t budgetBytes,
                        TextureCache* textureCache, const FlameLookup& isFlame,
                        const urecipe::Recipe* recipe) {
    // UTA-0129: each step below is a phase, in the order docs/specs/
    // UTA-0129-benchmark-tool.md SS 4.2 lists. No phase is opened inside a job.
    PhaseTimes times;
    std::optional<PhaseScope> phase(std::in_place, &times, "level");

    // 1. The level, and 2. its world.
    UTA_TRY(const upkg::ExportEntry* const levelExport, findLevel(map, mapName));
    UTA_TRY(const upkg::Level level, naming(upkg::readLevel(map, *levelExport), mapName));
    UTA_TRY(const upkg::ExportEntry* const modelExport, findModel(map, level, mapName));
    UTA_TRY(const upkg::Model model, naming(upkg::readModel(map, *modelExport), mapName));

    // 3. ROOM, with default options; the report rides along.
    phase.emplace(&times, "rooms");
    UTA_TRY(umap::RoomBuildResult rooms, naming(umap::buildRoomMap(model), mapName));

    // 4. NAVG and WIRG.
    phase.emplace(&times, "nav");
    UTA_TRY(unav::NavGraph nav, naming(unav::buildNavGraph(map, level, resolver), mapName));
    phase.emplace(&times, "wiring");
    UTA_TRY(unav::WiringGraph wiring, naming(unav::buildWiringGraph(map), mapName));

    // 5. PLAC and LITE -- UTA-0110 SS 4.7, moved ahead of the materials by
    // UTA-0119 SS 4.6: the movers are found from them.
    phase.emplace(&times, "actors");
    UTA_TRY(Actors actors, naming(buildActors(map, mapName, level, resolver), mapName));
    // UTA-0156 SS 4.3: each zone's ambient and fog flag, from the actors step 5 placed.
    std::vector<ubundle::Zone> zones = buildZones(model, actors.placements);
    // UTA-0156 SS 4.5: the level's brightness rides on every light, ahead of
    // step 11's probes, which light the level with the same records.
    const float levelBrightness = levelBrightnessOf(actors.placements);
    for (ubundle::Light& light : actors.lights) light.levelBrightness = levelBrightness;

    // 6. The movers, and each one's Model -- UTA-0119 SS 4.6. A Model that
    // does not read keeps its refusal's own code, naming the actor (INV-9).
    phase.emplace(&times, "movers");
    UTA_TRY(const std::vector<MoverSite> movers, naming(findMovers(map, actors.placements), mapName));
    std::vector<upkg::Model> moverModels;
    moverModels.reserve(movers.size());
    for (const MoverSite& mover : movers) {
        auto read = upkg::readModel(map, *mover.model);
        if (!read.has_value())
            return std::unexpected(
                read.error()
                    .withContext("reading mover " + actors.placements.actors[mover.placement].path)
                    .withContext(prefixFor(mapName)));
        moverModels.push_back(std::move(*read));
    }

    // 7. TEXS and MATS, over the level's Model and every mover's.
    phase.emplace(&times, "materials");
    std::vector<const upkg::Model*> surfaced{&model};
    for (const upkg::Model& moverModel : moverModels) surfaced.push_back(&moverModel);
    UTA_TRY(Materials materials,
            bakeMaterials(map, mapName, surfaced, resolver, jobs, curated, textureCache, isFlame, recipe));

    // 8. GEOM, each surface wearing the variant step 7 made for it --
    // UTA-0109 SS 4.4.
    phase.emplace(&times, "geometry");
    const MaterialLookup lookup = [&materials](upkg::ObjectReference texture,
                                               bool masked) -> const SurfaceMaterial* {
        const auto found = materials.bySurface.find({texture.raw(), masked});
        return found == materials.bySurface.end() ? nullptr : &found->second;
    };

    // UTA-0263 SS 4.3: the level's flame sheets become FLAM records, and so
    // leave GEOM. Movers' surfaces are never sheets. Timed as geometry.
    const FlameMaterialLookup flameMaterial = [&](upkg::ObjectReference texture,
                                                  bool masked) -> std::optional<std::uint32_t> {
        const SurfaceMaterial* const made = lookup(texture, masked);
        if (made == nullptr) return std::nullopt;
        const auto index = materials.recordIndex.find(made->id);
        if (index == materials.recordIndex.end() || !materials.records[index->second].flame.has_value())
            return std::nullopt;
        return index->second;
    };
    FlameSheets flames = findFlameSheets(model, flameMaterial);
    UTA_TRY(ubundle::Geometry geometry,
            naming(buildGeometry(model, lookup, zones.size(), flames.surfaces), mapName));

    // UTA-0162 SS 4.2: rows of lights become strips here, before step 11's
    // probes gather them, so the probes and LITE see the same strips. After
    // step 8, because rule 5 asks the drawn surfaces whether a member sees
    // the next (UTA-0255).
    phase.emplace(&times, "strips");
    {
        const SurfaceRays surfaces(geometry);
        markStrips(actors.lights,
                   [&surfaces](const Vec3& from, const Vec3& to) { return surfaces.blocked(from, to); });
    }
    // UTA-0263 SS 4.5: after the strips, so no flame names an absorbed light.
    assignFlameLights(flames.flames, actors.lights);

    // 9. MOVR, in export order -- UTA-0119 SS 4.5.
    phase.emplace(&times, "mover-shapes");
    std::vector<ubundle::MoverShape> shapes;
    shapes.reserve(movers.size());
    for (std::size_t i = 0; i < movers.size(); ++i) {
        UTA_TRY(ubundle::MoverShape shape,
                naming(buildMover(movers[i], moverModels[i], actors.placements, lookup), mapName));
        // UTA-0156 SS 4.3: a mover takes the zone at its placed location.
        const std::uint8_t zone = ubundle::zoneAt(rooms.map, shape.location, zones.size());
        for (ubundle::GeometryVertex& vertex : shape.geometry.vertices) vertex.zone = zone;
        shapes.push_back(std::move(shape));
    }

    // 10. COLL: the level's tree, then each mover's, in export order, from
    // the Model step 6 read -- UTA-0111 SS 4.6.
    phase.emplace(&times, "collision");
    ubundle::Collision collision;
    UTA_TRY(collision.level, naming(buildCollision(model), mapName));
    collision.movers.reserve(movers.size());
    for (std::size_t i = 0; i < movers.size(); ++i) {
        UTA_TRY(ubundle::MoverCollision tree,
                naming(buildMoverCollision(movers[i], moverModels[i], actors.placements), mapName));
        collision.movers.push_back(std::move(tree));
    }

    // 11. LPRB -- UTA-0112 SS 4.8: one bounce of the static lights, gathered at
    // lattice points near the level's surfaces. A batch with no material
    // reflects DEFAULT_ALBEDO, as a material with no opaque pixel does (SS 4.5).
    phase.emplace(&times, "light-probes");
    const AlbedoLookup albedo = [&materials](std::string_view id) {
        const auto found = materials.albedo.find(id);
        return found == materials.albedo.end() ? Rgb{DEFAULT_ALBEDO, DEFAULT_ALBEDO, DEFAULT_ALBEDO}
                                               : found->second;
    };
    // UTA-0161: a glowing material adds its mean emission to what it sends
    // on, and a liquid drawn PF_Unlit sends on its picture.
    const OwnLightLookup own = [&materials](std::string_view id) {
        OwnLight out;
        if (const auto found = materials.emission.find(id); found != materials.emission.end())
            out.emission = found->second;
        if (const auto index = materials.recordIndex.find(id); index != materials.recordIndex.end())
            out.unlitGlows = materials.records[index->second].liquid.has_value();
        return out;
    };
    UTA_TRY(ubundle::LightProbes probes,
            naming(bakeLightProbes(geometry, collision.level,
                                   bakedLights(actors.lights, actors.placements), albedo, jobs,
                                   probeReachOf(actors.placements), own),
                   mapName));

    // 11b. AOCC -- UTA-0164 SS 4.4: how enclosed each texel of each lit
    // surface is. A level with no GEOM has no AOCC.
    phase.reset();
    std::optional<ubundle::Occlusion> occlusion;
    if (!geometry.vertices.empty()) {
        phase.emplace(&times, "occlusion");
        UTA_TRY(ubundle::Occlusion baked, naming(bakeOcclusion(geometry, jobs), mapName));
        occlusion = std::move(baked);
    }

    BakeResult result;
    // 12. The budget, over every map of every material.
    phase.emplace(&times, "budget");
    result.budget = umat::measure(materials.textures, budgetBytes);
    phase.reset();
    result.phases = times.phases();
    result.rooms = std::move(rooms.report);
    result.skipped = std::move(materials.skipped);
    result.skippedFlames = std::move(flames.skipped);
    result.skippedLiquids = std::move(materials.skippedLiquids);
    result.skippedFires = std::move(materials.skippedFires);
    result.recipeUnused = std::move(materials.recipeUnused);

    // Every section is written, and empty where the level has none, so a
    // present but empty section says the level was examined (UTA-0008 SS 4.4).
    // Derived: a bake read an install (docs/design.md rule 15).
    result.bundle.header.origin = ubundle::Origin::Derived;
    result.bundle.header.kind = ubundle::BundleKind::Map;
    result.bundle.rooms = std::move(rooms.map);
    result.bundle.nav = std::move(nav);
    result.bundle.wiring = std::move(wiring);
    result.bundle.textures = std::move(materials.textures);
    result.bundle.materials = std::move(materials.records);
    result.bundle.geometry = std::move(geometry);
    result.bundle.placements = std::move(actors.placements);
    result.bundle.lights = std::move(actors.lights);
    result.bundle.movers = std::move(shapes);
    result.bundle.collision = std::move(collision);
    result.bundle.lightProbes = std::move(probes);
    result.bundle.zones = std::move(zones);
    result.bundle.occlusion = std::move(occlusion);
    result.bundle.flames = std::move(flames.flames);
    return result;
}

} // namespace detail

namespace {

/// The outermost package an import lives in, and whether the import IS that
/// package (its outer is null). Empty when the outer chain does not end.
std::pair<std::string, bool> homeOf(const upkg::Package& package, const upkg::ImportEntry& import) {
    const upkg::ImportEntry* at = &import;
    for (std::size_t depth = 0; depth < 64; ++depth) {
        if (at->outer.kind() == upkg::ObjectReferenceKind::Null) {
            const auto name = package.name(at->objectName);
            return {name.has_value() ? detail::fold(*name) : std::string{}, at == &import};
        }
        if (at->outer.kind() != upkg::ObjectReferenceKind::Import || at->outer.index() >= package.imports().size())
            return {};
        at = &package.imports()[at->outer.index()];
    }
    return {};
}

/// UTA-0141: the clashes that change what `readers` get, over the names in
/// `closure`. A shadowed file is opened here only, never through the resolver.
Result<std::vector<PackageClash>> clashesOf(const std::vector<const upkg::Package*>& readers,
                                            const std::vector<std::string>& closure, Install& install) {
    std::vector<PackageClash> clashes;
    const upkg::PackageResolver resolver = install.resolver();
    for (const std::string& package : closure) {
        const std::vector<std::filesystem::path> shadowed = install.shadowedFiles(package);
        if (shadowed.empty()) continue;
        UTA_TRY(const upkg::Package* const winner, resolver(package));
        // Reserved up front: each Package views its bytes, which must not move.
        std::vector<std::vector<std::byte>> bytes;
        std::vector<std::pair<std::filesystem::path, upkg::Package>> losers;
        // A shadowed file that does not read or open may hold what the winner
        // lacks, so it is named with the holders rather than dropped unseen.
        std::vector<std::filesystem::path> unreadable;
        bytes.reserve(shadowed.size());
        for (const std::filesystem::path& file : shadowed) {
            auto read = uta::fs::readFile(file);
            if (!read.has_value()) {
                unreadable.push_back(file);
                continue;
            }
            bytes.push_back(std::move(*read));
            if (auto opened = upkg::Package::open(bytes.back()); opened.has_value())
                losers.emplace_back(file, std::move(*opened));
            else
                unreadable.push_back(file);
        }
        std::optional<std::string> object;
        std::vector<std::filesystem::path> holders;
        for (const upkg::Package* reader : readers) {
            for (const upkg::ImportEntry& import : reader->imports()) {
                const auto [home, isPackage] = homeOf(*reader, import);
                if (isPackage || home != package) continue;
                // The whole group chain and the class, as the texture step
                // resolves an import: a same-named object in another group is
                // a different object.
                // A member of a class -- a property, a function -- is skipped:
                // an engine release may reshape its own classes' members, and
                // the game adapts old maps to that itself. UT 469's Engine.u
                // made Actor.Touching an ArrayProperty; maps built for 436
                // import it as an ObjectProperty and load fine.
                if (import.outer.kind() == upkg::ObjectReferenceKind::Import
                    && import.outer.index() < reader->imports().size()) {
                    const upkg::ImportEntry& outer = reader->imports()[import.outer.index()];
                    const auto outerClass = reader->name(outer.className);
                    if (outerClass.has_value() && detail::fold(*outerClass) == "class") continue;
                }
                const ImportedObject imported = walkImport(*reader, import);
                if (!imported.broken.empty() || imported.names.empty()) continue;
                if (winner != nullptr && exportNamed(*winner, imported) != nullptr) continue;
                for (const auto& [file, loser] : losers)
                    if (exportNamed(loser, imported) != nullptr) holders.push_back(file);
                holders.insert(holders.end(), unreadable.begin(), unreadable.end());
                if (!holders.empty()) {
                    object = package + "." + imported.path();
                    break;
                }
            }
            if (object.has_value()) break;
        }
        if (object.has_value()) clashes.push_back({package, *object, install.pathOf(package), holders});
    }
    return clashes;
}

} // namespace

Result<BakeOutcome> bakeToDirectory(const BakeRequest& request, JobSystem& jobs) {
    // UTA-0129: each step is a phase, and an outcome carries the ones that ran.
    PhaseTimes times;
    std::optional<PhaseScope> phase(std::in_place, &times, "open-install");
    UTA_TRY(Install install, Install::open(request.install));
    phase.emplace(&times, "read-map");
    UTA_TRY(const std::vector<std::byte> mapBytes, uta::fs::readFile(request.map));
    const std::string mapName = detail::mapNameOf(request.map);
    // UTA-0113 SS 4.3: the recipe, before the name, which covers it.
    UTA_TRY(const std::optional<urecipe::Found> recipe,
            urecipe::find(request.recipes, mapName, sha256(mapBytes)));
    const urecipe::Recipe* const recipeIn = recipe.has_value() ? &recipe->recipe : nullptr;

    // 1. The name, before anything is baked -- it is what finds a cached one.
    phase.emplace(&times, "name");
    BakeOutcome outcome;
    UTA_TRY(outcome.name, detail::bakeName(mapBytes, mapName, install, recipeIn));
    if (recipe.has_value()) outcome.recipe = recipe->path;
    outcome.path = request.outDir / (outcome.name + ".utab");
    phase.emplace(&times, "closure");
    UTA_TRY(const upkg::Package map, naming(upkg::Package::open(mapBytes), mapName));
    UTA_TRY(const std::vector<std::string> imported, detail::closure(map, install.resolver()));
    std::vector<const upkg::Package*> readers{&map};
    const upkg::PackageResolver resolver = install.resolver();
    for (const std::string& package : imported) {
        UTA_TRY(const upkg::Package* const opened, resolver(package));
        if (opened != nullptr) readers.push_back(opened);
    }
    phase.emplace(&times, "clashes");
    UTA_TRY(outcome.clashes, clashesOf(readers, imported, install));
    phase.reset();
    const auto finished = [&] {
        phase.reset();
        outcome.phases = times.phases();
    };

    std::error_code ec;
    std::filesystem::create_directories(request.outDir, ec);
    if (ec)
        return fail(ErrorCode::IoFailure,
                    "cannot create " + detail::utf8(request.outDir) + ": " + ec.message());

    // 2. A cached bake is the outcome unless forced. A file there that
    // readHeader refuses is baked over.
    if (!request.force && isCachedBake(outcome.path)) {
        outcome.verdict = Verdict::Cached;
        finished();
        return outcome;
    }
    // UTA-0245: one asking to fit takes a fitted bake already there too.
    const std::string fitted = detail::fittedName(outcome.name);
    const std::filesystem::path fittedPath = request.outDir / (fitted + ".utab");
    if (request.fitBudget && !request.force && isCachedBake(fittedPath)) {
        outcome.verdict = Verdict::Cached;
        outcome.name = fitted;
        outcome.path = fittedPath;
        finished();
        return outcome;
    }

    // 3. Bake, then the budget. Over it, nothing is written -- UTA-0052's
    // never-degrade rule.
    phase.emplace(&times, "bake");
    std::optional<TextureCache> textureCache;
    if (!request.textureCache.empty()) textureCache.emplace(request.textureCache);
    UTA_TRY(BakeResult result, detail::bake(map, mapName, install.resolver(), jobs, &umat::curated,
                                            request.budgetBytes,
                                            textureCache ? &*textureCache : nullptr, umat::isFlame,
                                            recipeIn));
    times.adopt(result.phases);
    if (textureCache) {
        result.textureCacheHits = textureCache->hits();
        result.textureCacheMisses = textureCache->misses();
        textureCache->trim();
    }
    phase.reset();
    if (!umat::enforceBudget(result.budget).has_value()) {
        // UTA-0245: asked to, shrink until it fits, under its own name.
        if (request.fitBudget && result.bundle.textures) {
            phase.emplace(&times, "fit-budget");
            outcome.fitted = umat::fitToBudget(*result.bundle.textures, request.budgetBytes);
            result.budget = umat::measure(*result.bundle.textures, request.budgetBytes);
            phase.reset();
        }
        if (!outcome.fitted || !outcome.fitted->fits) {
            outcome.verdict = Verdict::OverBudget;
            outcome.result = std::move(result);
            finished();
            return outcome;
        }
        outcome.name = fitted;
        outcome.path = fittedPath;
    }

    // 4. Written atomically: a crash leaves the old file or none (SS 6).
    phase.emplace(&times, "encode");
    UTA_TRY(const std::vector<std::byte> bytes, ubundle::write(result.bundle));
    phase.emplace(&times, "write-file");
    UTA_CHECK(uta::fs::writeFileAtomically(outcome.path, bytes));
    outcome.verdict = Verdict::Written;
    outcome.result = std::move(result);
    finished();
    return outcome;
}

} // namespace uta::ubake
