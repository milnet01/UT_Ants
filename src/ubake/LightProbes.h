// Bounced light, baked into probes --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.4 to SS 4.7.
//
// One bounce of the level's static lights, gathered at lattice points near its
// surfaces and stored as an ambient cube: six colours, one per axis direction.
// Direct light is UTA-0014's, drawn every frame with shadow maps.
//
// DETERMINISTIC AT ANY WORKER COUNT. Each probe's arithmetic depends on that
// probe alone, its sums run in a fixed order, and its result goes to its own
// slot (INV-10).

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubake/CollisionQuery.h"
#include "ubake/LightModel.h"
#include "ubake/SurfaceRays.h"
#include "ubundle/Bundle.h"
#include "umat/Material.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace uta::ubake {

/// SS 4.6: the lattice spacing, in UT units.
inline constexpr std::uint32_t PROBE_SPACING = 128;

/// SS 4.5: the reflectance of a surface with no material, or whose material's
/// base level has no opaque pixel.
inline constexpr double DEFAULT_ALBEDO = 0.5;

/// SS 4.4: the lights of `lights` that bake, in the order given -- not a
/// backdrop light, not special-lit, and static by its resolved bStatic.
[[nodiscard]] std::vector<ubundle::Light> bakedLights(const std::vector<ubundle::Light>& lights,
                                                      const ubundle::Placements& placements);

/// SS 4.5: the linear mean of an RGBA image's pixels whose alpha is not 0,
/// summed in row-major order; empty when no pixel qualifies.
[[nodiscard]] std::optional<Rgb> meanAlbedo(const umat::Image& rgba) noexcept;

using AlbedoLookup = std::function<Rgb(std::string_view materialId)>;

/// SS 4.7: the ray directions -- an icosahedron's vertices with each edge split
/// at its midpoint twice -- ascending by z, then y, then x.
[[nodiscard]] const std::vector<Vec3>& directions();

/// SS 4.7: the six faces from one radiance per direction, in directions()'
/// order. Face k is sum(L * max(0, w . a_k)) / sum(max(0, w . a_k)).
[[nodiscard]] std::array<Rgb, 6> cubeOf(std::span<const Rgb> radiance);

/// SS 4.7: the six faces of one probe at `p`.
[[nodiscard]] std::array<Rgb, 6> gatherProbe(const Vec3& p, const SurfaceRays& rays,
                                             const ubundle::Geometry& geometry,
                                             const std::vector<ubundle::Light>& lights,
                                             const AlbedoLookup& albedo);

/// SS 4.6 and SS 4.7: every probe of the level. A job that throws refuses it.
[[nodiscard]] Result<ubundle::LightProbes> bakeLightProbes(
    const ubundle::Geometry& geometry, const ubundle::CollisionTree& level,
    const std::vector<ubundle::Light>& lights, const AlbedoLookup& albedo, JobSystem& jobs);

} // namespace uta::ubake
