// The baked shadow mask -- docs/specs/UTA-0326-baked-shadow-mask.md SS 4.3
// and SS 4.4. ShadowMask.h says what is baked, and why it is the same at any
// worker count.

#include "ubake/ShadowMask.h"

#include "ubake/Charts.h"
#include "ubake/Geometry.h"
#include "ubake/Install.h"
#include "ubake/LightModel.h"
#include "ubake/LightProbes.h"
#include "ubake/SurfaceRays.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace uta::ubake {
namespace {

using charts::Chart;

/// Charts to one job. Any size gives the same bytes.
constexpr std::size_t CHARTS_PER_JOB = 16;

/// SS 4.4: a mover's box is widened by this much, in UT units, so a sine that
/// rounds differently on another compiler cannot flip a pair's mark.
constexpr double MOVER_PAD = 1;

constexpr double INFINITE = std::numeric_limits<double>::infinity();

struct Box {
    Vec3 min{INFINITE, INFINITE, INFINITE}, max{-INFINITE, -INFINITE, -INFINITE};

    void add(const Vec3& p) noexcept {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    [[nodiscard]] bool meets(const Box& o) const noexcept {
        return min.x <= o.max.x && o.min.x <= max.x && min.y <= o.max.y && o.min.y <= max.y && min.z <= o.max.z
               && o.min.z <= max.z;
    }
};

/// One light the mask pairs: a litDirectly light of LITE or of LAMP, or the sun.
struct Site {
    std::uint32_t index = 0; ///< into LITE, or past it into LAMP (UTA-0256 SS 4.2), or past both the sun's
    Vec3 from{}, span{};     ///< litFrom's segment: its location, or a leader's strip
    Vec3 centre{};
    double bound = 0;        ///< its sphere: lightRadius grown by half the strip
    double reach = 0;        ///< lightRadius, which weighs a strip's points
    bool incidence = true;
    bool special = false;    ///< UTA-0438 SS 4.2: it pairs with PF_SpecialLit charts only
    std::optional<Vec3> sun; ///< UTA-0338 SS 4.4: toward the sun, for the sun's site
};

Vec3 toVec(const std::array<float, 3>& p) noexcept { return {p[0], p[1], p[2]}; }

Site siteOf(std::uint32_t index, const ubundle::Light& light) {
    Site site;
    site.index = index;
    site.from = toVec(light.location);
    if (light.strip == ubundle::STRIP_LEADER) {
        site.from = toVec(light.stripFrom);
        site.span = toVec(light.stripTo) - site.from;
    }
    site.centre = site.from + site.span * 0.5;
    site.reach = lightRadius(light.radius);
    site.bound = site.reach + 0.5 * length(site.span);
    site.incidence = light.effect != ubundle::LE_NON_INCIDENCE && light.effect != ubundle::LE_CYLINDER;
    site.special = light.specialLit;
    return site;
}

/// SS 4.3 step 2: the point `site` is lit from at `o`. LightModel.h's litFrom
/// is its twin on a Light, and light.glsl's on the GPU; all three must agree.
Vec3 litFrom(const Site& site, const Vec3& o) noexcept {
    const double ss = dot(site.span, site.span);
    if (ss == 0) return site.from;
    return site.from + site.span * std::clamp(dot(o - site.from, site.span) / ss, 0.0, 1.0);
}

/// SS 4.3 step 2: how much of `site` the origin `o` sees, 0 to 1. A point
/// light is seen or not. A strip leader is seen in the share of
/// SHADOW_MASK_STRIP_SAMPLES points along its segment that `o` sees, each
/// weighed by UT99's falloff from it. The first sits (`offset` + 0.5) / 4
/// of a step in, so the four samples of a texel stagger across one step
/// (UTA-0381). Looking at the nearest point alone, that point slid along
/// the strip as `o` moved, and a straight edge cast a curved shadow. Where
/// no point reaches `o`, the nearest point decides.
double seenShare(const Site& site, const Vec3& o, int offset, const SurfaceRays& rays,
                 const SurfaceRays::Hole& hole) {
    const Vec3 nearest = litFrom(site, o);
    if (dot(site.span, site.span) != 0) {
        double seen = 0, total = 0;
        for (int k = 0; k < SHADOW_MASK_STRIP_SAMPLES; ++k) {
            const double t = (k + (offset + 0.5) / 4.0) / SHADOW_MASK_STRIP_SAMPLES;
            const Vec3 p = site.from + site.span * t;
            const double weight = falloff(length(p - o), site.reach);
            if (weight == 0) continue;
            total += weight;
            if (!rays.blocked(o, p, hole)) seen += weight;
        }
        if (total > 0) return seen / total;
    }
    return rays.blocked(o, nearest, hole) ? 0.0 : 1.0;
}

/// UTA-0119 SS 4.5: location + postScale * (Y P R q).
Vec3 placed(const Vec3& location, const std::array<std::int32_t, 3>& rotation, const std::array<float, 3>& scale,
            const Vec3& q) noexcept {
    const double cp = cosineOf(rotation[0]), sp = sineOf(rotation[0]);
    const double cy = cosineOf(rotation[1]), sy = sineOf(rotation[1]);
    const double cr = cosineOf(rotation[2]), sr = sineOf(rotation[2]);
    const Vec3 r{q.x, cr * q.y - sr * q.z, sr * q.y + cr * q.z};    // roll, about X
    const Vec3 p{cp * r.x - sp * r.z, r.y, sp * r.x + cp * r.z};    // pitch
    const Vec3 y{cy * p.x - sy * p.y, sy * p.x + cy * p.y, p.z};    // yaw, about Z
    return location + Vec3{scale[0] * y.x, scale[1] * y.y, scale[2] * y.z};
}

/// SS 4.4: each mover's reach -- its shape placed at its own location and at
/// each key its placement carries, as offsets from its base.
std::vector<Box> moverReaches(const ubundle::Bundle& bundle) {
    std::vector<Box> out;
    if (!bundle.movers) return out;
    for (const ubundle::MoverShape& mover : *bundle.movers) {
        std::map<std::uint32_t, std::pair<Vec3, std::array<std::int32_t, 3>>> keys;
        keys.try_emplace(0); // the base position: the mover reaches from where it stands
        if (bundle.placements)
            for (const ubundle::ActorPlacement& actor : bundle.placements->actors) {
                if (actor.exportIndex != mover.exportIndex) continue;
                for (const ubundle::PropertyRecord& record : actor.properties) {
                    const std::string name = detail::fold(record.name);
                    if (name == "keypos" && record.kind == ubundle::ValueKind::Vector)
                        keys[record.arrayIndex].first = toVec(std::get<std::array<float, 3>>(record.value));
                    else if (name == "keyrot" && record.kind == ubundle::ValueKind::Rotator)
                        keys[record.arrayIndex].second = std::get<std::array<std::int32_t, 3>>(record.value);
                }
            }
        Box box;
        const Vec3 base = toVec(mover.location);
        for (const auto& [index, key] : keys) {
            std::array<std::int32_t, 3> rotation = mover.rotation;
            for (std::size_t axis = 0; axis < 3; ++axis) rotation[axis] += key.second[axis];
            for (const ubundle::GeometryVertex& vertex : mover.geometry.vertices)
                box.add(placed(base + key.first, rotation, mover.postScale, toVec(vertex.position)));
        }
        if (box.min.x > box.max.x) continue; // a shape with no vertex reaches nothing
        box.min = box.min - Vec3{MOVER_PAD, MOVER_PAD, MOVER_PAD};
        box.max = box.max + Vec3{MOVER_PAD, MOVER_PAD, MOVER_PAD};
        out.push_back(box);
    }
    return out;
}

/// SS 4.3's alpha rule: whether a hit on `triangle` at weights (u, v) lands on a
/// texel of its masked material's cutout below MASK_THRESHOLD.
class Holes {
public:
    Holes(const ubundle::Geometry& geometry, const Cutouts& cutouts) : geometry_(geometry) {
        cutoutOf_.assign(geometry.indices.size() / 3, nullptr);
        for (const ubundle::GeometryBatch& batch : geometry.batches) {
            if ((batch.polyFlags & PF_MASKED) == 0) continue;
            const auto found = cutouts.find(batch.material);
            if (found == cutouts.end() || found->second.width == 0 || found->second.height == 0) continue;
            for (std::uint32_t i = batch.firstIndex; i + 3 <= batch.firstIndex + batch.indexCount; i += 3)
                cutoutOf_[i / 3] = &found->second;
        }
    }

