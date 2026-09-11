// UTA-0119's real-asset case: findMovers and buildMover over every map in the
// install, and the FCoords port placed against the level's own points.
//
// docs/specs/UTA-0119-mover-shapes.md SS 7. It PRINTS what the spec's SS 2 and
// SS 3 rest on, so those are an output of the suite rather than a
// transcription: movers baked; maps refused, by reason; movers carrying a
// shear; mover Models with no BSP nodes, whose shape comes out empty; mover
// Models whose points sit nearer the origin than the actor; and surfaces only
// a mover wears. It also places every static brush's Polys corners with
// tests/support/FCoordsPort.h and prints the share landing on a level point,
// by transform property, with GMath's table and with exact sine and cosine --
// which is what shows the port is the engine's.
//
// NO MATERIAL IS GENERATED: the lookup gives every texture one stand-in, as
// tests/real/RealGeometryTest.cpp's reason has it -- a census is not a bake.
//
// ONE Install PER MAP -- UTA-0011 SS 4.11.

#include "core/FileSystem.h"
#include "support/FCoordsPort.h"
#include "ubake/Actors.h"
#include "ubake/Install.h"
#include "ubake/Movers.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace {

namespace fs = std::filesystem;
namespace detail = uta::ubake::detail;
namespace fc = uta::test::fcoords;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;
using uta::upkg::ObjectReference;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;

struct Share {
    std::size_t brushes = 0, corners = 0;
    std::array<std::size_t, 2> landed{}; // [table, exact], within 0.1
};

struct Totals {
    std::size_t maps = 0, built = 0, movers = 0, emptyShapes = 0, sheared = 0;
    std::size_t nearOrigin = 0, nearActor = 0;
    std::size_t moverSurfaces = 0, moverOnlySurfaces = 0;
    double seconds = 0;
    std::map<std::string, std::size_t> refused;
    std::vector<std::string> refusals;
    std::map<std::string, Share> shares;
};

const uta::ubake::MaterialLookup STAND_IN = [](ObjectReference, bool) -> const uta::ubake::SurfaceMaterial* {
    static const uta::ubake::SurfaceMaterial made{"stand-in", 64.0, 64.0};
    return &made;
};

std::string classOf(const Package& package, const uta::upkg::ExportEntry& entry) {
    if (entry.objectClass.kind() == ObjectReferenceKind::Null) return {};
    return detail::fold(std::string(package.objectName(entry.objectClass).value_or("")));
}

/// A Scale record's three components, SheerRate and SheerAxis.
std::optional<fc::Scale> scaleOf(const PropertyRecord& record) {
    const auto* raw = std::get_if<uta::ubundle::RawValue>(&record.value);
    if (raw == nullptr || detail::fold(raw->structName) != "scale" || raw->bytes.size() != 17)
        return std::nullopt;
    const auto f32 = [&raw](std::size_t at) {
        std::uint32_t bits = 0;
        for (std::size_t b = 0; b < 4; ++b)
            bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(raw->bytes[at + b])) << (8U * b);
        return static_cast<double>(std::bit_cast<float>(bits));
    };
    return fc::Scale{fc::Vec{f32(0), f32(4), f32(8)}, f32(12), std::to_integer<int>(raw->bytes[16])};
}

/// The transform numbers an actor resolves to, as SS 4.4 resolves them, but
/// with the shear kept -- the census asks what the engine would have done.
struct Numbers {
    fc::Vec location, prePivot;
    fc::Rotator rotation;
    fc::Scale mainScale, postScale;
};

Numbers numbersOf(const uta::ubundle::ActorPlacement& actor, const uta::ubundle::ActorClass& actorClass) {
    const auto find = [&](std::string_view name, ValueKind kind) {
        return detail::resolvedRecord(name, actor.properties, actorClass.defaults,
                                      [kind](const PropertyRecord& r) { return r.kind == kind; });
    };
    const auto scale = [&](std::string_view name) {
        const PropertyRecord* r = detail::resolvedRecord(
            name, actor.properties, actorClass.defaults,
            [](const PropertyRecord& c) { return scaleOf(c).has_value(); });
        return r == nullptr ? fc::Scale{} : *scaleOf(*r);
    };
    const auto vec = [&](std::string_view name) {
        const PropertyRecord* r = find(name, ValueKind::Vector);
        if (r == nullptr) return fc::Vec{};
        const auto& v = std::get<std::array<float, 3>>(r->value);
        return fc::Vec{v[0], v[1], v[2]};
    };
    Numbers n;
    n.location = vec("location");
    n.prePivot = vec("prepivot");
    if (const PropertyRecord* r = find("rotation", ValueKind::Rotator)) {
        const auto& v = std::get<std::array<std::int32_t, 3>>(r->value);
        n.rotation = fc::Rotator{v[0], v[1], v[2]};
    }
    n.mainScale = scale("mainscale");
    n.postScale = scale("postscale");
    return n;
}

