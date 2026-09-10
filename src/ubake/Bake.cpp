// One bake and writing it -- docs/specs/UTA-0011-map-baker.md SS 4.5 to SS 4.7.

#include "ubake/Bake.h"

#include "core/FileSystem.h"
#include "ubake/Name.h"
#include "umat/Fingerprint.h"
#include "umat/Generate.h"
#include "umat/Resolve.h"
#include "unav/Build.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"

#include <array>
#include <expected>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

namespace uta::ubake {
namespace {

/// A surface whose index-0 texels are see-through -- UT99's PF_Masked. Source:
/// https://wiki.beyondunreal.com/Legacy:PolyFlags.
constexpr std::uint32_t PF_MASKED = 0x00000002u;

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

/// SS 4.6 "Which textures": an export reference is that export of the map; an
/// import reference resolves its outermost outer through the resolver, then
/// takes the export of that package whose name and outer names match.
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

    // An import: walk to the root of its outer chain, collecting names on the
    // way. The chain is bounded by the import table.
    const std::span<const upkg::ImportEntry> imports = map.imports();
    if (reference.index() >= imports.size()) {
        site.package = std::string(mapName);
        site.path = "import" + std::to_string(reference.index());
        site.unresolved = "the surface names import " + std::to_string(reference.index())
                          + ", past the map's import table";
        return site;
    }
    std::vector<std::string> names; // the texture first
    const upkg::ImportEntry* current = &imports[reference.index()];
    std::string broken;
    for (std::size_t hops = 0;; ++hops) {
        if (current->outer.kind() == upkg::ObjectReferenceKind::Null) {
            site.package = nameOf(map, current->objectName);
            break;
        }
        names.push_back(nameOf(map, current->objectName));
        if (current->outer.kind() != upkg::ObjectReferenceKind::Import
            || current->outer.index() >= imports.size() || hops > imports.size()) {
            broken = "its import's outer chain does not end at a package";
            break;
        }
        current = &imports[current->outer.index()];
    }

    std::string path;
    for (auto name = names.rbegin(); name != names.rend(); ++name)
        path += (path.empty() ? "" : ".") + *name;
    site.path = path;

    if (!broken.empty()) {
        if (site.package.empty()) site.package = std::string(mapName);
        site.unresolved = broken;
        return site;
    }
    if (names.empty()) {
        site.path = site.package;
        site.unresolved = "the surface's texture reference names a package, not a texture";
        return site;
    }