    [[nodiscard]] bool operator()(std::size_t triangle, double u, double v) const {
        const Cutout* const cutout = cutoutOf_[triangle];
        if (cutout == nullptr) return false;
        const ubundle::GeometryVertex& a = geometry_.vertices[geometry_.indices[triangle * 3]];
        const ubundle::GeometryVertex& b = geometry_.vertices[geometry_.indices[triangle * 3 + 1]];
        const ubundle::GeometryVertex& c = geometry_.vertices[geometry_.indices[triangle * 3 + 2]];
        // Pan time 0: the stored coordinates, 1.0 a repeat.
        const double s = (1 - u - v) * a.u + u * b.u + v * c.u;
        const double t = (1 - u - v) * a.v + u * b.v + v * c.v;
        const auto texel = [](double coordinate, std::uint32_t size) {
            const double wrapped = coordinate - std::floor(coordinate);
            return std::min(static_cast<std::uint32_t>(wrapped * size), size - 1);
        };
        return cutout->solid[std::size_t{texel(t, cutout->height)} * cutout->width + texel(s, cutout->width)] == 0;
    }

private:
    const ubundle::Geometry& geometry_;
    std::vector<const Cutout*> cutoutOf_;
};

/// One pair as baked: its light, its mark, and its texels unless every one is
/// 255. A pair whose every texel is 0 is never kept.
struct Baked {
    std::uint32_t light = 0;
    std::uint8_t moverReach = 0;
    std::vector<std::uint8_t> texels; ///< w * h, row-major; empty when all lit
    std::uint32_t x = 0, y = 0;       ///< where packing put it
};

/// Whether a pair's texels came out all lit, all dark, or neither.
struct Spread {
    bool allLit = true, allDark = true;
};

/// SS 4.3 step 2: `pair`'s texels on `chart`, each the mean of four samples at
/// a quarter and three quarters of the texel, rounded to a byte. `seen(origin,
/// k)` is what share of the light the k-th sample's origin sees; where
/// `bothSides`, `origin` lies on the plane and `seen` lifts it toward the
/// light's side. The one texel loop of the sun's pairs and the lights'.
template <typename Seen>
Spread bakeTexels(const Chart& chart, const charts::Outline& outline, double texelSize, bool bothSides,
                  std::vector<std::uint8_t>& texels, Seen&& seen) {
    Spread spread;
    texels.resize(std::size_t{chart.w} * chart.h);
    for (std::uint32_t j = 0; j < chart.h; ++j)
        for (std::uint32_t i = 0; i < chart.w; ++i) {
            double visible = 0;
            int sample = 0;
            for (const double du : {0.25, 0.75})
                for (const double dv : {0.25, 0.75}) {
                    const Vec3 origin = charts::originAt(chart, outline, static_cast<double>(chart.loU + i) + du,
                                                         static_cast<double>(chart.loV + j) + dv, texelSize,
                                                         bothSides ? 0.0 : SHADOW_MASK_LIFT);
                    visible += seen(origin, sample++);
                }
            const auto value = static_cast<std::uint8_t>(std::lround(255.0 * visible / 4.0));
            texels[std::size_t{j} * chart.w + i] = value;
            spread.allLit = spread.allLit && value == 255;
            spread.allDark = spread.allDark && value == 0;
        }
    return spread;
}

/// UTA-0338 SS 4.4: the sun's pair with one chart, as any light's, its texels'
/// shares from meetsSkyFirst. None where the chart faces away from it or no texel
/// sees it. Its moverReach is 0 (SS 3).
std::optional<Baked> bakeSunPair(const Chart& chart, const charts::Outline& outline, const Site& site,
                                 const SurfaceRays& rays, const SurfaceRays::Hole& hole, double texelSize,
                                 bool bothSides) {
    const Vec3 s = *site.sun;
    const double facing = dot(s, chart.n);
    if (!bothSides && facing <= 0) return std::nullopt;
    if (facing == 0) return std::nullopt; // edge-on, from either side
    Baked pair;
    pair.light = site.index;
    const Spread spread = bakeTexels(chart, outline, texelSize, bothSides, pair.texels, [&](Vec3 origin, int) {
        if (bothSides) origin = origin + chart.n * ((facing < 0 ? -1.0 : 1.0) * SHADOW_MASK_LIFT);
        return meetsSkyFirst(origin, s, rays, hole) ? 1.0 : 0.0;
    });
    if (spread.allDark) return std::nullopt;
    if (spread.allLit) pair.texels.clear();
    return pair;
}

/// SS 4.3 and SS 4.4: every pair of one lit chart, ascending by light.
/// `bothSides`: scene.frag lights the chart from whichever side it is seen,
/// so a light on either side pairs, and its rays leave from that light's side.
/// `sunRays` holds backdrop surfaces as occluders, as meetsSkyFirst needs.
std::vector<Baked> bakeChart(const Chart& chart, const ubundle::Geometry& geometry, const std::vector<Site>& sites,
                             const std::vector<Box>& movers, const SurfaceRays& rays, const SurfaceRays* sunRays,
                             const Holes& holes, double texelSize, bool bothSides) {
    std::vector<Baked> out;
    const charts::Outline outline = charts::outlineOf(chart, geometry);
    const double lowest = *std::min_element(outline.heights.begin(), outline.heights.end());
    Box polygon;
    for (std::uint32_t k = chart.first; k <= chart.last; ++k) polygon.add(charts::positionOf(geometry, k));
    const SurfaceRays::Hole hole = std::cref(holes);
    for (const Site& site : sites) {
        // UTA-0438 SS 4.2: a PF_SpecialLit chart takes special-lit lights
        // only, and they light no other chart. The sun is not one.
        if (site.special != chart.special) continue;
        if (site.sun) {
            if (std::optional<Baked> pair = bakeSunPair(chart, outline, site, *sunRays, hole, texelSize, bothSides))
                out.push_back(std::move(*pair));
            continue;
        }
        // The sphere meets the polygon: its centre's nearest point of it.
        const Vec3 nearest = charts::originAt(chart, outline, dot(site.centre, chart.u) / texelSize,
                                              dot(site.centre, chart.v) / texelSize, texelSize, 0.0);
        if (length(site.centre - nearest) > site.bound) continue;
        // In front of the plane somewhere, unless the light has no incidence term.
        if (site.incidence && !bothSides && dot(site.from, chart.n) <= lowest
            && dot(site.from + site.span, chart.n) <= lowest)
            continue;
        Baked pair;
        pair.light = site.index;
        const Spread spread =
            bakeTexels(chart, outline, texelSize, bothSides, pair.texels, [&](Vec3 origin, int sample) {
                if (bothSides) {
                    const double side = dot(litFrom(site, origin) - origin, chart.n) < 0 ? -1.0 : 1.0;
                    origin = origin + chart.n * (side * SHADOW_MASK_LIFT);
                }
                return seenShare(site, origin, sample, rays, hole);
            });
        if (spread.allDark) continue;
        if (spread.allLit) pair.texels.clear();
        Box between = polygon;
        between.add(site.centre);
        for (const Box& mover : movers)
            if (mover.meets(between)) pair.moverReach = 1;
        out.push_back(std::move(pair));
    }
    return out;
}

/// What every pair is baked against, the same at every texel size.
struct Scene {
    const ubundle::Geometry& geometry;
    const std::vector<Site>& sites;
    const std::vector<Box>& movers;
    const SurfaceRays& rays;
    const SurfaceRays* sunRays;
    const Holes& holes;
    const std::vector<bool>& bothSidesAt; ///< by vertex
};

/// Every lit chart's pairs at one texel size, and the atlas they pack into.
struct Packed {
    std::vector<std::vector<Baked>> baked; ///< by lit chart
    charts::AtlasSize atlas;
    bool empty = false; ///< no pair carries texels: every one is all lit
};

/// SS 4.3: bake each chart's pairs at `texelSize`, then shelf-pack the pairs
/// that carry texels -- tallest first, then by chart, then by light, which the
/// stable sort keeps.
Result<Packed> bakeAndPack(const std::vector<Chart*>& lit, const Scene& scene, double texelSize,
                           std::uint32_t atlasLimit, JobSystem& jobs) {
    Packed packed;
    // Each chart's pairs on their own, into their own slot.
    packed.baked.assign(lit.size(), {});
    const std::size_t jobCount = (lit.size() + CHARTS_PER_JOB - 1) / CHARTS_PER_JOB;
    const std::size_t threw = jobs.parallelFor(jobCount, [&](std::size_t job) {
        const std::size_t end = std::min(lit.size(), (job + 1) * CHARTS_PER_JOB);
        for (std::size_t c = job * CHARTS_PER_JOB; c < end; ++c)
            packed.baked[c] = bakeChart(*lit[c], scene.geometry, scene.sites, scene.movers, scene.rays, scene.sunRays,
                                        scene.holes, texelSize, scene.bothSidesAt[lit[c]->first]);
    });
    // A partly baked mask is a wrong bundle presented as a good one.
    if (threw != 0) return fail(ErrorCode::Unknown, "shadow mask: " + std::to_string(threw) + " jobs threw while baking");

    std::vector<std::pair<const Chart*, Baked*>> order;
    std::uint64_t area = 0;
    for (std::size_t c = 0; c < lit.size(); ++c)
        for (Baked& pair : packed.baked[c])
            if (!pair.texels.empty()) {
                order.emplace_back(lit[c], &pair);
                area += std::uint64_t{lit[c]->w} * lit[c]->h;
            }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first->h > b.first->h; });
    packed.empty = order.empty();
    packed.atlas = charts::widthFor(area, atlasLimit, packed.empty, [&order](std::uint32_t width) {
        return charts::shelve(
            order.size(), width, 0, [&order](std::size_t k) -> const Chart& { return *order[k].first; },
            [&order](std::size_t k, std::uint32_t x, std::uint32_t y) {
                order[k].second->x = x;
                order[k].second->y = y;
            });
    });
    return packed;
}

} // namespace

