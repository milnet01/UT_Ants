// ubake: the lamps a map's recipe adds -- docs/specs/UTA-0256-added-lamps.md
// SS 4.3.
//
// A lamp copies one of the map's lights and the fitting that holds it: the
// drawn surfaces and the flames of brushes the recipe names, moved to `at` and
// turned by `yaw` about the light. Nothing here adds the lamp to any ray set:
// the bakes see its light and never its fitting (SS 4.3 step 4).

#pragma once

#include "core/Error.h"
#include "ubake/Flames.h"
#include "ubake/Geometry.h"
#include "ubundle/Bundle.h"
#include "umap/Rooms.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "urecipe/Recipe.h"

#include <array>
#include <cstddef>
#include <vector>

namespace uta::ubake {

/// `point` turned by `yaw` (65536 to a turn, +X toward +Y, as UT99 turns) about
/// the vertical through `pivot`, then moved so `pivot` lands on `at`.
[[nodiscard]] std::array<float, 3> placedPoint(const std::array<float, 3>& point, const std::array<float, 3>& pivot,
                                               const std::array<float, 3>& at, std::uint32_t yaw) noexcept;

/// SS 4.3, every lamp of `lamps` in order. `lights` are LITE's, strips marked;
/// `flames` are the level's, with their sources. Refuses MalformedData naming
/// the lamp when its light names no light of the map, or one litDirectly
/// rejects or a strip holds, and when a fitting brush is not in the map or
/// gives neither a drawn surface nor a flame.
[[nodiscard]] Result<std::vector<ubundle::AddedLamp>> buildLamps(
    const upkg::Package& map, const upkg::Model& model, const std::vector<urecipe::AddedLamp>& lamps,
    const std::vector<ubundle::Light>& lights, const MaterialLookup& materials, const FlameSheets& flames,
    const umap::RoomMap& rooms, std::size_t zoneCount);

} // namespace uta::ubake
