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

/// SS 4.6 step 2 (UTA-0212): how far past the level's navigation points a
/// probe may lie, in UT units. A skybox sits far from the play area, so the
/// margin is wide: over 64 sampled maps, a lit surface reached at most 29,408
/// units past the navigation points' box. Probes then stay where they were on
/// such maps, while a floor 500,000 units across no longer takes them all.
inline constexpr double PROBE_REACH_MARGIN = 32768;

/// SS 4.6 step 2: a box of UT coordinates, bounds included.
struct ProbeReach {
    Vec3 low;
    Vec3 high;
};

/// SS 4.6 step 2: the box around the Location of every placed actor whose class
/// is Engine.NavigationPoint or descends from it, grown by PROBE_REACH_MARGIN.
/// None when the level places no such actor.
[[nodiscard]] std::optional<ProbeReach> probeReachOf(const ubundle::Placements& placements);

/// SS 4.5: the reflectance of a surface with no material, or whose material's
/// base level has no opaque pixel.
inline constexpr double DEFAULT_ALBEDO = 0.5;

/// SS 4.12 item 1 (UTA-0292): a surface sends on this many times the light it
/// shows. UT99's textures average 0.05-0.07 linear where real materials reflect
/// 0.2-0.5; the screen makes up for it with EXPOSURE, and the bounce now does too.
inline constexpr double REFLECTANCE_SCALE = 4;

/// SS 4.12 item 1: the most of the light reaching it that a surface sends on.
inline constexpr double ALBEDO_CAP = 0.9;

/// SS 4.4: the lights of `lights` that bake, in the order given -- not a
/// backdrop light, not special-lit, and static by its resolved bStatic.
[[nodiscard]] std::vector<ubundle::Light> bakedLights(const std::vector<ubundle::Light>& lights,
                                                      const ubundle::Placements& placements);

/// SS 4.5: the linear mean of an RGBA image's pixels whose alpha is not 0,
/// summed in row-major order; empty when no pixel qualifies.
[[nodiscard]] std::optional<Rgb> meanAlbedo(const umat::Image& rgba) noexcept;

using AlbedoLookup = std::function<Rgb(std::string_view materialId)>;
/// UTA-0161: what a material sends on of its own, beyond the light it reflects.
struct OwnLight {
    Rgb emission;            ///< its emit map's linear mean; zero when it does not glow
    bool unlitGlows = false; ///< a liquid: drawn PF_Unlit, it sends on its picture
};
using OwnLightLookup = std::function<OwnLight(std::string_view materialId)>;

/// SS 4.7 step 5: a shadow ray starts this far off the surface, along its normal.
inline constexpr double SHADOW_OFFSET = 0.5;

/// SS 4.7 steps 1-4: the lit side of the surface a ray meets. UTA-0292's
/// reference tracer shares these four functions with the probe bake, so the
/// two measure one light model (docs/specs/UTA-0292-reference-path-tracer.md
/// SS 4.1).
struct SurfaceHit {
    Vec3 at;                                       ///< where the ray meets it
    Vec3 normal;                                   ///< facing the ray
    const ubundle::GeometryBatch* batch = nullptr; ///< the surface's batch
    bool viaSky = false;                           ///< met from the sky view (SS 4.12 item 2)
};

/// SS 4.7 steps 1-4: the first surface a ray from `p` along `w` meets, sent on
/// once from `sky` where it meets the sky. None where it meets nothing, a
/// surface's back, or the sky with no sky view or a second time.
[[nodiscard]] std::optional<SurfaceHit> surfaceAlong(const Vec3& p, const Vec3& w, const SurfaceRays& rays,
                                                     const ubundle::Geometry& geometry,
                                                     const std::optional<Vec3>& sky);

/// SS 4.7 step 5: the light of `lights` reaching `x`, facing `n`, each one
/// ray-tested toward the point it is lit from.
[[nodiscard]] Rgb lightReaching(const Vec3& x, const Vec3& n, const SurfaceRays& rays,
                                const std::vector<ubundle::Light>& lights);

/// SS 4.12 item 1: the share of the light reaching it a surface of `albedo`
/// sends on -- REFLECTANCE_SCALE times it, at most ALBEDO_CAP.
[[nodiscard]] Rgb reflectanceOf(const Rgb& albedo) noexcept;

/// SS 4.7 step 4's unlit liquid -- or any unlit surface met through the sky
/// view (SS 4.12 item 2) -- and step 6: the light `hit` sends back along the
/// ray that met it.
[[nodiscard]] Rgb sentFrom(const SurfaceHit& hit, const SurfaceRays& rays,
                           const std::vector<ubundle::Light>& lights, const AlbedoLookup& albedo,
                           const OwnLightLookup& own);

/// SS 4.7: the ray directions -- an icosahedron's vertices with each edge split
/// at its midpoint twice -- ascending by z, then y, then x.
[[nodiscard]] const std::vector<Vec3>& directions();

/// SS 4.7: the six faces from one radiance per direction, in directions()'
/// order. Face k is sum(L * max(0, w . a_k)) / sum(max(0, w . a_k)).
[[nodiscard]] std::array<Rgb, 6> cubeOf(std::span<const Rgb> radiance);

/// SS 4.7: the six faces of one probe at `p`. `sky` is where the level's sky is
/// seen from (SS 4.12 item 2); with none, a ray meeting the sky brings nothing.
[[nodiscard]] std::array<Rgb, 6> gatherProbe(const Vec3& p, const SurfaceRays& rays,
                                             const ubundle::Geometry& geometry,
                                             const std::vector<ubundle::Light>& lights,
                                             const AlbedoLookup& albedo, const OwnLightLookup& own = {},
                                             const std::optional<Vec3>& sky = std::nullopt);

/// SS 4.6 and SS 4.7: every probe of the level, none outside `reach` where
/// there is one. A job that throws refuses it.
[[nodiscard]] Result<ubundle::LightProbes> bakeLightProbes(
    const ubundle::Geometry& geometry, const ubundle::CollisionTree& level,
    const std::vector<ubundle::Light>& lights, const AlbedoLookup& albedo, JobSystem& jobs,
    const std::optional<ProbeReach>& reach = std::nullopt, const OwnLightLookup& own = {},
    const std::optional<Vec3>& sky = std::nullopt);

} // namespace uta::ubake
