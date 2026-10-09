// The baked shadow mask -- docs/specs/UTA-0326-baked-shadow-mask.md SS 4.3
// and SS 4.4. ShadowMask.h says what is baked, and why it is the same at any
// worker count.

#include "ubake/ShadowMask.h"

#include "ubake/Charts.h"
#include "ubake/Install.h"
#include "ubake/SurfaceRays.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace uta::ubake {
namespace {

using charts::Chart;

/// SS 4.3: the occluders are what the shadow pass draws, so PF_FakeBackdrop
/// lets light through as well as LIGHT_PASSES_FLAGS.
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x00000080u;
constexpr std::uint32_t PF_MASKED = 0x00000002u;
constexpr std::uint32_t PF_TWO_SIDED = 0x00000100u;

/// UT99's ELightEffect values with no incidence term (UTA-0156).
constexpr std::uint8_t LE_NON_INCIDENCE = 13;
constexpr std::uint8_t LE_CYLINDER = 17;

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

/// One light the mask pairs: a litDirectly light of LITE or of LAMP.
struct Site {
    std::uint32_t index = 0; ///< into LITE, or past it into LAMP (UTA-0256 SS 4.2)
    Vec3 from{}, span{};     ///< litFrom's segment: its location, or a leader's strip
    Vec3 centre{};
    double bound = 0;        ///< its sphere: lightRadius grown by half the strip
    bool incidence = true;
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
    // AActor::WorldLightRadius, as light.glsl's lightRadius.
    site.bound = 25.0 * (light.radius + 1) + 0.5 * length(site.span);
    site.incidence = light.effect != LE_NON_INCIDENCE && light.effect != LE_CYLINDER;
    return site;
}

/// SS 4.3 step 2: the point `site` is lit from at `o`, as light.glsl's litFrom.
Vec3 litFrom(const Site& site, const Vec3& o) noexcept {
    const double ss = dot(site.span, site.span);
    if (ss == 0) return site.from;
    return site.from + site.span * std::clamp(dot(o - site.from, site.span) / ss, 0.0, 1.0);
}

double sineOf(std::int32_t angle) noexcept { return std::sin(angle * 2.0 * std::numbers::pi / 65536.0); }
double cosineOf(std::int32_t angle) noexcept { return std::cos(angle * 2.0 * std::numbers::pi / 65536.0); }

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
        keys[0];
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

/// SS 4.3 and SS 4.4: every pair of one lit chart, ascending by light.
/// `bothSides`: scene.frag lights the chart from whichever side it is seen,
/// so a light on either side pairs, and its rays leave from that light's side.
std::vector<Baked> bakeChart(const Chart& chart, const ubundle::Geometry& geometry, const std::vector<Site>& sites,
                             const std::vector<Box>& movers, const SurfaceRays& rays, const Holes& holes,
                             double texelSize, bool bothSides) {
    std::vector<Baked> out;
    const charts::Outline outline = charts::outlineOf(chart, geometry);
    const double lowest = *std::min_element(outline.heights.begin(), outline.heights.end());
    Box polygon;
    for (std::uint32_t k = chart.first; k <= chart.last; ++k) polygon.add(charts::positionOf(geometry, k));
    const SurfaceRays::Hole hole = std::cref(holes);
    for (const Site& site : sites) {
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
        pair.texels.resize(std::size_t{chart.w} * chart.h);
        bool allLit = true, allDark = true;
        for (std::uint32_t j = 0; j < chart.h; ++j)
            for (std::uint32_t i = 0; i < chart.w; ++i) {
                int visible = 0;
                for (const double du : {0.25, 0.75})
                    for (const double dv : {0.25, 0.75}) {
                        Vec3 origin = charts::originAt(chart, outline, static_cast<double>(chart.loU + i) + du,
                                                       static_cast<double>(chart.loV + j) + dv, texelSize,
                                                       bothSides ? 0.0 : SHADOW_MASK_LIFT);
                        if (bothSides) {
                            const double side = dot(litFrom(site, origin) - origin, chart.n) < 0 ? -1.0 : 1.0;
                            origin = origin + chart.n * (side * SHADOW_MASK_LIFT);
                        }
                        if (!rays.blocked(origin, litFrom(site, origin), hole)) ++visible;
                    }
                const auto value = static_cast<std::uint8_t>(std::lround(255.0 * visible / 4.0));
                pair.texels[std::size_t{j} * chart.w + i] = value;
                allLit = allLit && value == 255;
                allDark = allDark && value == 0;
            }
        if (allDark) continue;
        if (allLit) pair.texels.clear();
        Box between = polygon;
        between.add(site.centre);
        for (const Box& mover : movers)
            if (mover.meets(between)) pair.moverReach = 1;
        out.push_back(std::move(pair));
    }
    return out;
}

/// SS 4.3: shelf packing at `width`, tallest first. The height used, or 0 when
/// a rectangle is wider than `width`.
std::uint64_t shelve(const std::vector<std::pair<const Chart*, Baked*>>& order, std::uint32_t width) {
    std::uint64_t x = 0, shelfY = 0, shelfH = 0;
    for (const auto& [chart, pair] : order) {
        if (chart->w > width) return 0;
        if (x + chart->w > width) {
            shelfY += shelfH;
            x = 0;
            shelfH = 0;
        }
        pair->x = static_cast<std::uint32_t>(x);
        pair->y = static_cast<std::uint32_t>(shelfY);
        x += chart->w;
        shelfH = std::max<std::uint64_t>(shelfH, chart->h);
    }
    return shelfY + shelfH;
}

} // namespace

