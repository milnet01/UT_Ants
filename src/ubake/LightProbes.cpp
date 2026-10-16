// Bounced light, baked into probes --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.4 to SS 4.7. LightProbes.h
// says what is baked, and why it is the same at any worker count.

#include "ubake/LightProbes.h"

#include "ubake/Actors.h"
#include "ubundle/Sky.h"

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
constexpr std::uint32_t PF_UNLIT = 0x400000; // UTA-0161

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

bool isFinite(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
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

Rgb scaled(const Rgb& c, double k) noexcept { return {c.r * k, c.g * k, c.b * k}; }

/// SS 4.7 steps 1-4, its steps numbered as there. `fromSky` marks a ray
/// already sent on from the sky view (SS 4.12 item 2).
std::optional<SurfaceHit> surfaceFrom(const Vec3& p, const Vec3& w, const SurfaceRays& rays,
                                      const ubundle::Geometry& geometry, const std::optional<Vec3>& sky,
                                      bool fromSky, const std::optional<ubundle::PaintedSky>& painted) {
    const std::optional<SurfaceRays::Hit> hit = rays.first(p, w);
    if (!hit) return std::nullopt;                                         // 1
    const ubundle::GeometryBatch& batch = batchOf(geometry, hit->triangle);
    Vec3 n = normalOf(geometry, hit->triangle);                             // 2
    if (dot(w, n) >= 0) {                                                   // 3
        if ((batch.polyFlags & PF_TWO_SIDED) == 0) return std::nullopt;
        n = n * -1.0;
    }
    // 4. SS 4.12 item 2: the sky shows what the sky view sees that way, so the
    // ray carries on from there -- once; a sky the sky view itself meets is dark.
    if ((batch.polyFlags & PF_FAKE_BACKDROP) != 0) {
        // UTA-0350 SS 4.2: a sky the map only paints is met where it is painted.
        if (!sky && painted)
            return SurfaceHit{p + w * hit->t, n, &batch, false, ubundle::paintedSkyLight(*painted, {w.x, w.y, w.z})};
        if (!sky || fromSky) return std::nullopt;
        return surfaceFrom(*sky, w, rays, geometry, sky, true, painted);
    }
    return SurfaceHit{p + w * hit->t, n, &batch, fromSky, std::nullopt};
}

} // namespace

bool sunSeen(const Vec3& x, const Vec3& s, const SurfaceRays& rays, const SurfaceRays::Hole& hole) {
    return rays.firstHas(x, s, PF_FAKE_BACKDROP, hole);
}

float sunSeenFrom(const Vec3& p, const Vec3& s, const SurfaceRays& rays) {
    std::size_t seen = sunSeen(p, s, rays) ? 1 : 0;
    std::size_t cast = 1;
    const double within = detail::cosPolynomial(SUN_RADIUS); // LightModel.h's, not the platform's cos
    for (const Vec3& w : directions()) {
        if (dot(w, s) < within) continue;
        ++cast;
        if (sunSeen(p, w, rays)) ++seen;
    }
    return static_cast<float>(static_cast<double>(seen) / static_cast<double>(cast));
}

std::optional<SurfaceHit> surfaceAlong(const Vec3& p, const Vec3& w, const SurfaceRays& rays,
                                       const ubundle::Geometry& geometry, const std::optional<Vec3>& sky,
                                       const std::optional<ubundle::PaintedSky>& painted) {
    return surfaceFrom(p, w, rays, geometry, sky, false, painted);
}

namespace {

/// lightReaching, the sun left out where `withSun` is not set.
Rgb reaching(const Vec3& x, const Vec3& n, const SurfaceRays& rays, const std::vector<ubundle::Light>& lights,
             bool withSun) {
    Rgb e;
    for (const ubundle::Light& light : lights) {
        const bool sun = light.effect == ubundle::SUN_EFFECT;
        if (sun && !withSun) continue;
        const Rgb lit = lightAt(light, x, n);
        // A light that puts nothing here adds nothing either way, so its
        // shadow ray is not worth casting.
        if (lit.r == 0 && lit.g == 0 && lit.b == 0) continue;
        // UTA-0338 SS 4.4: the sun is seen through the sky, or not at all.
        if (sun) {
            if (!sunSeen(x + n * SHADOW_OFFSET, towardSun(light), rays)) continue;
        // Toward the point the light is lit from: a strip's nearest (UTA-0162 SS 4.3).
        } else if (rays.blocked(x + n * SHADOW_OFFSET, litFrom(light, x))) {
            continue;
        }
        e.r += lit.r;
        e.g += lit.g;
        e.b += lit.b;
    }
    return e;
}

} // namespace