Result<ubundle::ShadowMask> bakeShadowMask(const ubundle::Bundle& bundle, JobSystem& jobs, const Cutouts& cutouts) {
    return bakeShadowMask(bundle, jobs, SHADOW_MASK_TEXEL_SIZE, cutouts);
}

Result<ubundle::ShadowMask> bakeShadowMask(const ubundle::Bundle& bundle, JobSystem& jobs, float texelSize,
                                           const Cutouts& cutouts, std::uint32_t atlasLimit) {
    if (!bundle.geometry || !bundle.lights)
        return fail(ErrorCode::InvalidArgument, "shadow mask: the bundle has no GEOM or no LITE");
    // Coarsening doubles the texel until the level fits, which never ends on
    // a size that doubling cannot grow (UTA-0218).
    if (!(texelSize > 0.0f) || !std::isfinite(texelSize))
        return fail(ErrorCode::InvalidArgument,
                    "shadow mask: a texel size must be positive and finite, not " + std::to_string(texelSize));
    const ubundle::Geometry& geometry = *bundle.geometry;

    std::vector<Site> sites;
    for (std::uint32_t i = 0; i < bundle.lights->size(); ++i)
        if (ubundle::litDirectly((*bundle.lights)[i])) sites.push_back(siteOf(i, (*bundle.lights)[i]));
    // UTA-0256 SS 4.3 step 4: a lamp's light pairs as any light does, after
    // LITE's, and its fitting is in no ray set.
    if (bundle.lamps) {
        const auto first = static_cast<std::uint32_t>(bundle.lights->size());
        for (std::uint32_t k = 0; k < bundle.lamps->size(); ++k)
            if (ubundle::litDirectly((*bundle.lamps)[k].light))
                sites.push_back(siteOf(first + k, (*bundle.lamps)[k].light));
    }
    // UTA-0338 SS 4.4: the sun pairs after the lamps, seen through the sky, so
    // its rays need backdrop surfaces solid, as SMSK's own set has them not.
    std::optional<SurfaceRays> sunRays;
    if (bundle.sun) {
        Site site;
        site.index = static_cast<std::uint32_t>(bundle.lights->size() + (bundle.lamps ? bundle.lamps->size() : 0));
        site.sun = towardSun(ubundle::lightOfSun(*bundle.sun));
        sites.push_back(site);
        sunRays.emplace(geometry);
    }
    const std::vector<Box> movers = moverReaches(bundle);
    // SS 4.3: the occluders are what the shadow pass draws, so PF_FakeBackdrop
    // lets light through as well as LIGHT_PASSES_FLAGS.
    const SurfaceRays rays(geometry, LIGHT_PASSES_FLAGS | PF_FAKE_BACKDROP);
    const Holes holes(geometry, cutouts);

    std::vector<Chart> all = charts::chartsOf(geometry);
    std::vector<Chart*> lit;
    for (Chart& chart : all)
        if (chart.lit) lit.push_back(&chart);

    // SS 4.3: a two-sided chart is lit from both sides, but a liquid as its
    // front only (UTA-0215), as scene.frag lights them.
    std::vector<std::string_view> liquids;
    if (bundle.materials)
        for (const ubundle::MaterialRecord& material : *bundle.materials)
            if (material.liquid) liquids.push_back(material.id);
    std::vector<bool> bothSidesAt(geometry.vertices.size(), false);
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        if ((batch.polyFlags & PF_TWO_SIDED) == 0u
            || std::find(liquids.begin(), liquids.end(), batch.material) != liquids.end())
            continue;
        for (std::uint32_t i = batch.firstIndex; i < batch.firstIndex + batch.indexCount; ++i)
            bothSidesAt[geometry.indices[i]] = true;
    }

    // Coarsen the texel until the pairs pack into the atlas.
    const Scene scene{geometry, sites, movers, rays, sunRays ? &*sunRays : nullptr, holes, bothSidesAt};
    Packed packed;
    for (;;) {
        if (texelSize > SHADOW_MASK_TEXEL_CEILING)
            return fail(ErrorCode::MalformedData,
                        "shadow mask: the level does not fit a " + std::to_string(atlasLimit)
                            + "-texel atlas at " + std::to_string(SHADOW_MASK_TEXEL_CEILING) + " units a texel");
        charts::sizeCharts(all, geometry, texelSize);
        // A chart's sides are u16 on the wire.
        const bool fitsWire = std::all_of(lit.begin(), lit.end(), [](const Chart* c) {
            return c->w <= std::numeric_limits<std::uint16_t>::max() && c->h <= std::numeric_limits<std::uint16_t>::max();
        });
        if (fitsWire) {
            UTA_TRY(packed, bakeAndPack(lit, scene, texelSize, atlasLimit, jobs));
            // With no texels to place, any texel size fits.
            if (packed.empty || (packed.atlas.height != 0 && packed.atlas.height <= atlasLimit)) break;
        }
        texelSize *= 2;
    }
    const std::vector<std::vector<Baked>>& baked = packed.baked;

    ubundle::ShadowMask out;
    out.texelSize = texelSize;
    out.width = packed.atlas.width;
    // Rows to a multiple of four, as the occlusion atlas's. SMSK refuses a
    // height of 0, so a mask with nothing packed is four rows of zeros.
    out.height = static_cast<std::uint32_t>(std::max<std::uint64_t>((packed.atlas.height + 3) / 4 * 4, 4));
    out.texels.assign(std::size_t{out.width} * out.height, 0);
    out.vertexChart.assign(geometry.vertices.size(), ubundle::MASK_NO_CHART);
    out.vertexTexel.assign(geometry.vertices.size(), {0, 0});
    for (std::size_t c = 0; c < lit.size(); ++c) {
        const Chart& chart = *lit[c];
        ubundle::MaskChart record;
        record.firstPair = static_cast<std::uint32_t>(out.pairs.size());
        record.pairCount = static_cast<std::uint32_t>(baked[c].size());
        record.width = static_cast<std::uint16_t>(chart.w);
        record.height = static_cast<std::uint16_t>(chart.h);
        out.charts.push_back(record);
        for (const Baked& pair : baked[c]) {
            ubundle::MaskPair entry;
            entry.light = pair.light;
            entry.moverReach = pair.moverReach;
            if (pair.texels.empty()) {
                entry.x = entry.y = ubundle::MASK_ALL_LIT;
            } else {
                entry.x = static_cast<std::uint16_t>(pair.x);
                entry.y = static_cast<std::uint16_t>(pair.y);
                for (std::uint32_t j = 0; j < chart.h; ++j)
                    std::copy_n(pair.texels.begin() + std::ptrdiff_t{j} * chart.w, chart.w,
                                out.texels.begin() + static_cast<std::ptrdiff_t>((pair.y + j) * std::size_t{out.width} + pair.x));
            }
            out.pairs.push_back(entry);
        }
        // SS 4.2: each vertex's place in its chart's rectangle.
        for (std::uint32_t k = chart.first; k <= chart.last; ++k) {
            const Vec3 p = charts::positionOf(geometry, k);
            out.vertexChart[k] = static_cast<std::uint32_t>(c);
            out.vertexTexel[k] = {static_cast<float>(dot(p, chart.u) / texelSize - static_cast<double>(chart.loU)),
                                  static_cast<float>(dot(p, chart.v) / texelSize - static_cast<double>(chart.loV))};
        }
    }
    return out;
}

} // namespace uta::ubake
