// UTA-0109's real-asset case: buildGeometry over every map in the install.
//
// docs/specs/UTA-0109-map-geometry.md SS 7. It asserts INV-4 on every polygon
// every map emits, and PRINTS the figures SS 2 and SS 4.3 rest on, so they are
// an output of the suite rather than a transcription in the roadmap: nodes
// drawn, reversed and skipped by reason; the pan-sign seam tally; the
// DrawScale values the textures carry; and maps refused, by reason.
//
// A file of its own rather than a case in RealInstallTest.cpp, which UTA-0103
// splits by subject.
//
// ONE Install PER MAP -- UTA-0011 SS 4.11: an Install keeps every package it
// opened, and one Install across the library held every map's closure at once
// and ran the machine out of memory.
//
// NO MATERIAL IS GENERATED. The lookup resolves each texture the way the bake
// does (detail::resolveTexture) and reads only its base level's size and its
// DrawScale, which is all buildGeometry needs. Generating every material in
// the library would turn a geometry census into a full bake.

#include "core/FileSystem.h"
#include "ubake/Bake.h"
#include "ubake/Geometry.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace {

namespace fs = std::filesystem;
namespace detail = uta::ubake::detail;
using uta::ubake::SurfaceMaterial;
using uta::upkg::ObjectReference;
using uta::upkg::Package;

using Vec = std::array<double, 3>;

