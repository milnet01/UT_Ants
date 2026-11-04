// The grass a bake plants on a map's floors, as VEGE --
// docs/specs/UTA-0293-grass.md SS 4.1 to SS 4.3.
//
// A FILE OF ITS OWN because it changes for its own reason: the name list and
// the greenness thresholds are tuned by eye on real maps, and nothing else in
// the bake moves when they are.
//
// PLANTED FROM THE BUNDLE, after SMSK: GEOM's batches carry each surface's
// material and flags, its vertices their zone and texture coordinates, and
// SMSK each vertex's chart. The only input from outside is each ground-cover
// material's greenness, made with the material (Bake.cpp's makeVariant).
//
// THE SAME BYTES ON EVERY COMPILER: triangles in index order, every random
// choice a hash of integers, arithmetic in double.

#pragma once

#include "ubundle/Bundle.h"
#include "umat/Material.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace uta::ubake {

/// SS 4.2: the grid a floor is sampled on, in world units.
inline constexpr double CLUMP_SPACING = 16.0;
/// SS 4.2: how many picture texels a clump should span, which picks the mip
/// greenness is read from.
inline constexpr double TEXELS_PER_CLUMP = 8.0;
/// SS 4.1 item 3: the least up component of a floor's normal.
inline constexpr double GRASS_SLOPE = 0.7;
/// SS 4.2: no clump lies nearer a pickup than this, horizontally.
inline constexpr double PICKUP_CLEAR = 40.0;
/// SS 4.2: `g - max(r, b)`, linear, at which greenness starts and is full.
/// First values; SS 13 records how they were set.
inline constexpr double GREEN_LOW = 0.01;
inline constexpr double GREEN_HIGH = 0.06;
/// SS 4.3: the side of a VEGE cell, in world units.
inline constexpr double GRASS_CELL = 1024.0;
/// SS 4.4: the tallest a blade stands and the furthest it leans, in world
/// units. A cell's bounds take them in. grass.vert's blades stay inside it.
inline constexpr double BLADE_REACH = 20.0;

/// SS 4.1 item 1: `materialId`'s name or any group -- umat::materialId's
/// `<package>.<group...>.<name>`, lower case, any "#masked" dropped, the
/// package left out -- holds a ground-cover word.
[[nodiscard]] bool isGroundCover(std::string_view materialId);

/// SS 4.2: a picture's greenness at the mip a clump reads, row-major, 0 to
/// 255 for 0 to 1.
struct GreenMap {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> green;
};

/// SS 4.2: `rgba`'s greenness, box-filtered in linear light down to the level
/// whose texel spans CLUMP_SPACING / TEXELS_PER_CLUMP world units, given that
/// one of its own texels spans `unitsPerTexel`. Empty for a picture that is not
/// four channels.
[[nodiscard]] std::optional<GreenMap> greenMapOf(const umat::Image& rgba, double unitsPerTexel);

/// A ground-cover material's GreenMap by MATS id; nullptr where it has none.
using GreenLookup = std::function<const GreenMap*(std::string_view materialId)>;

/// What planting made: the section, or why it was left out.
struct Planted {
    /// Empty where nothing grows, or past the limit.
    std::optional<ubundle::Grass> grass;
    /// SS 4.3: how many clumps the map would have had, where that passed
    /// GRASS_CLUMP_LIMIT; 0 otherwise.
    std::size_t overLimit = 0;
};

/// SS 4.1 and SS 4.2 over `bundle`'s GEOM: every floor triangle of a
/// ground-cover material, sampled on a jittered CLUMP_SPACING grid in the
/// world's plan, each sample kept by the picture's greenness and dropped near
/// a pickup. Reads MATS, GEOM, ZONE, PLAC and SMSK; any may be absent.
[[nodiscard]] Planted plantGrass(const ubundle::Bundle& bundle, const GreenLookup& green);

} // namespace uta::ubake
