// Bounced light, baked into probes --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.4 to SS 4.7. LightProbes.h
// says what is baked, and why it is the same at any worker count.

#include "ubake/LightProbes.h"

#include "ubake/Actors.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace uta::ubake {
namespace {

/// LightType's and PolyFlags' values -- the 432 headers'
/// Engine/Inc/EngineClasses.h and Engine/Inc/UnObj.h.
constexpr std::uint8_t LT_BACKDROP_LIGHT = 6;
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x80;
constexpr std::uint32_t PF_TWO_SIDED = 0x100;

/// SS 4.7 step 5: a shadow ray starts this far off the surface, along its normal.
constexpr double SHADOW_OFFSET = 0.5;

/// Probes to one job. Any size gives the same bytes; this one keeps the number
/// of jobs small.
constexpr std::size_t PROBES_PER_JOB = 64;

using Cell = std::array<std::int32_t, 3>;

Vec3 normalised(const Vec3& v) noexcept {
    const double size = length(v);
    return {v.x / size, v.y / size, v.z / size};
}

Vec3 positionOf(const ubundle::Geometry& geometry, std::uint32_t index) {
    const auto& p = geometry.vertices[index].position;
    return {p[0], p[1], p[2]};
}

/// SS 4.7 step 2: the triangle's first vertex's normal, normalised.
Vec3 normalOf(const ubundle::Geometry& geometry, std::size_t triangle) {
    const auto& n = geometry.vertices[geometry.indices[3 * triangle]].normal;
    return normalised({n[0], n[1], n[2]});
}

/// The batch whose run of indices holds `triangle`. Batches tile `indices` in
/// order (UTA-0109 SS 4.2), so their first indices ascend.
const ubundle::GeometryBatch& batchOf(const ubundle::Geometry& geometry, std::size_t triangle) {
    const auto index = static_cast<std::uint32_t>(3 * triangle);
    const auto after = std::upper_bound(
        geometry.batches.begin(), geometry.batches.end(), index,
        [](std::uint32_t value, const ubundle::GeometryBatch& batch) { return value < batch.firstIndex; });
    return *(after - 1);
}

double component(const Vec3& v, std::size_t axis) noexcept {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

/// SS 4.7's directions: the icosahedron's vertices, its faces found as the
/// triples of mutually adjacent vertices, then each edge split at its
/// normalised midpoint twice. A midpoint is the normalised sum of its edge's
/// two ends, and addition commutes, so no face order changes a value.
std::vector<Vec3> buildDirections() {
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Vec3> raw;
    for (const double s : {-1.0, 1.0})
        for (const double t : {-1.0, 1.0}) {
            raw.push_back({0, s, t * phi});
            raw.push_back({s, t * phi, 0});
            raw.push_back({t * phi, 0, s});
        }
    // Before normalising, an edge is 2 long and every other pair is farther.
    const auto adjacent = [&raw](std::size_t i, std::size_t j) {
        const Vec3 d = raw[i] - raw[j];
        return std::abs(dot(d, d) - 4.0) < 1e-9;
    };
    std::vector<std::array<std::size_t, 3>> faces;
    for (std::size_t i = 0; i < raw.size(); ++i)
        for (std::size_t j = i + 1; j < raw.size(); ++j)
            for (std::size_t k = j + 1; k < raw.size(); ++k)
                if (adjacent(i, j) && adjacent(j, k) && adjacent(i, k)) faces.push_back({i, j, k});

    std::vector<Vec3> points;
    for (const Vec3& v : raw) points.push_back(normalised(v));
    for (int split = 0; split < 2; ++split) {
        std::map<std::pair<std::size_t, std::size_t>, std::size_t> middles;
        const auto middle = [&](std::size_t a, std::size_t b) {
            const auto [it, added] = middles.try_emplace({std::min(a, b), std::max(a, b)}, points.size());
            if (added) points.push_back(normalised(points[a] + points[b]));
            return it->second;
        };
        std::vector<std::array<std::size_t, 3>> smaller;
        for (const auto& [a, b, c] : faces) {
            const std::size_t ab = middle(a, b);
            const std::size_t bc = middle(b, c);
            const std::size_t ca = middle(c, a);
            smaller.push_back({a, ab, ca});
            smaller.push_back({ab, b, bc});
            smaller.push_back({ca, bc, c});
            smaller.push_back({ab, bc, ca});
        }
        faces = std::move(smaller);
    }
    std::sort(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) {
        return std::tie(a.z, a.y, a.x) < std::tie(b.z, b.y, b.x);
    });
    return points;
}

/// SS 4.7's radiance along one direction, its steps numbered as there.
Rgb radianceAlong(const Vec3& p, const Vec3& w, const SurfaceRays& rays,
                  const ubundle::Geometry& geometry, const std::vector<ubundle::Light>& lights,
                  const AlbedoLookup& albedo) {
    const std::optional<SurfaceRays::Hit> hit = rays.first(p, w);
    if (!hit) return {};                                                   // 1
    const ubundle::GeometryBatch& batch = batchOf(geometry, hit->triangle);
    Vec3 n = normalOf(geometry, hit->triangle);                             // 2
    if (dot(w, n) >= 0) {                                                   // 3
        if ((batch.polyFlags & PF_TWO_SIDED) == 0) return {};
        n = n * -1.0;
    }
    if ((batch.polyFlags & PF_FAKE_BACKDROP) != 0) return {};               // 4

    const Vec3 x = p + w * hit->t;                                          // 5
    Rgb e;
    for (const ubundle::Light& light : lights) {
        const Rgb lit = lightAt(light, x, n);
        // A light that puts nothing here adds nothing either way, so its
        // shadow ray is not worth casting.
        if (lit.r == 0 && lit.g == 0 && lit.b == 0) continue;
        const Vec3 at{light.location[0], light.location[1], light.location[2]};
        if (rays.blocked(x + n * SHADOW_OFFSET, at)) continue;
        e.r += lit.r;
        e.g += lit.g;
        e.b += lit.b;
    }
    const Rgb a = albedo(batch.material);                                   // 6
    return {a.r * e.r, a.g * e.g, a.b * e.b};
}

} // namespace

