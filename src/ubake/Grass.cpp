// The grass a bake plants -- docs/specs/UTA-0293-grass.md SS 4.1 to SS 4.3.

#include "ubake/Grass.h"

#include "ubake/Actors.h"
#include "ubake/Geometry.h"
#include "ubake/LightModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace uta::ubake {
namespace {

/// SS 4.1 item 2's flags: a surface drawn see-through, cut out, or not at all.
constexpr std::uint32_t NOT_GROUND = PF_MASKED | PF_TRANSLUCENT | PF_MODULATED | PF_FAKE_BACKDROP | PF_INVISIBLE;

/// A placed actor of this class or a descendant is a pickup -- SS 4.2.
constexpr std::string_view INVENTORY = "engine.inventory";

/// The words of SS 4.1 item 1. Ground, earth and dirt are left out: UT99 paints
/// grass olive and earth brown, close enough that no colour test parts them.
constexpr std::array<std::string_view, 5> GRASS_WORDS = {"gras", "grss", "lawn", "turf", "meadow"};

/// splitmix64's finaliser: every random choice is this over integers, so one
/// map plants the same clumps on every machine (INV-6).
[[nodiscard]] std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

[[nodiscard]] std::uint64_t hashOf(std::int64_t i, std::int64_t j, std::uint64_t salt) noexcept {
    return mix(mix(mix(static_cast<std::uint64_t>(i)) ^ static_cast<std::uint64_t>(j)) ^ salt);
}

/// [0, 1) from a hash's top 53 bits.
[[nodiscard]] double unitOf(std::uint64_t h) noexcept { return static_cast<double>(h >> 11) * 0x1.0p-53; }

[[nodiscard]] double smoothstep(double low, double high, double x) noexcept {
    const double t = std::clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

/// The density at texture coordinate (u, v), repeating, nearest texel.
[[nodiscard]] double densityAt(const GreenMap& map, double u, double v) noexcept {
    const auto texel = [](double t, std::uint32_t size) {
        const double wrapped = t - std::floor(t);
        return std::min(static_cast<std::uint32_t>(wrapped * size), size - 1);
    };
    const std::uint32_t x = texel(u, map.width);
    const std::uint32_t y = texel(v, map.height);
    return map.green[std::size_t{y} * map.width + x] / 255.0;
}

/// Where SS 4.2's pickups stand, as PLAC resolves their Location.
[[nodiscard]] std::vector<std::array<double, 2>> pickupsOf(const ubundle::Bundle& bundle) {
    std::vector<std::array<double, 2>> out;
    if (!bundle.placements) return out;
    const ubundle::Placements& placements = *bundle.placements;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        if (!ubundle::isOrDescendsFrom(actorClass, INVENTORY)) continue;
        const ubundle::PropertyRecord* const record =
            detail::resolvedRecord("location", ubundle::ValueKind::Vector, actor.properties, actorClass.defaults);
        const auto* const at = record == nullptr ? nullptr : std::get_if<std::array<float, 3>>(&record->value);
        // An actor with no Location of its own sits at the origin, as UT99 places it.
        out.push_back(at == nullptr ? std::array<double, 2>{0, 0} : std::array<double, 2>{(*at)[0], (*at)[1]});
    }
    return out;
}

[[nodiscard]] bool nearPickup(const std::vector<std::array<double, 2>>& pickups, double x, double y) noexcept {
    return std::ranges::any_of(pickups, [x, y](const std::array<double, 2>& p) {
        const double dx = x - p[0], dy = y - p[1];
        return dx * dx + dy * dy < PICKUP_CLEAR * PICKUP_CLEAR;
    });
}

/// SS 4.1 item 2: a material whose own look is not ground's.
[[nodiscard]] bool hasOwnLook(const ubundle::MaterialRecord& record) noexcept {
    return record.liquid.has_value() || record.flame.has_value() || record.fire.has_value() || record.pane;
}

/// One clump before it is sorted into cells, with the cell it falls in.
struct Sample {
    std::int64_t cellX = 0, cellY = 0;
    ubundle::GrassClump clump;
};

/// SS 4.3: the clumps sorted into GRASS_CELL squares of the plan, each cell's
/// bounds taking in BLADE_REACH every way. Within a cell the even seeds come
/// first, so High draws a run's head (SS 4.6, urender/Grass.h). Stable, so the
/// order is otherwise planting order.
[[nodiscard]] ubundle::Grass sortIntoCells(std::vector<Sample> samples) {
    std::ranges::stable_sort(samples, [](const Sample& a, const Sample& b) {
        return std::tuple{a.cellX, a.cellY, a.clump.seed % 2} < std::tuple{b.cellX, b.cellY, b.clump.seed % 2};
    });
    ubundle::Grass grass;
    grass.clumps.reserve(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const ubundle::GrassClump& clump = samples[i].clump;
        const bool opens = i == 0 || samples[i].cellX != samples[i - 1].cellX || samples[i].cellY != samples[i - 1].cellY;
        if (opens) grass.cells.push_back(ubundle::GrassCell{static_cast<std::uint32_t>(i), 0, clump.base, clump.base});
        ubundle::GrassCell& cell = grass.cells.back();
        ++cell.clumpCount;
        for (std::size_t k = 0; k < 3; ++k) {
            cell.low[k] = std::min(cell.low[k], clump.base[k]);
            cell.high[k] = std::max(cell.high[k], clump.base[k]);
        }
        grass.clumps.push_back(clump);
    }
    constexpr auto reach = static_cast<float>(BLADE_REACH);
    for (ubundle::GrassCell& cell : grass.cells) {
        cell.low[0] -= reach;
        cell.low[1] -= reach;
        cell.high[0] += reach;
        cell.high[1] += reach;
        cell.high[2] += reach;
    }
    return grass;
}

} // namespace

