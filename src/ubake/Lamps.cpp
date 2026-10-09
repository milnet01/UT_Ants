// The lamps a map's recipe adds -- docs/specs/UTA-0256-added-lamps.md SS 4.3.

#include "ubake/Lamps.h"

#include "ubake/Install.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <set>
#include <string>

namespace uta::ubake {
namespace {

/// The export of `map` whose object name folds to `name`'s fold, or none.
std::optional<std::uint32_t> exportNamed(const upkg::Package& map, std::string_view name) {
    const std::string wanted = detail::fold(name);
    const auto exports = map.exports();
    for (std::uint32_t i = 0; i < exports.size(); ++i) {
        const auto own = map.name(exports[i].objectName);
        if (own.has_value() && detail::fold(*own) == wanted) return i;
    }
    return std::nullopt;
}

/// Every surface index NOT in `keep`, ascending, with `alsoOmit` merged in:
/// buildGeometry's `omitted`, so it draws `keep` and nothing else.
std::vector<std::uint32_t> allBut(std::size_t surfaces, const std::set<std::uint32_t>& keep,
                                  const std::vector<std::uint32_t>& alsoOmit) {
    std::vector<std::uint32_t> omitted;
    for (std::uint32_t s = 0; s < surfaces; ++s)
        if (!keep.contains(s) || std::ranges::binary_search(alsoOmit, s)) omitted.push_back(s);
    return omitted;
}

std::array<float, 3> turned(const std::array<float, 3>& v, std::uint32_t yaw) noexcept {
    const double angle = static_cast<double>(yaw) * 2 * std::numbers::pi / 65536.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {static_cast<float>(v[0] * c - v[1] * s), static_cast<float>(v[0] * s + v[1] * c), v[2]};
}

} // namespace

std::array<float, 3> placedPoint(const std::array<float, 3>& point, const std::array<float, 3>& pivot,
                                 const std::array<float, 3>& at, std::uint32_t yaw) noexcept {
    const std::array<float, 3> offset = turned({point[0] - pivot[0], point[1] - pivot[1], point[2] - pivot[2]}, yaw);
    return {at[0] + offset[0], at[1] + offset[1], at[2] + offset[2]};
}

Result<std::vector<ubundle::AddedLamp>> buildLamps(const upkg::Package& map, const upkg::Model& model,
                                                   const std::vector<urecipe::AddedLamp>& lamps,
                                                   const std::vector<ubundle::Light>& lights,
                                                   const MaterialLookup& materials, const FlameSheets& flames,
                                                   const umap::RoomMap& rooms, std::size_t zoneCount) {
    std::vector<ubundle::AddedLamp> out;
    out.reserve(lamps.size());
    for (const urecipe::AddedLamp& wanted : lamps) {
        const auto refuse = [&wanted](const std::string& why) {
            return fail(ErrorCode::MalformedData, std::format("recipe lamp {}: {}", wanted.name, why));
        };

        // Step 1: the light it copies.
        const auto lightExport = exportNamed(map, wanted.light);
        const auto found = lightExport.has_value()
                               ? std::ranges::find(lights, *lightExport, &ubundle::Light::exportIndex)
                               : lights.end();
        if (found == lights.end()) return refuse(std::format("{} is not a light of the map", wanted.light));
        if (!ubundle::litDirectly(*found)) return refuse(std::format("{} gives no direct light", wanted.light));
        if (found->strip != ubundle::STRIP_NONE) return refuse(std::format("{} is part of a strip", wanted.light));
        const std::array<float, 3> pivot = found->location;

        // Step 3: each fitting brush, and what it gives.
        std::set<std::uint32_t> fitting; // surface indices
        for (const std::string& brush : wanted.fitting) {
            const auto brushExport = exportNamed(map, brush);
            if (!brushExport.has_value()) return refuse(std::format("{} is not in the map", brush));
            std::set<std::uint32_t> own;
            for (std::uint32_t s = 0; s < model.surfs.size(); ++s) {
                const upkg::ObjectReference actor = model.surfs[s].actor;
                if (actor.kind() == upkg::ObjectReferenceKind::Export && actor.index() == *brushExport) own.insert(s);
            }
            const bool flame = std::ranges::any_of(flames.sources, [&own](const std::vector<std::uint32_t>& sheets) {
                return std::ranges::any_of(sheets, [&own](std::uint32_t s) { return own.contains(s); });
            });
            UTA_TRY(const ubundle::Geometry drawn,
                    buildGeometry(model, materials, zoneCount, allBut(model.surfs.size(), own, flames.surfaces)));
            if (drawn.indices.empty() && !flame)
                return refuse(std::format("{} gives neither a drawn surface nor a flame", brush));
            fitting.insert(own.begin(), own.end());
        }

        ubundle::AddedLamp lamp;
        // Step 2: the copy's light.
        lamp.light = *found;
        lamp.light.location = wanted.at;
        lamp.light.rotation[1] += static_cast<std::int32_t>(wanted.yaw);

        UTA_TRY(lamp.shape, buildGeometry(model, materials, zoneCount,
                                          allBut(model.surfs.size(), fitting, flames.surfaces)));
        for (ubundle::GeometryVertex& vertex : lamp.shape.vertices) {
            vertex.position = placedPoint(vertex.position, pivot, wanted.at, wanted.yaw);
            vertex.normal = turned(vertex.normal, wanted.yaw);
            vertex.zone = ubundle::zoneAt(rooms, vertex.position, zoneCount);
        }
        for (std::size_t k = 0; k < flames.flames.size(); ++k) {
            if (!std::ranges::any_of(flames.sources[k], [&fitting](std::uint32_t s) { return fitting.contains(s); }))
                continue;
            ubundle::Flame flame = flames.flames[k];
            flame.base = placedPoint(flame.base, pivot, wanted.at, wanted.yaw);
            flame.light = -1;
            lamp.flames.push_back(flame);
        }
        out.push_back(std::move(lamp));
    }
    return out;
}

} // namespace uta::ubake