/// Level points on a grid, for a nearest-point test.
struct Grid {
    std::unordered_map<long long, std::vector<fc::Vec>> cells;
    static long long key(long long i, long long j, long long k) {
        return (i * 73856093LL) ^ (j * 19349663LL) ^ (k * 83492791LL);
    }
    void add(fc::Vec p) { cells[key(std::llround(p.x), std::llround(p.y), std::llround(p.z))].push_back(p); }
    bool near(fc::Vec p) const {
        const long long i = std::llround(p.x), j = std::llround(p.y), k = std::llround(p.z);
        for (long long a = -1; a <= 1; ++a)
            for (long long b = -1; b <= 1; ++b)
                for (long long c = -1; c <= 1; ++c) {
                    const auto found = cells.find(key(i + a, j + b, k + c));
                    if (found == cells.end()) continue;
                    for (const fc::Vec& q : found->second) {
                        const fc::Vec d = p - q;
                        if (fc::dot(d, d) <= 0.01) return true;
                    }
                }
        return false;
    }
};

std::optional<ObjectReference> ownBrush(const Package& map, const uta::upkg::ExportEntry& entry) {
    const auto properties = uta::upkg::readProperties(map, entry);
    if (!properties.has_value()) return std::nullopt;
    for (const uta::upkg::Property& property : *properties) {
        const auto* reference = std::get_if<ObjectReference>(&property.value);
        if (reference == nullptr) continue;
        if (detail::fold(std::string(map.name(property.nameIndex).value_or(""))) == "brush") return *reference;
    }
    return std::nullopt;
}

void tallyShare(Totals& totals, const std::string& bucket, std::size_t corners,
                const std::array<std::size_t, 2>& landed) {
    Share& share = totals.shares[bucket];
    ++share.brushes;
    share.corners += corners;
    share.landed[0] += landed[0];
    share.landed[1] += landed[1];
}

void censusOf(const fs::path& root, const fs::path& mapPath, Totals& totals) {
    ++totals.maps;
    const std::string mapName = detail::mapNameOf(mapPath);
    const auto bytes = uta::fs::readFile(mapPath);
    if (!bytes.has_value()) return void(++totals.refused["the map does not read"]);
    const auto map = Package::open(*bytes);
    if (!map.has_value()) return void(++totals.refused["the map does not open"]);
    const auto exports = map->exports();

    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& entry : exports)
        if (classOf(*map, entry) == "level") levelExport = &entry;
    if (levelExport == nullptr) return void(++totals.refused["no Level export"]);
    const auto level = uta::upkg::readLevel(*map, *levelExport);
    if (!level.has_value() || level->model.kind() != ObjectReferenceKind::Export
        || level->model.index() >= exports.size())
        return void(++totals.refused["no level Model"]);
    const auto levelModel = uta::upkg::readModel(*map, exports[level->model.index()]);
    if (!levelModel.has_value()) return void(++totals.refused["the level's Model refuses"]);

    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());
    const auto started = std::chrono::steady_clock::now();
    const auto actors = uta::ubake::buildActors(*map, mapName, *level, install->resolver());
    if (!actors.has_value()) return void(++totals.refused["buildActors refuses"]);
    const auto movers = uta::ubake::findMovers(*map, actors->placements);
    if (!movers.has_value()) {
        ++totals.refused["findMovers refuses"];
        if (totals.refusals.size() < 5) totals.refusals.push_back(mapName + ": " + std::string(movers.error().message()));
        return;
    }

    std::set<std::int32_t> levelTextures;
    for (const auto& surf : levelModel->surfs) levelTextures.insert(surf.texture.raw());

    for (const uta::ubake::MoverSite& site : *movers) {
        const auto& actor = actors->placements.actors[site.placement];
        const auto& actorClass = actors->placements.classes[actor.classIndex];
        const auto model = uta::upkg::readModel(*map, *site.model);
        if (!model.has_value()) {
            ++totals.refused["a mover's Model refuses"];
            continue;
        }
        const auto shape = uta::ubake::buildMover(site, *model, actors->placements, STAND_IN);
        if (!shape.has_value()) {
            ++totals.refused["buildMover refuses"];
            if (totals.refusals.size() < 5) totals.refusals.push_back(mapName + ": " + std::string(shape.error().message()));
            continue;
        }
        ++totals.movers;
        if (shape->geometry.vertices.empty()) ++totals.emptyShapes;

        const Numbers n = numbersOf(actor, actorClass);
        if (fc::sheerSnap(n.mainScale.sheerRate) != 0 || fc::sheerSnap(n.postScale.sheerRate) != 0)
            ++totals.sheared;
        if (!model->points.empty()) {
            fc::Vec centre;
            for (const auto& p : model->points) centre = {centre.x + p.x, centre.y + p.y, centre.z + p.z};
            const double count = static_cast<double>(model->points.size());
            centre = {centre.x / count, centre.y / count, centre.z / count};
            const fc::Vec fromActor = centre - n.location;
            if (fc::dot(n.location, n.location) > 256.0 * 256.0)
                (fc::dot(centre, centre) < fc::dot(fromActor, fromActor) ? totals.nearOrigin : totals.nearActor) += 1;
        }
        for (const auto& surf : model->surfs) {
            ++totals.moverSurfaces;
            if (!levelTextures.contains(surf.texture.raw())) ++totals.moverOnlySurfaces;
        }
    }
    totals.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    ++totals.built;

    // Static brushes: the editor merged each into the level with ToWorld, so a
    // faithful port puts their corners on the level's own points.
    Grid grid;
    for (const auto& p : levelModel->points) grid.add({p.x, p.y, p.z});
    for (const auto& actor : actors->placements.actors) {
        const auto& actorClass = actors->placements.classes[actor.classIndex];
        if (actorClass.path != "engine.brush") continue;
        const auto brush = ownBrush(*map, exports[actor.exportIndex]);
        if (!brush.has_value() || brush->kind() != ObjectReferenceKind::Export || brush->index() >= exports.size())
            continue;
        const auto model = uta::upkg::readModel(*map, exports[brush->index()]);
        if (!model.has_value() || model->polys.kind() != ObjectReferenceKind::Export
            || model->polys.index() >= exports.size())
            continue;
        const auto polys = uta::upkg::readPolys(*map, exports[model->polys.index()]);
        if (!polys.has_value()) continue;

        const Numbers n = numbersOf(actor, actorClass);
        const std::array<fc::Coords, 2> world = {
            fc::toWorld(n.location, n.rotation, n.prePivot, n.mainScale, n.postScale, fc::Trig::Table),
            fc::toWorld(n.location, n.rotation, n.prePivot, n.mainScale, n.postScale, fc::Trig::Exact)};
        std::size_t corners = 0;
        std::array<std::size_t, 2> landed{};
        for (const auto& polygon : polys->polygons)
            for (const auto& p : polygon.vertices) {
                ++corners;
                for (std::size_t m = 0; m < 2; ++m)
                    landed[m] += grid.near(fc::transformPointBy({p.x, p.y, p.z}, world[m]));
            }
        if (corners == 0) continue;

        const bool rotated = n.rotation.pitch != 0 || n.rotation.yaw != 0 || n.rotation.roll != 0;
        const auto nonUnit = [](const fc::Scale& s) { return s.scale.x != 1 || s.scale.y != 1 || s.scale.z != 1; };
        const bool sheared = fc::sheerSnap(n.mainScale.sheerRate) != 0 || fc::sheerSnap(n.postScale.sheerRate) != 0;
        const bool pivoted = n.prePivot.x != 0 || n.prePivot.y != 0 || n.prePivot.z != 0;
        tallyShare(totals, "all", corners, landed);
        if (rotated) tallyShare(totals, "rotated", corners, landed);
        if (nonUnit(n.mainScale)) tallyShare(totals, "MainScale non-unit", corners, landed);
        if (nonUnit(n.postScale)) tallyShare(totals, "PostScale non-unit", corners, landed);
        if (pivoted) tallyShare(totals, "PrePivot non-zero", corners, landed);
        if (sheared) tallyShare(totals, "sheared", corners, landed);
        if (!rotated && !nonUnit(n.mainScale) && !nonUnit(n.postScale) && !pivoted && !sheared)
            tallyShare(totals, "no transform but Location", corners, landed);
    }
}

} // namespace