bool isGroundCover(std::string_view materialId) {
    if (const auto hash = materialId.find('#'); hash != std::string_view::npos)
        materialId = materialId.substr(0, hash);
    const auto dot = materialId.find('.');
    if (dot == std::string_view::npos) return false;
    const std::string_view groupsAndName = materialId.substr(dot + 1);
    return std::ranges::any_of(GRASS_WORDS, [groupsAndName](std::string_view word) {
        return groupsAndName.find(word) != std::string_view::npos;
    });
}

std::optional<GreenMap> greenMapOf(const umat::Image& rgba, double unitsPerTexel) {
    if (rgba.channels != 4 || rgba.width == 0 || rgba.height == 0 || !(unitsPerTexel > 0)) return std::nullopt;
    // The power of two nearest the wanted step, from 1 to the picture's side.
    const double wanted = CLUMP_SPACING / TEXELS_PER_CLUMP / unitsPerTexel;
    std::uint32_t step = 1;
    while (step * 2 <= std::min(rgba.width, rgba.height) && std::log2(wanted) >= std::log2(step) + 0.5) step *= 2;
    GreenMap map{std::max(rgba.width / step, 1U), std::max(rgba.height / step, 1U), {}};
    // Each texel's green share, then its density against the level's mean
    // share: UT99's grass is olive, red above green, so only a relative test
    // finds its bare patches.
    std::vector<double> share;
    share.reserve(std::size_t{map.width} * map.height);
    for (std::uint32_t y = 0; y < map.height; ++y)
        for (std::uint32_t x = 0; x < map.width; ++x) {
            Rgb sum;
            for (std::uint32_t dy = 0; dy < step; ++dy)
                for (std::uint32_t dx = 0; dx < step; ++dx) {
                    const std::size_t at = (std::size_t{y * step + dy} * rgba.width + (x * step + dx)) * 4;
                    sum.r += linearOf(std::to_integer<std::uint8_t>(rgba.pixels[at]));
                    sum.g += linearOf(std::to_integer<std::uint8_t>(rgba.pixels[at + 1]));
                    sum.b += linearOf(std::to_integer<std::uint8_t>(rgba.pixels[at + 2]));
                }
            const double n = static_cast<double>(step) * step;
            share.push_back((sum.g / n + SHARE_GREY / 3) / ((sum.r + sum.g + sum.b) / n + SHARE_GREY));
        }
    double mean = 0;
    for (const double s : share) mean += s;
    mean /= static_cast<double>(share.size());
    map.green.reserve(share.size());
    for (const double s : share)
        map.green.push_back(static_cast<std::uint8_t>(std::lround(smoothstep(GREEN_LOW, GREEN_HIGH, s - mean) * 255)));
    return map;
}

