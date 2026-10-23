// Bounced light, baked into probes --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.4 to SS 4.7.
//
// PROBE_BOUNCES bounces of the level's static lights, gathered at lattice points near its
// surfaces and stored as an ambient cube: six colours, one per axis direction.
// Direct light is UTA-0014's, drawn every frame with shadow maps.
//
// DETERMINISTIC AT ANY WORKER COUNT. Each probe's arithmetic depends on that
// probe alone and on the last pass's copy of the others (UTA-0254), its sums
// run in a fixed order, and its result goes to its own slot (INV-10).

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubake/CollisionQuery.h"
#include "ubake/LightModel.h"
#include "ubake/SurfaceRays.h"
#include "ubundle/Bundle.h"
#include "umat/Material.h"
#include "upkg/Geometry.h"

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

/// UTA-0375: the reach of a level with no navigation point -- its Model's own
/// bounding box, grown by PROBE_SPACING so no probe beside an outer face is
/// cut. None when the Model marks the box invalid or a corner is not finite.
[[nodiscard]] std::optional<ProbeReach> levelReachOf(const upkg::Model& model);

/// UTA-0375: how far from the origin a probe may lie on any axis, in UT units.
/// A bound of this project's, not the engine's. MAX_COORDINATE lets a point lie
/// 2^24 units out, where even a slab walk would not end; this keeps it short.
inline constexpr double PROBE_WORLD_LIMIT = 262144;

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
/// backdrop light, not special-lit, not a sun (UTA-0338 SS 4.4, which its
/// caller adds), and static by its resolved bStatic.
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
    /// UTA-0350 SS 4.2: set where the surface is the painted sky's, which
    /// sends this light along the ray in place of the surface's own.
    std::optional<std::array<float, 3>> paintedSky;
};

/// SS 4.7 steps 1-4: the first surface a ray from `p` along `w` meets, sent on
/// once from `sky` where it meets the sky. With no sky view and a `painted`
/// sky, the sky hit itself, carrying that sky's light (UTA-0350 SS 4.2). None
/// where it meets nothing, a surface's back, or the sky with neither, or the
/// sky a second time.
[[nodiscard]] std::optional<SurfaceHit> surfaceAlong(const Vec3& p, const Vec3& w, const SurfaceRays& rays,
                                                     const ubundle::Geometry& geometry,
                                                     const std::optional<Vec3>& sky,
                                                     const std::optional<ubundle::PaintedSky>& painted = std::nullopt);

/// UTA-0338 SS 4.4: the sun's angular radius, in radians -- the disc the
/// renderer draws, and the cone a probe's view of the sun is taken over.
inline constexpr double SUN_RADIUS = 0.035;

/// UTA-0338 SS 4.3: whether the ray from `x` along `s` first meets sky -- a
/// PF_FakeBackdrop surface, a hit `hole` names passing. The sun's test and the
/// sky's alike (UTA-0387), so a rule for the sun alone does not go here.
/// `rays` must hold backdrop surfaces as occluders, as the default set does
/// and SMSK's does not; a ray that meets nothing meets no sky.
[[nodiscard]] bool meetsSkyFirst(const Vec3& x, const Vec3& s, const SurfaceRays& rays,
                           const SurfaceRays::Hole& hole = {});

/// UTA-0338 SS 4.4: the share of the sun a probe at `p` sees -- over `s` and
/// every direction of directions() within SUN_RADIUS of it, the share
/// meetsSkyFirst passes.
[[nodiscard]] float sunSeenFrom(const Vec3& p, const Vec3& s, const SurfaceRays& rays);

/// UTA-0387 SS 4.1: the share of directions() along which a probe at `p`
/// sees the sky -- the share meetsSkyFirst passes.
[[nodiscard]] float skySeenFrom(const Vec3& p, const SurfaceRays& rays);

/// SS 4.13 (UTA-0397): how far a probe at `p` sees along +X, -X, +Y, -Y, +Z
/// and -Z before it meets a surface, each capped at `spacing`.
[[nodiscard]] std::array<float, 6> reachFrom(const Vec3& p, const SurfaceRays& rays, double spacing);