Vec toDouble(const uta::upkg::Vector3& v) {
    return {v.x, v.y, v.z};
}
Vec minus(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
double dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

/// A float triple by its bits, so equal points match exactly and -0.0 is not +0.0.
using PointKey = std::array<std::uint32_t, 3>;
PointKey keyOf(const uta::upkg::Vector3& v) {
    return {std::bit_cast<std::uint32_t>(v.x), std::bit_cast<std::uint32_t>(v.y),
            std::bit_cast<std::uint32_t>(v.z)};
}

constexpr std::uint32_t PF_FAKE_BACKDROP = 0x00000080u;
constexpr std::uint32_t PF_PORTAL = 0x04000000u;

struct Totals {
    std::size_t maps = 0;
    std::size_t built = 0;
    std::map<std::string, std::size_t> refused; // by reason
    std::size_t drawn = 0;
    std::size_t reversed = 0;
    std::size_t noArea = 0;
    std::size_t invisible = 0;
    std::size_t fewerThanThree = 0;
    std::size_t portal = 0;
    std::size_t fakeBackdrop = 0;
    std::size_t inv4Violations = 0;
    std::array<std::size_t, 2> onlyPlus{};  // U, V
    std::array<std::size_t, 2> onlyMinus{}; // U, V
    std::size_t texturesResolved = 0;       // per map that names them
    std::size_t withDrawScale = 0;          // per map that names them
    float minDrawScale = 0;
    float maxDrawScale = 0;
    double buildSeconds = 0;
};

/// What the lookup found for one texture reference in one map.
struct TextureFacts {
    SurfaceMaterial material;
    std::optional<float> drawScale; ///< the property's value, where it carries one
};

std::string classOf(const Package& package, const uta::upkg::ExportEntry& entry) {
    return detail::fold(std::string(package.objectName(entry.objectClass).value_or("")));
}

/// The texture a reference names: its base level's size times its scale, and
/// its DrawScale property if it carries one. Empty where it does not resolve
/// or has no picture.
std::optional<TextureFacts> factsOf(const Package& map, std::string_view mapName,
                                    ObjectReference reference,
                                    const uta::upkg::PackageResolver& resolver) {
    const auto resolved = detail::resolveTexture(map, mapName, reference, resolver);
    if (!resolved.has_value() || resolved->holder == nullptr) return std::nullopt;
    const Package& holder = *resolved->holder;
    const auto properties = uta::upkg::readProperties(holder, *resolved->entry);
    const auto texture = uta::upkg::readTexture(holder, *resolved->entry);
    if (!properties.has_value() || !texture.has_value() || texture->mips.empty()) return std::nullopt;
    const uta::upkg::Mip& base = texture->mips[0];
    if (base.width == 0 || base.height == 0) return std::nullopt;

    TextureFacts facts;
    const double scale = detail::textureScale(holder, *properties);
    facts.material = SurfaceMaterial{"r" + std::to_string(reference.raw()), base.width * scale,
                                     base.height * scale};
    for (const uta::upkg::Property& property : *properties) {
        if (detail::fold(std::string(holder.name(property.nameIndex).value_or(""))) != "drawscale")
            continue;
        if (const auto* value = std::get_if<float>(&property.value)) facts.drawScale = *value;
    }
    return facts;
}

/// One drawn polygon, for the seam tally: its surface and its corners.
struct Polygon {
    std::size_t surf = 0;
    std::vector<std::uint32_t> points; // indices into the Model's points
};

/// Whether `d` is a whole multiple of `size`, within a thousandth of a texel.
bool linesUp(double d, double size) {
    return std::abs(d - size * std::round(d / size)) < 0.001;
}

/// SS 7's seam tally for one map: two drawn polygons of different surfaces
/// sharing two points by value, naming one texture with equal TU, TV and N
/// and different pans. Per axis, a sign lines up where both sides' offsets
/// with that sign differ by a whole multiple of the axis's size.
void tallySeams(const uta::upkg::Model& model, const std::vector<Polygon>& polygons,
                const std::map<std::int32_t, std::optional<TextureFacts>>& textures,
                Totals& totals) {
    std::map<std::pair<PointKey, PointKey>, std::vector<std::size_t>> edges;
    for (std::size_t p = 0; p < polygons.size(); ++p) {
        const auto& corners = polygons[p].points;
        for (std::size_t k = 0; k < corners.size(); ++k) {
            PointKey a = keyOf(model.points[corners[k]]);
            PointKey b = keyOf(model.points[corners[(k + 1) % corners.size()]]);
            if (b < a) std::swap(a, b);
            edges[{a, b}].push_back(p);
        }
    }
    for (const auto& [edge, sharing] : edges) {
        for (std::size_t i = 0; i < sharing.size(); ++i) {
            for (std::size_t j = i + 1; j < sharing.size(); ++j) {
                const auto& s1 = model.surfs[polygons[sharing[i]].surf];
                const auto& s2 = model.surfs[polygons[sharing[j]].surf];
                if (polygons[sharing[i]].surf == polygons[sharing[j]].surf) continue;
                if (s1.texture.raw() != s2.texture.raw()) continue;
                const auto found = textures.find(s1.texture.raw());
                if (found == textures.end() || !found->second.has_value()) continue;
                const auto sameVector = [&](std::int32_t a, std::int32_t b) {
                    return keyOf(model.vectors[static_cast<std::size_t>(a)])
                           == keyOf(model.vectors[static_cast<std::size_t>(b)]);
                };
                if (!sameVector(s1.vTextureU, s2.vTextureU) || !sameVector(s1.vTextureV, s2.vTextureV)
                    || !sameVector(s1.vNormal, s2.vNormal))
                    continue;
                const Vec b1 = toDouble(model.points[static_cast<std::size_t>(s1.pBase)]);
                const Vec b2 = toDouble(model.points[static_cast<std::size_t>(s2.pBase)]);
                const std::array<std::pair<Vec, double>, 2> axes = {
                    std::pair{toDouble(model.vectors[static_cast<std::size_t>(s1.vTextureU)]),
                              found->second->material.uSize},
                    std::pair{toDouble(model.vectors[static_cast<std::size_t>(s1.vTextureV)]),
                              found->second->material.vSize}};
                const std::array<std::pair<double, double>, 2> pans = {
                    std::pair{double(s1.panU), double(s2.panU)},
                    std::pair{double(s1.panV), double(s2.panV)}};
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    const auto [pan1, pan2] = pans[axis];
                    if (pan1 == pan2) continue;
                    const auto& [along, size] = axes[axis];
                    bool plus = true;
                    bool minusSign = true;
                    for (const PointKey& key : {edge.first, edge.second}) {
                        const Vec point = {std::bit_cast<float>(key[0]), std::bit_cast<float>(key[1]),
                                           std::bit_cast<float>(key[2])};
                        const double o1 = dot(minus(point, b1), along);
                        const double o2 = dot(minus(point, b2), along);
                        plus = plus && linesUp((o1 + pan1) - (o2 + pan2), size);
                        minusSign = minusSign && linesUp((o1 - pan1) - (o2 - pan2), size);
                    }
                    if (plus && !minusSign) ++totals.onlyPlus[axis];
                    if (minusSign && !plus) ++totals.onlyMinus[axis];
                }
            }
        }
    }
}