namespace {

/// What one planting pass found: the samples, at most GRASS_CLUMP_LIMIT of
/// them, and how many it would have kept had there been no limit.
struct Pass {
    std::vector<Sample> samples;
    std::size_t count = 0;
};

/// SS 4.1 and SS 4.2 on a grid of `spacing` units.
[[nodiscard]] Pass plantAt(const ubundle::Bundle& bundle, const GreenLookup& green, double spacing) {
    Pass out;
    const ubundle::Geometry& geometry = *bundle.geometry;
    const std::vector<ubundle::MaterialRecord>& materials = *bundle.materials;
    const std::vector<std::array<double, 2>> pickups = pickupsOf(bundle);
    const ubundle::ShadowMask* const mask =
        bundle.shadowMask && bundle.shadowMask->vertexChart.size() == geometry.vertices.size()
                && bundle.shadowMask->vertexTexel.size() == geometry.vertices.size()
            ? &*bundle.shadowMask
            : nullptr;

    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        if ((batch.polyFlags & NOT_GROUND) != 0 || !isGroundCover(batch.material)) continue;
        const auto record = std::ranges::lower_bound(materials, batch.material, {}, &ubundle::MaterialRecord::id);
        if (record == materials.end() || record->id != batch.material || hasOwnLook(*record)) continue;
        const GreenMap* const map = green(batch.material);
        if (map == nullptr || map->green.empty()) continue;
        const auto materialIndex = static_cast<std::uint32_t>(record - materials.begin());

        for (std::uint32_t first = batch.firstIndex; first + 3 <= batch.firstIndex + batch.indexCount; first += 3) {
            const std::uint64_t triangle = first / 3;
            const std::array<std::uint32_t, 3> corner = {geometry.indices[first], geometry.indices[first + 1],
                                                         geometry.indices[first + 2]};
            const ubundle::GeometryVertex& a = geometry.vertices[corner[0]];
            const ubundle::GeometryVertex& b = geometry.vertices[corner[1]];
            const ubundle::GeometryVertex& c = geometry.vertices[corner[2]];
            if (a.normal[2] < GRASS_SLOPE) continue;
            if (bundle.zones && a.zone < bundle.zones->size() && (*bundle.zones)[a.zone].water != 0) continue;

            // Barycentric weights in the plan; a floor's triangle has area there.
            const double x0 = a.position[0], y0 = a.position[1];
            const double e1x = b.position[0] - x0, e1y = b.position[1] - y0;
            const double e2x = c.position[0] - x0, e2y = c.position[1] - y0;
            const double area = e1x * e2y - e2x * e1y;
            if (std::abs(area) < 1e-6) continue;
            const auto cellOf = [spacing](double t) { return static_cast<std::int64_t>(std::floor(t / spacing)); };
            const double lowX = std::min({x0, double{b.position[0]}, double{c.position[0]}});
            const double highX = std::max({x0, double{b.position[0]}, double{c.position[0]}});
            const double lowY = std::min({y0, double{b.position[1]}, double{c.position[1]}});
            const double highY = std::max({y0, double{b.position[1]}, double{c.position[1]}});
            const bool charted = mask != nullptr && mask->vertexChart[corner[0]] != ubundle::MASK_NO_CHART
                                 && mask->vertexChart[corner[0]] == mask->vertexChart[corner[1]]
                                 && mask->vertexChart[corner[0]] == mask->vertexChart[corner[2]];

            for (std::int64_t j = cellOf(lowY); j <= cellOf(highY); ++j)
                for (std::int64_t i = cellOf(lowX); i <= cellOf(highX); ++i) {
                    // The jitter is the grid cell's alone, so a polygon cut
                    // into triangles plants evenly across its seams; whether
                    // the sample is kept, and its seed, are also the triangle's.
                    const std::uint64_t jitter = hashOf(i, j, 0);
                    const double px = (static_cast<double>(i) + unitOf(jitter)) * spacing;
                    const double py = (static_cast<double>(j) + unitOf(mix(jitter))) * spacing;
                    const double dx = px - x0, dy = py - y0;
                    const double wb = (dx * e2y - e2x * dy) / area;
                    const double wc = (e1x * dy - dx * e1y) / area;
                    const double wa = 1 - wb - wc;
                    if (wa < 0 || wb < 0 || wc < 0) continue;
                    const auto blend = [wa, wb, wc](float va, float vb, float vc) {
                        return wa * va + wb * vb + wc * vc;
                    };
                    const double u = blend(a.u, b.u, c.u);
                    const double v = blend(a.v, b.v, c.v);
                    if (unitOf(hashOf(i, j, mix(triangle) ^ 1)) >= densityAt(*map, u, v)) continue;
                    if (nearPickup(pickups, px, py)) continue;
                    // Past the limit only the count goes on: this pass is
                    // planted again, sparser (plantGrass).
                    if (++out.count > ubundle::GRASS_CLUMP_LIMIT) continue;

                    ubundle::GrassClump clump;
                    clump.base = {static_cast<float>(px), static_cast<float>(py),
                                  static_cast<float>(blend(a.position[2], b.position[2], c.position[2]))};
                    clump.material = materialIndex;
                    clump.uv = {static_cast<float>(u), static_cast<float>(v)};
                    if (charted) {
                        const auto& ta = mask->vertexTexel[corner[0]];
                        const auto& tb = mask->vertexTexel[corner[1]];
                        const auto& tc = mask->vertexTexel[corner[2]];
                        clump.maskChart = mask->vertexChart[corner[0]];
                        clump.maskTexel = {static_cast<float>(blend(ta[0], tb[0], tc[0])),
                                           static_cast<float>(blend(ta[1], tb[1], tc[1]))};
                    }
                    clump.seed = static_cast<std::uint32_t>(hashOf(i, j, mix(triangle) ^ 2));
                    out.samples.push_back(Sample{static_cast<std::int64_t>(std::floor(px / GRASS_CELL)),
                                             static_cast<std::int64_t>(std::floor(py / GRASS_CELL)), clump});
                }
        }
    }
    return out;
}

} // namespace

Planted plantGrass(const ubundle::Bundle& bundle, const GreenLookup& green) {
    Planted out;
    Pass pass = plantAt(bundle, green, out.spacing);
    // SS 4.3: a map past the limit is planted again on a wider grid. The
    // count falls with the square of the spacing; the margin and the loop
    // absorb the jitter, which moves with the grid.
    while (pass.count > ubundle::GRASS_CLUMP_LIMIT) {
        out.spacing *= std::sqrt(static_cast<double>(pass.count) / ubundle::GRASS_CLUMP_LIMIT) * 1.02;
        pass = plantAt(bundle, green, out.spacing);
    }
    if (!pass.samples.empty()) out.grass = sortIntoCells(std::move(pass.samples));
    return out;
}

} // namespace uta::ubake