    UTA_TRY(const upkg::Package* const holder, resolver(detail::fold(site.package)));
    if (holder == nullptr) {
        site.unresolved = "package " + site.package + " is not in the install, or does not open";
        return site;
    }
    std::vector<std::string> chain;
    for (const std::string& name : names) chain.push_back(detail::fold(name));
    for (const upkg::ExportEntry& candidate : holder->exports()) {
        if (matchesChain(*holder, candidate, chain)) {
            site.holder = holder;
            site.entry = &candidate;
            return site;
        }
    }
    site.unresolved = "package " + site.package + " holds no " + path;
    return site;
}

/// One variant, or why it cannot be made. The error arm is a SKIP and never a
/// refusal of the bake: every failure SS 4.6 lists belongs to one texture, and
/// the bake goes on without it.
std::expected<umat::Material, std::string> makeVariant(const TextureSite& site,
                                                       const std::string& id, bool masked,
                                                       JobSystem& jobs,
                                                       const detail::CuratedLookup& curated) {
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
    if (!paletteReference.has_value()
        || paletteReference->kind() == upkg::ObjectReferenceKind::Null)
        return std::unexpected(std::string("it names no palette"));
    if (paletteReference->kind() != upkg::ObjectReferenceKind::Export
        || paletteReference->index() >= holder.exports().size())
        return std::unexpected(std::string("its palette is not an export of its own package"));
    const auto palette = upkg::readPalette(holder, holder.exports()[paletteReference->index()]);
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

    // Step 4. UTA-0010 SS 4.5's order with no recipe: the defaults, then the
    // curated library's entry for this picture.
    umat::MaterialSettings settings{};
    if (const auto fingerprint = umat::pictureFingerprint(base, *palette)) {
        if (const umat::CuratedOverride* const entry = curated(*fingerprint))
            settings = umat::applied(settings, *entry);
    }

    // Step 5.
    const auto rgba = umat::resolve(base, *palette, masked);
    if (!rgba.has_value())
        return std::unexpected("umat::resolve refused it: " + std::string(rgba.error().message()));
    auto material = umat::generate(id, *rgba, settings, jobs);
    if (!material.has_value())
        return std::unexpected("umat::generate refused it: "
                               + std::string(material.error().message()));
    return std::move(*material);
}

struct Materials {
    std::vector<ubundle::CompressedTexture> textures;
    std::vector<ubundle::MaterialRecord> records;
    std::vector<SkippedTexture> skipped;
};

Result<Materials> bakeMaterials(const upkg::Package& map, std::string_view mapName,
                                const upkg::Model& model, const upkg::PackageResolver& resolver,
                                JobSystem& jobs, const detail::CuratedLookup& curated) {
    // Which variants each distinct reference needs. A map's surfaces name a
    // few hundred textures thousands of times, so each is resolved once.
    struct Needs {
        bool opaque = false;
        bool masked = false;
    };
    std::map<std::int32_t, Needs> byReference;
    for (const upkg::BspSurf& surf : model.surfs) {
        if (surf.texture.kind() == upkg::ObjectReferenceKind::Null) continue;
        Needs& needs = byReference[surf.texture.raw()];
        // The SURFACE decides, not the texture's own bMasked: surfaces sharing
        // one texture disagree about index 0 (UTA-0009 SS 2 item 4).
        if ((surf.polyFlags & PF_MASKED) != 0)
            needs.masked = true;
        else
            needs.opaque = true;
    }

    struct Variant {
        const TextureSite* site = nullptr;
        bool masked = false;
    };
    std::vector<TextureSite> sites;
    sites.reserve(byReference.size());
    std::map<std::string, Variant> variants; // by material id: ascending bytewise
    for (const auto& [raw, needs] : byReference) {
        UTA_TRY(TextureSite site, siteOf(map, mapName, upkg::ObjectReference{raw}, resolver));
        sites.push_back(std::move(site));
        const TextureSite* const placed = &sites.back();
        if (needs.opaque)
            variants.try_emplace(umat::materialId(placed->package, placed->path, false),
                                 Variant{placed, false});
        if (needs.masked)
            variants.try_emplace(umat::materialId(placed->package, placed->path, true),
                                 Variant{placed, true});
    }

    // One variant at a time, in id order: generate() spreads its own work
    // over the job system, and taking the results in this order is what keeps
    // TEXS independent of which job finishes first (INV-1).
    Materials out;
    for (const auto& [id, variant] : variants) {
        auto made = makeVariant(*variant.site, id, variant.masked, jobs, curated);
        if (!made.has_value()) {
            out.skipped.push_back(SkippedTexture{id, std::move(made).error()});
            continue;
        }
        out.records.push_back(ubundle::MaterialRecord{made->id, made->metallic});
        for (ubundle::CompressedTexture& map : made->maps) out.textures.push_back(std::move(map));
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

Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                        const upkg::PackageResolver& resolver, JobSystem& jobs,
                        const CuratedLookup& curated, std::uint64_t budgetBytes) {
    // 1. The level, and 2. its world.
    UTA_TRY(const upkg::ExportEntry* const levelExport, findLevel(map, mapName));
    UTA_TRY(const upkg::Level level, naming(upkg::readLevel(map, *levelExport), mapName));
    UTA_TRY(const upkg::ExportEntry* const modelExport, findModel(map, level, mapName));
    UTA_TRY(const upkg::Model model, naming(upkg::readModel(map, *modelExport), mapName));

    // 3. ROOM, with default options; the report rides along.
    UTA_TRY(umap::RoomBuildResult rooms, naming(umap::buildRoomMap(model), mapName));

    // 4. NAVG and WIRG.
    UTA_TRY(unav::NavGraph nav, naming(unav::buildNavGraph(map, level, resolver), mapName));
    UTA_TRY(unav::WiringGraph wiring, naming(unav::buildWiringGraph(map), mapName));

    // 5. TEXS and MATS.
    UTA_TRY(Materials materials, bakeMaterials(map, mapName, model, resolver, jobs, curated));

    BakeResult result;
    // 6. The budget, over every map of every material.
    result.budget = umat::measure(materials.textures, budgetBytes);
    result.rooms = std::move(rooms.report);
    result.skipped = std::move(materials.skipped);

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
    return result;
}

} // namespace detail

Result<BakeOutcome> bakeToDirectory(const BakeRequest& request, JobSystem& jobs) {
    UTA_TRY(Install install, Install::open(request.install));
    UTA_TRY(const std::vector<std::byte> mapBytes, uta::fs::readFile(request.map));
    const std::string mapName = detail::mapNameOf(request.map);

    // 1. The name, before anything is baked -- it is what finds a cached one.
    BakeOutcome outcome;
    UTA_TRY(outcome.name, detail::bakeName(mapBytes, mapName, install));
    outcome.path = request.outDir / (outcome.name + ".utab");

    std::error_code ec;
    std::filesystem::create_directories(request.outDir, ec);
    if (ec)
        return fail(ErrorCode::IoFailure,
                    "cannot create " + detail::utf8(request.outDir) + ": " + ec.message());

    // 2. A cached bake is the outcome unless forced. A file there that
    // readHeader refuses is baked over.
    if (!request.force && isCachedBake(outcome.path)) {
        outcome.verdict = Verdict::Cached;
        return outcome;
    }

    // 3. Bake, then the budget. Over it, nothing is written -- UTA-0052's
    // never-degrade rule.
    UTA_TRY(const upkg::Package map, naming(upkg::Package::open(mapBytes), mapName));
    UTA_TRY(BakeResult result, detail::bake(map, mapName, install.resolver(), jobs,
                                            &umat::curated, request.budgetBytes));
    if (!umat::enforceBudget(result.budget).has_value()) {
        outcome.verdict = Verdict::OverBudget;
        outcome.result = std::move(result);
        return outcome;
    }

    // 4. Written atomically: a crash leaves the old file or none (SS 6).
    UTA_TRY(const std::vector<std::byte> bytes, ubundle::write(result.bundle));
    UTA_CHECK(uta::fs::writeFileAtomically(outcome.path, bytes));
    outcome.verdict = Verdict::Written;
    outcome.result = std::move(result);
    return outcome;
}

} // namespace uta::ubake