std::vector<ubundle::Light> bakedLights(const std::vector<ubundle::Light>& lights,
                                        const ubundle::Placements& placements) {
    static const std::vector<ubundle::PropertyRecord> none;
    std::vector<ubundle::Light> out;
    for (const ubundle::Light& light : lights) {
        if (light.type == LT_BACKDROP_LIGHT || light.specialLit) continue;
        const auto found = std::lower_bound(
            placements.actors.begin(), placements.actors.end(), light.exportIndex,
            [](const ubundle::ActorPlacement& actor, std::uint32_t slot) { return actor.exportIndex < slot; });
        if (found == placements.actors.end() || found->exportIndex != light.exportIndex) continue;
        const auto& defaults = found->classIndex < placements.classes.size()
                                   ? placements.classes[found->classIndex].defaults
                                   : none;
        // UTA-0110 SS 4.6's order: the actor's own, else its class's default.
        const ubundle::PropertyRecord* const record = detail::resolvedRecord(
            "bstatic", found->properties, defaults,
            [](const ubundle::PropertyRecord& r) { return r.kind == ubundle::ValueKind::Bool; });
        const bool* const isStatic = record == nullptr ? nullptr : std::get_if<bool>(&record->value);
        if (isStatic == nullptr || !*isStatic) continue;
        out.push_back(light);
    }
    return out;
}

std::optional<Rgb> meanAlbedo(const umat::Image& rgba) noexcept {
    if (rgba.channels != 4) return std::nullopt;
    Rgb sum;
    std::size_t counted = 0;
    for (std::size_t i = 0; i + 4 <= rgba.pixels.size(); i += 4) {
        if (rgba.pixels[i + 3] == std::byte{0}) continue;
        sum.r += linearOf(static_cast<std::uint8_t>(rgba.pixels[i]));
        sum.g += linearOf(static_cast<std::uint8_t>(rgba.pixels[i + 1]));
        sum.b += linearOf(static_cast<std::uint8_t>(rgba.pixels[i + 2]));
        ++counted;
    }
    if (counted == 0) return std::nullopt;
    const auto n = static_cast<double>(counted);
    return Rgb{sum.r / n, sum.g / n, sum.b / n};
}

const std::vector<Vec3>& directions() {
    static const std::vector<Vec3> all = buildDirections();
    return all;
}

std::array<Rgb, 6> cubeOf(std::span<const Rgb> radiance) {
    const std::vector<Vec3>& all = directions();
    const std::size_t count = std::min(all.size(), radiance.size());
    std::array<Rgb, 6> faces{};
    // Faces +X, -X, +Y, -Y, +Z, -Z: axis k / 2, and its sign by k's parity.
    for (std::size_t k = 0; k < 6; ++k) {
        const double sign = k % 2 == 0 ? 1.0 : -1.0;
        Rgb sum;
        double weights = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double w = std::max(0.0, sign * component(all[i], k / 2));
            sum.r += radiance[i].r * w;
            sum.g += radiance[i].g * w;
            sum.b += radiance[i].b * w;
            weights += w;
        }
        faces[k] = {sum.r / weights, sum.g / weights, sum.b / weights};
    }
    return faces;
}