Result<ubundle::ShadowMask> bakeShadowMask(const ubundle::Bundle& bundle, JobSystem& jobs, const Cutouts& cutouts) {
    return bakeShadowMask(bundle, jobs, SHADOW_MASK_TEXEL_SIZE, cutouts);
}

Result<ubundle::ShadowMask> bakeShadowMask(const ubundle::Bundle& bundle, JobSystem& jobs, float texelSize,
                                           const Cutouts& cutouts) {
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
    const std::vector<Box> movers = moverReaches(bundle);
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

    std::vector<std::vector<Baked>> baked;
    std::uint32_t width = 0;
    std::uint64_t height = 0;
    for (;;) {
        if (texelSize > SHADOW_MASK_TEXEL_CEILING)
            return fail(ErrorCode::MalformedData,
                        "shadow mask: the level does not fit a " + std::to_string(ubundle::SHADOW_MASK_ATLAS_LIMIT)
                            + "-texel atlas at " + std::to_string(SHADOW_MASK_TEXEL_CEILING) + " units a texel");
        charts::sizeCharts(all, geometry, texelSize);
        // A chart's sides are u16 on the wire.
        const bool fitsWire = std::all_of(lit.begin(), lit.end(), [](const Chart* c) {
            return c->w <= std::numeric_limits<std::uint16_t>::max() && c->h <= std::numeric_limits<std::uint16_t>::max();
        });
        if (fitsWire) {
            // SS 4.3: each chart's pairs on their own, into their own slot.
            baked.assign(lit.size(), {});
            const std::size_t batches = (lit.size() + CHARTS_PER_JOB - 1) / CHARTS_PER_JOB;
            const double size = texelSize;
            const std::size_t threw = jobs.parallelFor(batches, [&](std::size_t job) {
                const std::size_t end = std::min(lit.size(), (job + 1) * CHARTS_PER_JOB);
                for (std::size_t c = job * CHARTS_PER_JOB; c < end; ++c)
                    baked[c] = bakeChart(*lit[c], geometry, sites, movers, rays, holes, size, bothSidesAt[lit[c]->first]);
            });
            // A partly baked mask is a wrong bundle presented as a good one.
            if (threw != 0)
                return fail(ErrorCode::Unknown, "shadow mask: " + std::to_string(threw) + " jobs threw while baking");

            // SS 4.3: pack the pairs carrying texels -- tallest first, then by
            // chart, then by light, which the stable sort keeps.
            std::vector<std::pair<const Chart*, Baked*>> order;
            std::uint64_t area = 0;
            for (std::size_t c = 0; c < lit.size(); ++c)
                for (Baked& pair : baked[c])
                    if (!pair.texels.empty()) {
                        order.emplace_back(lit[c], &pair);
                        area += std::uint64_t{lit[c]->w} * lit[c]->h;
                    }
            std::stable_sort(order.begin(), order.end(),
                             [](const auto& a, const auto& b) { return a.first->h > b.first->h; });
            width = 64;
            while (std::uint64_t{width} * width < area && width < ubundle::SHADOW_MASK_ATLAS_LIMIT) width *= 2;
            for (;;) {
                height = shelve(order, width);
                if (height != 0 && height <= width) break;
                if (order.empty() || width == ubundle::SHADOW_MASK_ATLAS_LIMIT) break;
                width *= 2;
            }
            if (order.empty() || (height != 0 && height <= ubundle::SHADOW_MASK_ATLAS_LIMIT)) break;
        }
        texelSize *= 2;
    }

    ubundle::ShadowMask out;
    out.texelSize = texelSize;
    out.width = width;
    out.height = static_cast<std::uint32_t>(std::max<std::uint64_t>((height + 3) / 4 * 4, 4));
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