Rgb lightReaching(const Vec3& x, const Vec3& n, const SurfaceRays& rays,
                  const std::vector<ubundle::Light>& lights) {
    return reaching(x, n, rays, lights, true);
}

Rgb reflectanceOf(const Rgb& albedo) noexcept {
    const auto reflects = [](double albedoChannel) {
        return std::min(REFLECTANCE_SCALE * albedoChannel, ALBEDO_CAP);
    };
    return {reflects(albedo.r), reflects(albedo.g), reflects(albedo.b)};
}

Rgb sentFrom(const SurfaceHit& hit, const SurfaceRays& rays, const std::vector<ubundle::Light>& lights,
             const AlbedoLookup& albedo, const OwnLightLookup& own) {
    // UTA-0350 SS 4.2: the painted sky sends its own light, at the scale a
    // sky's picture does (SS 4.12 item 2), and never the sun's.
    if (hit.paintedSky) {
        const std::array<float, 3>& c = *hit.paintedSky;
        return scaled(Rgb{c[0], c[1], c[2]}, REFLECTANCE_SCALE);
    }
    const ubundle::GeometryBatch& batch = *hit.batch;
    // UTA-0161: an unlit liquid -- acid, waste, lava -- shows its picture at
    // full brightness and sends that on. Other unlit surfaces do not: SS 8
    // rejects it, a map made fullbright for its look flooding its neighbours.
    // SS 4.12 item 1: a picture shown unlit scales as emission does, uncapped.
    // SS 4.12 item 2 (user, 2026-10-08): so does any unlit surface met through
    // the sky view, since a sky's picture is the light it gives. Met in the
    // level itself, SS 8's rejection stands.
    const OwnLight mine = own ? own(batch.material) : OwnLight{};
    if ((batch.polyFlags & PF_UNLIT) != 0 && (mine.unlitGlows || hit.viaSky))
        return scaled(albedo(batch.material), REFLECTANCE_SCALE);

    // UTA-0338 SS 4.4: a ray sent on from the sky view never reaches the sun --
    // the sky the renderer captures from there holds no disc.
    const Rgb e = reaching(hit.at, hit.normal, rays, lights, !hit.viaSky); // 5
    // 6. UTA-0253: the surface sends on the light it shows, not the light's
    // own value -- so a probe holds light as scene.frag adds it, after the power.
    // SS 4.12 item 1: at REFLECTANCE_SCALE times its stored albedo, capped.
    const Rgb k = reflectanceOf(albedo(batch.material));
    Rgb sent{k.r * shownLight(e.r), k.g * shownLight(e.g), k.b * shownLight(e.b)};
    // UTA-0161: and a glowing one adds its emission, as scene.frag adds its
    // emit map -- scaled as a picture shown unlit is, uncapped.
    const Rgb glow = scaled(mine.emission, REFLECTANCE_SCALE);
    sent.r += glow.r;
    sent.g += glow.g;
    sent.b += glow.b;
    return sent;
}