TEST_CASE("every map's movers bake and the census prints", "[real-assets][movers]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    std::vector<fs::path> maps;
    for (const fs::directory_entry& entry : fs::directory_iterator(root / "Maps"))
        if (entry.is_regular_file() && detail::fold(entry.path().extension().string()) == ".unr")
            maps.push_back(entry.path());
    std::sort(maps.begin(), maps.end());
    REQUIRE_FALSE(maps.empty());

    Totals totals;
    for (const fs::path& map : maps) censusOf(root, map, totals);

    std::cout << "UTA-0119 movers census over " << totals.maps << " maps, " << totals.built << " built in "
              << totals.seconds << " s\n"
              << "  movers baked " << totals.movers << ", empty shapes (no BSP nodes) " << totals.emptyShapes
              << ", carrying a shear " << totals.sheared << "\n"
              << "  mover Models (actor over 256 out): centre nearer the origin " << totals.nearOrigin
              << ", nearer the actor " << totals.nearActor << "\n"
              << "  mover surfaces " << totals.moverSurfaces << ", wearing a texture no level surface wears "
              << totals.moverOnlySurfaces << "\n"
              << "  static brush corners on a level point, table | exact:\n";
    for (const auto& [bucket, share] : totals.shares) {
        const auto pct = [&](std::size_t n) { return share.corners ? 100.0 * n / share.corners : 0.0; };
        std::cout << "    " << bucket << ": " << share.brushes << " brushes, " << share.corners << " corners, "
                  << pct(share.landed[0]) << "% | " << pct(share.landed[1]) << "%\n";
    }
    for (const auto& [reason, count] : totals.refused) std::cout << "  refused, " << reason << ": " << count << "\n";
    for (const std::string& message : totals.refusals) std::cout << "    " << message << "\n";

    CHECK(totals.built > 0);
    CHECK(totals.movers > 0);
}