/// INV-4 over the output: each polygon is a run of fan triangles sharing a
/// first index, and its summed cross product along its vertices' normal is
/// positive.
void checkWinding(const uta::ubundle::Geometry& geometry, Totals& totals) {
    const auto& v = geometry.vertices;
    std::size_t t = 0;
    while (t + 2 < geometry.indices.size()) {
        const std::uint32_t first = geometry.indices[t];
        const Vec p0 = {v[first].position[0], v[first].position[1], v[first].position[2]};
        const Vec normal = {v[first].normal[0], v[first].normal[1], v[first].normal[2]};
        double s = 0;
        while (t + 2 < geometry.indices.size() && geometry.indices[t] == first) {
            const auto& b = v[geometry.indices[t + 1]].position;
            const auto& c = v[geometry.indices[t + 2]].position;
            s += dot(cross(minus({b[0], b[1], b[2]}, p0), minus({c[0], c[1], c[2]}, p0)), normal);
            t += 3;
        }
        if (!(s > 0)) ++totals.inv4Violations;
    }
}

void censusOf(const fs::path& root, const fs::path& mapPath, Totals& totals) {
    ++totals.maps;
    const std::string mapName = detail::mapNameOf(mapPath);
    const auto bytes = uta::fs::readFile(mapPath);
    if (!bytes.has_value()) {
        ++totals.refused["the map does not read"];
        return;
    }
    const auto map = Package::open(*bytes);
    if (!map.has_value()) {
        ++totals.refused["the map does not open"];
        return;
    }

    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& entry : map->exports())
        if (classOf(*map, entry) == "level") levelExport = &entry;
    if (levelExport == nullptr) {
        ++totals.refused["no Level export"];
        return;
    }
    const auto level = uta::upkg::readLevel(*map, *levelExport);
    if (!level.has_value() || level->model.kind() != uta::upkg::ObjectReferenceKind::Export
        || level->model.index() >= map->exports().size()) {
        ++totals.refused["no level Model"];
        return;
    }
    const auto model = uta::upkg::readModel(*map, map->exports()[level->model.index()]);
    if (!model.has_value()) {
        ++totals.refused["readModel refuses"];
        return;
    }

    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());
    const uta::upkg::PackageResolver resolver = install->resolver();

    std::map<std::int32_t, std::optional<TextureFacts>> textures;
    const uta::ubake::MaterialLookup lookup = [&](ObjectReference texture,
                                                  bool) -> const SurfaceMaterial* {
        auto found = textures.find(texture.raw());
        if (found == textures.end()) {
            found = textures.emplace(texture.raw(), factsOf(*map, mapName, texture, resolver)).first;
            if (found->second.has_value()) {
                ++totals.texturesResolved;
                if (const auto scale = found->second->drawScale) {
                    if (totals.withDrawScale == 0 || *scale < totals.minDrawScale)
                        totals.minDrawScale = *scale;
                    if (totals.withDrawScale == 0 || *scale > totals.maxDrawScale)
                        totals.maxDrawScale = *scale;
                    ++totals.withDrawScale;
                }
            }
        }
        return found->second.has_value() ? &found->second->material : nullptr;
    };

    const auto started = std::chrono::steady_clock::now();
    const auto geometry = uta::ubake::buildGeometry(*model, lookup);
    totals.buildSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!geometry.has_value()) {
        ++totals.refused["buildGeometry refuses"];
        return;
    }
    ++totals.built;
    checkWinding(*geometry, totals);

    // SS 4.3's steps again, counting each outcome. buildGeometry accepted
    // this Model, so every index a drawn node reads is in range.
    std::vector<Polygon> polygons;
    for (const auto& node : model->nodes) {
        if (node.numVertices < 3) {
            ++totals.fewerThanThree;
            continue;
        }
        const auto& surf = model->surfs[static_cast<std::size_t>(node.iSurf)];
        if ((surf.polyFlags & uta::ubake::PF_INVISIBLE) != 0) {
            ++totals.invisible;
            continue;
        }
        Polygon polygon;
        polygon.surf = static_cast<std::size_t>(node.iSurf);
        std::vector<Vec> points;
        for (std::size_t k = 0; k < node.numVertices; ++k) {
            const auto index = static_cast<std::uint32_t>(
                model->verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex);
            polygon.points.push_back(index);
            points.push_back(toDouble(model->points[index]));
        }
        const Vec along = toDouble(model->vectors[static_cast<std::size_t>(surf.vNormal)]);
        double s = 0;
        for (std::size_t k = 1; k + 1 < points.size(); ++k)
            s += dot(cross(minus(points[k], points[0]), minus(points[k + 1], points[0])), along);
        if (s == 0) {
            ++totals.noArea;
            continue;
        }
        if (s < 0) ++totals.reversed;
        ++totals.drawn;
        if ((surf.polyFlags & PF_PORTAL) != 0) ++totals.portal;
        if ((surf.polyFlags & PF_FAKE_BACKDROP) != 0) ++totals.fakeBackdrop;
        polygons.push_back(std::move(polygon));
    }
    tallySeams(*model, polygons, textures, totals);
}

} // namespace

