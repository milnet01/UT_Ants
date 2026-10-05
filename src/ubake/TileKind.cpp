// Whether a material's repeats may move -- docs/specs/UTA-0277-per-tile-
// variation.md SS 4.2 and SS 4.5.

#include "ubake/TileKind.h"

#include "core/Sha256.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace uta::ubake {
namespace {

/// SS 4.2 step 4's working size, and step 3's and the autocorrelation's.
constexpr std::size_t SIDE = 128;
constexpr std::size_t HALF = SIDE / 2;
/// The high-pass's box: blotches wider than this are removed (1/32 cycles a texel).
constexpr std::size_t BOX = 32;
/// Step 3: a luma variance under 2 squared at 64 x 64 is flat.
constexpr double FLAT_VARIANCE = 4.0;
/// Bands are counted against a noise picture's, which is about 1 on this scale.
constexpr double LINES_SCALE = 64.0;
/// Radius 4 at 128 texels is 2 at 64, squared.
constexpr int SPOTS_RADIUS_SQUARED = 4;

using Grid = std::vector<double>; // row-major, `side` by `side`

/// The picture's luma, box-resampled to SIDE by SIDE. A source side shorter
/// than SIDE repeats its texels; a longer one averages each output texel's
/// span. Integer weights, so every compiler sums the same terms.
Grid lumaAt(const umat::Image& rgba) {
    const std::size_t width = rgba.width;
    const std::size_t height = rgba.height;
    const auto luma = [&](std::size_t x, std::size_t y) {
        const std::byte* p = rgba.pixels.data() + (y * width + x) * 4;
        return (54.0 * std::to_integer<int>(p[0]) + 183.0 * std::to_integer<int>(p[1])
                + 19.0 * std::to_integer<int>(p[2]))
               / 256.0;
    };
    const auto span = [](std::size_t i, std::size_t size) {
        const std::size_t first = i * size / SIDE;
        return std::pair{first, std::max(first + 1, (i + 1) * size / SIDE)};
    };
    Grid out(SIDE * SIDE);
    for (std::size_t y = 0; y < SIDE; ++y) {
        const auto [y0, y1] = span(y, height);
        for (std::size_t x = 0; x < SIDE; ++x) {
            const auto [x0, x1] = span(x, width);
            double sum = 0;
            for (std::size_t sy = y0; sy < y1; ++sy)
                for (std::size_t sx = x0; sx < x1; ++sx) sum += luma(sx, sy);
            out[y * SIDE + x] = sum / static_cast<double>((y1 - y0) * (x1 - x0));
        }
    }
    return out;
}

/// Each 2 x 2 block's mean: `side` to `side` / 2.
Grid halved(const Grid& grid, std::size_t side) {
    const std::size_t half = side / 2;
    Grid out(half * half);
    for (std::size_t y = 0; y < half; ++y)
        for (std::size_t x = 0; x < half; ++x)
            out[y * half + x] = (grid[2 * y * side + 2 * x] + grid[2 * y * side + 2 * x + 1]
                                 + grid[(2 * y + 1) * side + 2 * x] + grid[(2 * y + 1) * side + 2 * x + 1])
                                / 4.0;
    return out;
}

double meanOf(const Grid& grid) {
    double sum = 0;
    for (const double value : grid) sum += value;
    return sum / static_cast<double>(grid.size());
}

double varianceOf(const std::vector<double>& values) {
    const double mean = meanOf(values);
    double sum = 0;
    for (const double value : values) sum += (value - mean) * (value - mean);
    return sum / static_cast<double>(values.size());
}

/// The picture less its BOX-wide wrap-around box mean, rows then columns: a
/// texture tiles, so its far edge is its near edge's neighbour.
Grid highPassed(const Grid& grid) {
    const auto at = [](std::size_t i, int d) {
        return static_cast<std::size_t>(static_cast<int>(i) + d + static_cast<int>(SIDE)) % SIDE;
    };
    constexpr int FIRST = -static_cast<int>(BOX / 2) + 1; // the window is i-15 .. i+16
    Grid along(SIDE * SIDE);
    for (std::size_t y = 0; y < SIDE; ++y)
        for (std::size_t x = 0; x < SIDE; ++x) {
            double sum = 0;
            for (int d = FIRST; d < FIRST + static_cast<int>(BOX); ++d) sum += grid[at(y, d) * SIDE + x];
            along[y * SIDE + x] = sum / static_cast<double>(BOX);
        }
    Grid out(SIDE * SIDE);
    for (std::size_t y = 0; y < SIDE; ++y)
        for (std::size_t x = 0; x < SIDE; ++x) {
            double sum = 0;
            for (int d = FIRST; d < FIRST + static_cast<int>(BOX); ++d) sum += along[y * SIDE + at(x, d)];
            out[y * SIDE + x] = grid[y * SIDE + x] - sum / static_cast<double>(BOX);
        }
    return out;
}

/// The variance of the means along each row, each column and each of the two
/// diagonal families, paired as rows with columns and diagonal with diagonal;
/// the larger pair, over the picture's variance. A diagonal wraps, so each of
/// its SIDE lines holds SIDE texels.
double linesOf(const Grid& hp, double variance) {
    std::vector<double> rows(SIDE), columns(SIDE), down(SIDE), up(SIDE);
    for (std::size_t y = 0; y < SIDE; ++y)
        for (std::size_t x = 0; x < SIDE; ++x) {
            const double value = hp[y * SIDE + x];
            rows[y] += value;
            columns[x] += value;
            down[(x + y) % SIDE] += value;
            up[(x + SIDE - y) % SIDE] += value;
        }
    for (auto* line : {&rows, &columns, &down, &up})
        for (double& sum : *line) sum /= static_cast<double>(SIDE);
    const double axes = varianceOf(rows) + varianceOf(columns);
    const double diagonals = varianceOf(down) + varianceOf(up);
    return std::max(axes, diagonals) / variance * LINES_SCALE;
}

/// The highest wrap-around autocorrelation of the high-passed picture at 64 x
/// 64, over offsets further than the radius, as a share of its energy.
double spotsOf(const Grid& hp) {
    const Grid q = halved(hp, SIDE);
    double energy = 0;
    for (const double value : q) energy += value * value;
    if (!(energy > 0)) return 0;
    double best = -1;
    for (std::size_t dy = 0; dy < HALF; ++dy)
        for (std::size_t dx = 0; dx < HALF; ++dx) {
            const int sy = dy > HALF / 2 ? static_cast<int>(dy) - static_cast<int>(HALF) : static_cast<int>(dy);
            const int sx = dx > HALF / 2 ? static_cast<int>(dx) - static_cast<int>(HALF) : static_cast<int>(dx);
            if (sx * sx + sy * sy <= SPOTS_RADIUS_SQUARED) continue;
            double sum = 0;
            for (std::size_t y = 0; y < HALF; ++y) {
                const std::size_t ty = (y + dy) % HALF;
                for (std::size_t x = 0; x < HALF; ++x) sum += q[y * HALF + x] * q[ty * HALF + (x + dx) % HALF];
            }
            best = std::max(best, sum / energy);
        }
    return best;
}

using Vec = std::array<double, 3>;

Vec toDouble(const upkg::Vector3& v) { return {v.x, v.y, v.z}; }
Vec minus(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

template <typename T>
bool within(std::int32_t index, const std::vector<T>& table) {
    return index >= 0 && static_cast<std::size_t>(index) < table.size();
}

} // namespace

TileScores tileScores(const umat::Image& rgba) {
    if (rgba.channels != 4 || rgba.width == 0 || rgba.height == 0
        || rgba.pixels.size() < std::size_t{rgba.width} * rgba.height * 4)
        return TileScores{true, 0, 0};
    const Grid luma = lumaAt(rgba);
    if (varianceOf(halved(luma, SIDE)) < FLAT_VARIANCE) return TileScores{true, 0, 0};
    const Grid hp = highPassed(luma);
    const double variance = varianceOf(hp);
    if (!(variance > 0)) return TileScores{true, 0, 0};
    return TileScores{false, linesOf(hp, variance), spotsOf(hp)};
}

ubundle::TileKind judgeTile(const TileScores& scores, const TileLimits& limits) {
    if (scores.flat) return ubundle::TileKind::Fixed;
    if (scores.lines < limits.linesShuffle && scores.spots < limits.spotsShuffle) return ubundle::TileKind::Shuffle;
    if (scores.lines >= limits.linesFixed || scores.spots >= limits.spotsFixed) return ubundle::TileKind::Fixed;
    return ubundle::TileKind::Unsure;
}

std::array<std::byte, 32> pictureHash(const umat::Image& rgba) {
    Sha256 hash;
    std::array<std::byte, 8> size{};
    for (std::size_t i = 0; i < 4; ++i) {
        size[i] = static_cast<std::byte>((rgba.width >> (8 * i)) & 0xFFu);
        size[4 + i] = static_cast<std::byte>((rgba.height >> (8 * i)) & 0xFFu);
    }
    hash.add(size);
    hash.add(rgba.pixels);
    return hash.finish();
}

std::map<std::string, TileSurfaces, std::less<>> tileSurfaces(const upkg::Model& model,
                                                              const MaterialLookup& materials) {
    struct Gathered {
        const SurfaceMaterial* made = nullptr;
        double uMin = INFINITY, uMax = -INFINITY, vMin = INFINITY, vMax = -INFINITY;
        Vec lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
        Vec weighted{};
        double area = 0;
        Vec normal{};
    };
    std::map<std::int32_t, Gathered> bySurf; // ascending, so the result does not depend on node order
    for (const upkg::BspNode& node : model.nodes) {
        if (node.numVertices < 3 || !within(node.iSurf, model.surfs)) continue;
        const upkg::BspSurf& surf = model.surfs[static_cast<std::size_t>(node.iSurf)];
        if ((surf.polyFlags & PF_INVISIBLE) != 0 || surf.texture.kind() == upkg::ObjectReferenceKind::Null) continue;
        if (node.iVertPool < 0 || static_cast<std::size_t>(node.iVertPool) + node.numVertices > model.verts.size())
            continue;
        if (!within(surf.pBase, model.points) || !within(surf.vNormal, model.vectors)
            || !within(surf.vTextureU, model.vectors) || !within(surf.vTextureV, model.vectors))
            continue;
        const SurfaceMaterial* const made = materials(surf.texture, (surf.polyFlags & PF_MASKED) != 0);
        if (made == nullptr || !(made->uSize > 0) || !(made->vSize > 0)) continue;
        std::vector<Vec> points;
        bool inRange = true;
        for (std::size_t k = 0; k < node.numVertices; ++k) {
            const std::int32_t point = model.verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex;
            if (!within(point, model.points)) inRange = false;
            else points.push_back(toDouble(model.points[static_cast<std::size_t>(point)]));
        }
        if (!inRange) continue;
        Gathered& g = bySurf[node.iSurf];
        g.made = made;
        const Vec base = toDouble(model.points[static_cast<std::size_t>(surf.pBase)]);
        const Vec textureU = toDouble(model.vectors[static_cast<std::size_t>(surf.vTextureU)]);
        const Vec textureV = toDouble(model.vectors[static_cast<std::size_t>(surf.vTextureV)]);
        g.normal = toDouble(model.vectors[static_cast<std::size_t>(surf.vNormal)]);
        for (const Vec& p : points) {
            // UTA-0109 SS 4.4's mapping, as buildGeometry writes each vertex.
            const double u = (dot(minus(p, base), textureU) + surf.panU) / made->uSize;
            const double v = (dot(minus(p, base), textureV) + surf.panV) / made->vSize;
            g.uMin = std::min(g.uMin, u);
            g.uMax = std::max(g.uMax, u);
            g.vMin = std::min(g.vMin, v);
            g.vMax = std::max(g.vMax, v);
            for (std::size_t a = 0; a < 3; ++a) {
                g.lo[a] = std::min(g.lo[a], p[a]);
                g.hi[a] = std::max(g.hi[a], p[a]);
            }
        }
        for (std::size_t k = 1; k + 1 < points.size(); ++k) {
            const Vec c = cross(minus(points[k], points[0]), minus(points[k + 1], points[0]));
            const double area = std::sqrt(dot(c, c)) / 2;
            g.area += area;
            for (std::size_t a = 0; a < 3; ++a) g.weighted[a] += area * (points[0][a] + points[k][a] + points[k + 1][a]) / 3;
        }
    }
    std::map<std::string, TileSurfaces, std::less<>> out;
    for (const auto& [index, g] : bySurf) {
        // A surface with no area or no normal is never seen, so it neither
        // counts nor gives the view -- which keeps the view's normal unit.
        const double length = std::sqrt(dot(g.normal, g.normal));
        if (!(g.area > 0) || !(length > 0)) continue;
        TileSurfaces& t = out[g.made->id];
        ++t.surfaces;
        if (g.uMax - g.uMin >= 2 && g.vMax - g.vMin >= 2) t.spansTwo = true;
        if (!(g.area > t.largestArea)) continue;
        t.largestArea = g.area;
        for (std::size_t a = 0; a < 3; ++a) {
            t.at[a] = g.weighted[a] / g.area;
            t.normal[a] = g.normal[a] / length;
        }
        t.extent = std::max({g.hi[0] - g.lo[0], g.hi[1] - g.lo[1], g.hi[2] - g.lo[2]});
    }
    return out;
}

} // namespace uta::ubake
