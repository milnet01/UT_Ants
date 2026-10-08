// Exact light to measure the renderer against --
// docs/specs/UTA-0292-reference-path-tracer.md SS 4.4.
//
// THE PROBE BAKE'S OWN MODEL. Where a ray meets a surface, what that surface
// sends back is ubake's sentFrom, the function the probes are baked with; so a
// gap between the renderer and this is the renderer's (or the single bounce's),
// never a second copy of the model drifting from the first.
//
// SAME AT ANY WORKER COUNT (INV-10). Each pixel draws its random numbers from
// a stream seeded by its coordinates alone, and writes only its own slots.

#pragma once

#include "ubake/LightProbes.h"
#include "ubake/MaterialLight.h"
#include "ubake/SurfaceRays.h"
#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace uta::ref {

/// SS 4.4's channels, per pixel, in this order.
enum Channel : std::size_t { DIRECT, FIRST, LATER, SKY, LIT, CHANNELS };

class Reference {
public:
    /// `lights` are the ones that bake (ubake::bakedLights); `sky` is where the
    /// sky view stands, or none. A material `materials` does not name takes
    /// DEFAULT_ALBEDO and sends nothing of its own, as the bake's lookups do.
    Reference(const ubundle::Geometry& geometry, std::vector<ubundle::Light> lights, std::optional<ubake::Vec3> sky,
              std::span<const ubake::MaterialLight> materials);
    // Its lookups hold `this`, so a copy would read the original's materials.
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;

    /// SS 4.4: one pixel's channels, for the eye ray from `eye` along `dir`.
    [[nodiscard]] std::array<float, CHANNELS> pixel(const ubake::Vec3& eye, const ubake::Vec3& dir, int samples,
                                                    int depth, std::uint64_t seed) const;

    /// SS 4.4: every pixel of a `width` by `height` view from `camera`, row 0 at
    /// the top, CHANNELS floats each, on `workers` threads.
    [[nodiscard]] std::vector<float> trace(const urender::Camera& camera, double horizontalFovDegrees,
                                           std::uint32_t width, std::uint32_t height, int samples, int depth,
                                           unsigned workers) const;

private:
    [[nodiscard]] ubake::Rgb radiance(const ubake::Vec3& p, const ubake::Vec3& w, int depth,
                                      std::uint64_t& random) const;
    [[nodiscard]] bool reflects(const ubake::SurfaceHit& hit) const;

    const ubundle::Geometry& geometry_;
    ubake::SurfaceRays rays_;
    ubake::SurfaceRays eyeRays_;
    std::vector<ubundle::Light> lights_;
    std::optional<ubake::Vec3> sky_;
    std::unordered_map<std::string, ubake::MaterialLight> materials_;
    ubake::AlbedoLookup albedo_;
    ubake::OwnLightLookup own_;
};

/// SS 4.4's score of one view: the renderer's light terms against the
/// reference's channels, over 8 by 8 blocks.
struct Score {
    std::size_t blocks = 0;  ///< blocks kept: at least 48 pixels lit in both
    double reference = 0;    ///< the reference's mean light over kept blocks
    double renderer = 0;     ///< the renderer's
    double total = 0;        ///< mean absolute block gap, over `reference`
    double direct = 0;       ///< the direct term's gap
    double indirect = 0;     ///< the indirect term's against first bounce and sky
    double sky = 0;          ///< the sky's share of `reference`
    double later = 0;        ///< later bounces' share of `reference`
    double stops = 0;        ///< mean absolute log2 ratio, on blocks both light
};

/// `reference` is width * height * CHANNELS floats; `terms` is width * height
/// * 3 floats, red the direct term and green the indirect; both row 0 at the top.
[[nodiscard]] Score scoreView(std::span<const float> reference, std::span<const float> terms, std::uint32_t width,
                              std::uint32_t height);

} // namespace uta::ref