std::vector<ubundle::Light> bakedLights(const std::vector<ubundle::Light>& lights,
                                        const ubundle::Placements& placements) {
    static const std::vector<ubundle::PropertyRecord> none;
    std::vector<ubundle::Light> out;
    for (const ubundle::Light& light : lights) {
        // An absorbed strip light is lit by its row's leader (UTA-0162 SS 4.3).
        if (light.type == LT_BACKDROP_LIGHT || light.specialLit || light.strip == ubundle::STRIP_ABSORBED)
            continue;
        // UTA-0338 SS 4.4: a sun is no actor; its caller adds it by hand.
        if (light.effect == ubundle::SUN_EFFECT) continue;
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

std::optional<ProbeReach> probeReachOf(const ubundle::Placements& placements) {
    constexpr std::string_view NAVIGATION_POINT = "engine.navigationpoint";
    std::optional<ProbeReach> reach;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        if (actorClass.path != NAVIGATION_POINT
            && std::find(actorClass.ancestry.begin(), actorClass.ancestry.end(), NAVIGATION_POINT)
                   == actorClass.ancestry.end())
            continue;
        const ubundle::PropertyRecord* const record = detail::resolvedRecord(
            "location", actor.properties, actorClass.defaults,
            [](const ubundle::PropertyRecord& r) { return r.kind == ubundle::ValueKind::Vector; });
        const auto* const at = record == nullptr ? nullptr : std::get_if<std::array<float, 3>>(&record->value);
        // An actor with no Location of its own sits at the origin, as UT99 places it.
        const Vec3 p = at == nullptr ? Vec3{0, 0, 0} : Vec3{(*at)[0], (*at)[1], (*at)[2]};
        if (!reach) {
            reach = ProbeReach{p, p};
            continue;
        }
        reach->low = {std::min(reach->low.x, p.x), std::min(reach->low.y, p.y), std::min(reach->low.z, p.z)};
        reach->high = {std::max(reach->high.x, p.x), std::max(reach->high.y, p.y), std::max(reach->high.z, p.z)};
    }
    if (reach) {
        const double m = PROBE_REACH_MARGIN;
        reach->low = {reach->low.x - m, reach->low.y - m, reach->low.z - m};
        reach->high = {reach->high.x + m, reach->high.y + m, reach->high.z + m};
    }
    return reach;
}

std::optional<ProbeReach> levelReachOf(const upkg::Model& model) {
    const Vec3 low{model.boundsMin.x, model.boundsMin.y, model.boundsMin.z};
    const Vec3 high{model.boundsMax.x, model.boundsMax.y, model.boundsMax.z};
    if (!model.boundsValid || !isFinite(low) || !isFinite(high)) return std::nullopt;
    const auto s = static_cast<double>(PROBE_SPACING);
    return ProbeReach{{low.x - s, low.y - s, low.z - s}, {high.x + s, high.y + s, high.z + s}};
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
                               const AlbedoLookup& albedo, const OwnLightLookup& own,
                               const std::optional<Vec3>& sky, const std::optional<ubundle::PaintedSky>& painted) {
    const std::vector<Vec3>& all = directions();
    std::vector<Rgb> radiance;
    radiance.reserve(all.size());
    for (const Vec3& w : all) {
        const std::optional<SurfaceHit> hit = surfaceAlong(p, w, rays, geometry, sky, painted);
        radiance.push_back(hit ? sentFrom(*hit, rays, lights, albedo, own) : Rgb{});
    }
    return cubeOf(radiance);
}

Result<ubundle::LightProbes> bakeLightProbes(const ubundle::Geometry& geometry,
                                             const ubundle::CollisionTree& level,
                                             const std::vector<ubundle::Light>& lights,
                                             const AlbedoLookup& albedo, JobSystem& jobs,
                                             const std::optional<ProbeReach>& reach,
                                             const OwnLightLookup& own, const std::optional<Vec3>& sky,
                                             const std::optional<ubundle::PaintedSky>& painted) {
    const auto spacing = static_cast<double>(PROBE_SPACING);
    const auto pointOf = [spacing](const Cell& cell) {
        return Vec3{cell[0] * spacing, cell[1] * spacing, cell[2] * spacing};
    };

    // SS 4.6 steps 1 and 2: each non-sky triangle's grown box, cut to the
    // lattice points within one spacing of its plane and inside `reach`. The
    // box is clipped before the walk, so a triangle far larger than the play
    // area costs its share of the reach and no more (UTA-0212). UTA-0375: the
    // box is clipped to PROBE_WORLD_LIMIT too, so no point is too far out.
    const double limit = PROBE_WORLD_LIMIT;
    const Vec3 reachLow = reach ? Vec3{std::fmax(reach->low.x, -limit), std::fmax(reach->low.y, -limit),
                                       std::fmax(reach->low.z, -limit)}
                                : Vec3{-limit, -limit, -limit};
    const Vec3 reachHigh = reach ? Vec3{std::fmin(reach->high.x, limit), std::fmin(reach->high.y, limit),
                                        std::fmin(reach->high.z, limit)}
                                 : Vec3{limit, limit, limit};
    std::vector<Cell> candidates;
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        if ((batch.polyFlags & PF_FAKE_BACKDROP) != 0) continue;
        for (std::uint32_t i = batch.firstIndex; i + 3 <= batch.firstIndex + batch.indexCount;
             i += 3) {
            const Vec3 a = positionOf(geometry, geometry.indices[i]);
            const Vec3 b = positionOf(geometry, geometry.indices[i + 1]);
            const Vec3 c = positionOf(geometry, geometry.indices[i + 2]);
            const Vec3 n = normalOf(geometry, i / 3);
            // A coordinate or normal that is not finite seeds no candidate.
            if (!isFinite(a) || !isFinite(b) || !isFinite(c) || !isFinite(n)) continue;
            const Vec3 low{std::max(std::min({a.x, b.x, c.x}) - spacing, reachLow.x),
                           std::max(std::min({a.y, b.y, c.y}) - spacing, reachLow.y),
                           std::max(std::min({a.z, b.z, c.z}) - spacing, reachLow.z)};
            const Vec3 high{std::min(std::max({a.x, b.x, c.x}) + spacing, reachHigh.x),
                            std::min(std::max({a.y, b.y, c.y}) + spacing, reachHigh.y),
                            std::min(std::max({a.z, b.z, c.z}) + spacing, reachHigh.z)};
            // UTA-0375: along the axis the plane faces most, walk only the
            // cells near its slab, so a sloped triangle costs its area and not
            // its box's volume. The slab is widened by a cell either side and
            // every cell still meets the distance test, so the candidates are
            // the whole box's.
            const std::size_t d = std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z) ? 0
                                  : std::abs(n.y) >= std::abs(n.z)                                ? 1
                                                                                                  : 2;
            const std::size_t u = (d + 1) % 3;
            const std::size_t v = (d + 2) % 3;
            const double nd = component(n, d);
            const double offset = dot(n, a);
            const auto from = [spacing](double at) { return static_cast<std::int32_t>(std::ceil(at / spacing)); };
            const auto to = [spacing](double at) { return static_cast<std::int32_t>(std::floor(at / spacing)); };
            const std::int32_t firstD = from(component(low, d));
            const std::int32_t lastD = to(component(high, d));
            for (std::int32_t cu = from(component(low, u)); cu <= to(component(high, u)); ++cu)
                for (std::int32_t cv = from(component(low, v)); cv <= to(component(high, v)); ++cv) {
                    std::int32_t slabFirst = firstD;
                    std::int32_t slabLast = lastD;
                    if (nd != 0) {
                        // n_d t + r within spacing of 0, r the rest of n . p - n . a.
                        const double r = component(n, u) * cu * spacing + component(n, v) * cv * spacing - offset;
                        const double t0 = (-spacing - r) / nd;
                        const double t1 = (spacing - r) / nd;
                        const double first = std::floor(std::min(t0, t1) / spacing) - 1;
                        const double last = std::ceil(std::max(t0, t1) / spacing) + 1;
                        if (first > slabFirst) slabFirst = static_cast<std::int32_t>(std::min<double>(first, lastD + 1.0));
                        if (last < slabLast) slabLast = static_cast<std::int32_t>(std::max<double>(last, firstD - 1.0));
                    }
                    for (std::int32_t cd = slabFirst; cd <= slabLast; ++cd) {
                        Cell candidate{};
                        candidate[d] = cd;
                        candidate[u] = cu;
                        candidate[v] = cv;
                        if (std::abs(dot(n, pointOf(candidate) - a)) <= spacing)
                            candidates.push_back(candidate);
                    }
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

    // UTA-0338 SS 4.4: where a sun is lit, what share of it each probe sees.
    const auto sun = std::ranges::find(lights, ubundle::SUN_EFFECT, &ubundle::Light::effect);
    if (sun != lights.end()) out.sunSeen.assign(out.probes.size(), 0.0f);

    // SS 4.7: each probe gathered on its own, into its own slot.
    const SurfaceRays rays(geometry);
    const std::size_t batches = (out.probes.size() + PROBES_PER_JOB - 1) / PROBES_PER_JOB;
    const std::size_t threw = jobs.parallelFor(batches, [&](std::size_t job) {
        const std::size_t end = std::min(out.probes.size(), (job + 1) * PROBES_PER_JOB);
        for (std::size_t p = job * PROBES_PER_JOB; p < end; ++p) {
            ubundle::LightProbe& probe = out.probes[p];
            const std::array<Rgb, 6> cube = gatherProbe(pointOf(probe.cell), rays, geometry, lights, albedo, own, sky, painted);
            for (std::size_t face = 0; face < 6; ++face)
                probe.cube[face] = {static_cast<float>(cube[face].r), static_cast<float>(cube[face].g),
                                    static_cast<float>(cube[face].b)};
            if (sun != lights.end()) out.sunSeen[p] = sunSeenFrom(pointOf(probe.cell), towardSun(*sun), rays);
        }
    });
    // A partly gathered section is a wrong bundle presented as a good one.
    if (threw != 0)
        return fail(ErrorCode::Unknown,
                    "light probes: " + std::to_string(threw) + " jobs threw while gathering");
    return out;
}

} // namespace uta::ubake