/// SS 4.13: how far past its reach a point may lie and still be lit by a
/// probe, so a point on the surface the reach stopped at is lit by it.
inline constexpr double PROBE_SIGHT_SLACK = 8.0;

/// SS 4.13: whether a probe with `reach` lights a point `d` away from it --
/// no axis of `d` past that axis's reach plus PROBE_SIGHT_SLACK.
[[nodiscard]] bool probeReaches(const std::array<float, 6>& reach, const Vec3& d) noexcept;

/// SS 4.7 step 5: the light of `lights` reaching `x`, facing `n`, each one
/// ray-tested toward the point it is lit from -- a sun by meetsSkyFirst
/// (UTA-0338 SS 4.4).
[[nodiscard]] Rgb lightReaching(const Vec3& x, const Vec3& n, const SurfaceRays& rays,
                                const std::vector<ubundle::Light>& lights);

/// SS 4.12 item 1: the share of the light reaching it a surface of `albedo`
/// sends on -- REFLECTANCE_SCALE times it, at most ALBEDO_CAP.
[[nodiscard]] Rgb reflectanceOf(const Rgb& albedo) noexcept;

/// SS 4.7 step 4's unlit liquid -- or any unlit surface met through the sky
/// view (SS 4.12 item 2), or the painted sky (UTA-0350 SS 4.2) -- and step 6:
/// the light `hit` sends back along the ray that met it.
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
/// seen from (SS 4.12 item 2); with none, a ray meeting the sky brings the
/// `painted` sky's light where there is one (UTA-0350 SS 4.2), else nothing.
[[nodiscard]] std::array<Rgb, 6> gatherProbe(const Vec3& p, const SurfaceRays& rays,
                                             const ubundle::Geometry& geometry,
                                             const std::vector<ubundle::Light>& lights,
                                             const AlbedoLookup& albedo, const OwnLightLookup& own = {},
                                             const std::optional<Vec3>& sky = std::nullopt,
                                             const std::optional<ubundle::PaintedSky>& painted = std::nullopt);

/// SS 4.7 step 7 (UTA-0254): the bounces a probe holds. The first is
/// gatherProbe's; each further one is a bounceProbe pass over the last.
inline constexpr std::size_t PROBE_BOUNCES = 3;

/// SS 4.7 step 7 (UTA-0254): the six faces of the light the probes `lit` give
/// the surfaces seen from `p`, sent on as step 6 sends direct light. `lit`'s
/// probes are read as probes.glsl's indirectAt reads them. A sky, and an unlit
/// surface that sends its picture (step 4), send nothing here.
[[nodiscard]] std::array<Rgb, 6> bounceProbe(const Vec3& p, const SurfaceRays& rays,
                                             const ubundle::Geometry& geometry, const ubundle::LightProbes& lit,
                                             const AlbedoLookup& albedo, const OwnLightLookup& own = {},
                                             const std::optional<Vec3>& sky = std::nullopt,
                                             const std::optional<ubundle::PaintedSky>& painted = std::nullopt);

/// SS 4.6 and SS 4.7: every probe of the level, none outside `reach` where
/// there is one, nor past PROBE_WORLD_LIMIT, each holding PROBE_BOUNCES bounces.
/// Each probe's `reach` (SS 4.13); where `lights` holds a sun, its `sunSeen`
/// (UTA-0338 SS 4.4); and where the level has sky, its `skySeen` (UTA-0387).
/// A job that throws refuses it.
[[nodiscard]] Result<ubundle::LightProbes> bakeLightProbes(
    const ubundle::Geometry& geometry, const ubundle::CollisionTree& level,
    const std::vector<ubundle::Light>& lights, const AlbedoLookup& albedo, JobSystem& jobs,
    const std::optional<ProbeReach>& reach = std::nullopt, const OwnLightLookup& own = {},
    const std::optional<Vec3>& sky = std::nullopt, const std::optional<ubundle::PaintedSky>& painted = std::nullopt);

} // namespace uta::ubake