std::array<Rgb, 6> gatherProbe(const Vec3& p, const SurfaceRays& rays,
                               const ubundle::Geometry& geometry,
                               const std::vector<ubundle::Light>& lights,
                               const AlbedoLookup& albedo) {
    const std::vector<Vec3>& all = directions();
    std::vector<Rgb> radiance;
    radiance.reserve(all.size());
    for (const Vec3& w : all) radiance.push_back(radianceAlong(p, w, rays, geometry, lights, albedo));
    return cubeOf(radiance);
}

Result<ubundle::LightProbes> bakeLightProbes(const ubundle::Geometry& geometry,
                                             const ubundle::CollisionTree& level,
                                             const std::vector<ubundle::Light>& lights,
                                             const AlbedoLookup& albedo, JobSystem& jobs) {
    const auto spacing = static_cast<double>(PROBE_SPACING);
    const auto pointOf = [spacing](const Cell& cell) {
        return Vec3{cell[0] * spacing, cell[1] * spacing, cell[2] * spacing};
    };

    // SS 4.6 steps 1 and 2: each non-sky triangle's grown box, cut to the
    // lattice points within one spacing of its plane.
    std::vector<Cell> candidates;
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        if ((batch.polyFlags & PF_FAKE_BACKDROP) != 0) continue;
        for (std::uint32_t i = batch.firstIndex; i + 3 <= batch.firstIndex + batch.indexCount;
             i += 3) {
            const Vec3 a = positionOf(geometry, geometry.indices[i]);
            const Vec3 b = positionOf(geometry, geometry.indices[i + 1]);
            const Vec3 c = positionOf(geometry, geometry.indices[i + 2]);
            const Vec3 n = normalOf(geometry, i / 3);
            const Vec3 low{std::min({a.x, b.x, c.x}) - spacing, std::min({a.y, b.y, c.y}) - spacing,
                           std::min({a.z, b.z, c.z}) - spacing};
            const Vec3 high{std::max({a.x, b.x, c.x}) + spacing, std::max({a.y, b.y, c.y}) + spacing,
                            std::max({a.z, b.z, c.z}) + spacing};
            const auto from = [spacing](double v) { return static_cast<std::int32_t>(std::ceil(v / spacing)); };
            const auto to = [spacing](double v) { return static_cast<std::int32_t>(std::floor(v / spacing)); };
            for (std::int32_t k = from(low.z); k <= to(high.z); ++k)
                for (std::int32_t j = from(low.y); j <= to(high.y); ++j)
                    for (std::int32_t cell = from(low.x); cell <= to(high.x); ++cell) {
                        const Cell candidate{cell, j, k};
                        if (std::abs(dot(n, pointOf(candidate) - a)) <= spacing)
                            candidates.push_back(candidate);
                    }
        }
    }
    // Step 4: ordered by z, then y, then x, each once.
    const auto zyx = [](const Cell& p, const Cell& q) {
        return std::tie(p[2], p[1], p[0]) < std::tie(q[2], q[1], q[0]);
    };
    std::sort(candidates.begin(), candidates.end(), zyx);
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    // Step 3: a candidate is a probe where the level's tree says it is empty.
    ubundle::LightProbes out;
    out.spacing = PROBE_SPACING;
    for (const Cell& cell : candidates)
        if (isEmpty(level, pointOf(cell))) out.probes.push_back(ubundle::LightProbe{cell, {}});

    // SS 4.7: each probe gathered on its own, into its own slot.
    const SurfaceRays rays(geometry);
    const std::size_t batches = (out.probes.size() + PROBES_PER_JOB - 1) / PROBES_PER_JOB;
    const std::size_t threw = jobs.parallelFor(batches, [&](std::size_t job) {
        const std::size_t end = std::min(out.probes.size(), (job + 1) * PROBES_PER_JOB);
        for (std::size_t p = job * PROBES_PER_JOB; p < end; ++p) {
            ubundle::LightProbe& probe = out.probes[p];
            const std::array<Rgb, 6> cube = gatherProbe(pointOf(probe.cell), rays, geometry, lights, albedo);
            for (std::size_t face = 0; face < 6; ++face)
                probe.cube[face] = {static_cast<float>(cube[face].r), static_cast<float>(cube[face].g),
                                    static_cast<float>(cube[face].b)};
        }
    });
    // A partly gathered section is a wrong bundle presented as a good one.
    if (threw != 0)
        return fail(ErrorCode::Unknown,
                    "light probes: " + std::to_string(threw) + " jobs threw while gathering");
    return out;
}

} // namespace uta::ubake