TEST_CASE("every map's geometry winds along its surfaces and the census prints",
          "[real-assets][geom]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    std::vector<fs::path> maps;
    for (const fs::directory_entry& entry : fs::directory_iterator(root / "Maps"))
        if (entry.is_regular_file() && detail::fold(entry.path().extension().string()) == ".unr")
            maps.push_back(entry.path());
    std::sort(maps.begin(), maps.end());
    REQUIRE_FALSE(maps.empty());

    Totals totals;
    for (const fs::path& map : maps) censusOf(root, map, totals);

    std::cout << "UTA-0109 geometry census over " << totals.maps << " maps, " << totals.built
              << " built in " << totals.buildSeconds << " s of buildGeometry\n"
              << "  nodes drawn " << totals.drawn << " (reversed " << totals.reversed
              << ", PF_Portal " << totals.portal << ", PF_FakeBackdrop " << totals.fakeBackdrop
              << ")\n"
              << "  skipped: no area " << totals.noArea << ", PF_Invisible " << totals.invisible
              << ", fewer than three vertices " << totals.fewerThanThree << "\n"
              << "  seams lining up only with +: U " << totals.onlyPlus[0] << ", V "
              << totals.onlyPlus[1] << "; only with -: U " << totals.onlyMinus[0] << ", V "
              << totals.onlyMinus[1] << "\n"
              << "  textures resolved (per map naming them) " << totals.texturesResolved
              << ", carrying DrawScale " << totals.withDrawScale;
    if (totals.withDrawScale > 0)
        std::cout << " (" << totals.minDrawScale << " to " << totals.maxDrawScale << ")";
    std::cout << "\n";
    for (const auto& [reason, count] : totals.refused)
        std::cout << "  maps refused, " << reason << ": " << count << "\n";

    CHECK(totals.built > 0);
    CHECK(totals.inv4Violations == 0);
}
