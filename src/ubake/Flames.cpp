// The level's flame sheets, as FLAM records --
// docs/specs/UTA-0263-shader-flames.md SS 4.3 and SS 4.5.

#include "ubake/Flames.h"

#include "ubake/Geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace uta::ubake {
namespace {

using Vec = std::array<double, 3>;

/// One sheet before merging: its material, and the corners it covers.
struct Sheet {
    std::uint32_t material = 0;
    std::uint32_t surface = 0;
    Vec base{};
    double width = 0;
    double height = 0;
};

bool within(std::int64_t index, std::size_t size) {
    return index >= 0 && static_cast<std::uint64_t>(index) < size;
}

double horizontalDistance(const Vec& a, const Vec& b) {
    return std::hypot(a[0] - b[0], a[1] - b[1]);
}

double distance(const Vec& a, const Vec& b) {
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

/// The corners of every drawn node of each surface, or nothing for a surface
/// one of whose nodes names an index past its table or a point that is not
/// finite and inside MAX_COORDINATE -- buildGeometry refuses that node, so it
/// must reach buildGeometry.
std::map<std::uint32_t, std::vector<Vec>> cornersBySurface(const upkg::Model& model,
                                                           const std::vector<bool>& candidate) {
    std::map<std::uint32_t, std::vector<Vec>> corners;
    std::vector<bool> broken(model.surfs.size(), false);
    for (const upkg::BspNode& node : model.nodes) {
        if (node.numVertices < 3 || !within(node.iSurf, model.surfs.size())) continue;
        const auto surface = static_cast<std::uint32_t>(node.iSurf);
        if (!candidate[surface] || broken[surface]) continue;
        const std::size_t count = node.numVertices;
        bool ok = node.iVertPool >= 0 && static_cast<std::uint64_t>(node.iVertPool) + count <= model.verts.size();
        std::vector<Vec> points;
        for (std::size_t k = 0; ok && k < count; ++k) {
            const std::int32_t point = model.verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex;
            if (!within(point, model.points.size())) {
                ok = false;
                break;
            }
            const upkg::Vector3& p = model.points[static_cast<std::size_t>(point)];
            const Vec at{p.x, p.y, p.z};
            for (const double part : at)
                if (!(std::abs(part) <= MAX_COORDINATE)) ok = false;
            points.push_back(at);
        }
        if (!ok) {
            broken[surface] = true;
            corners.erase(surface);
            continue;
        }
        auto& all = corners[surface];
        all.insert(all.end(), points.begin(), points.end());
    }
    return corners;
}

/// The sheet `points` span, or why it makes none. The horizontal axis is the
/// one across the surface: perpendicular to its normal and to the vertical.
std::expected<Sheet, std::string> sheetOf(const std::vector<Vec>& points, const upkg::Vector3& normal) {
    const double across = std::hypot(normal.x, normal.y);
    if (!(across > 0)) return std::unexpected(std::string("it lies flat, so it has no height"));
    const Vec h{-normal.y / across, normal.x / across, 0};
    const auto along = [&h](const Vec& p) { return p[0] * h[0] + p[1] * h[1]; };

    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    double first = std::numeric_limits<double>::max();
    double last = std::numeric_limits<double>::lowest();
    for (const Vec& p : points) {
        low = std::min(low, p[2]);
        high = std::max(high, p[2]);
        first = std::min(first, along(p));
        last = std::max(last, along(p));
    }
    Sheet sheet;
    sheet.height = high - low;
    sheet.width = last - first;
    if (!(sheet.height > 0) || !(sheet.width > 0))
        return std::unexpected(std::string("it has no width or no height"));

    // The centre of the lowest edge: its two ends are the lowest points
    // furthest apart across the sheet.
    const Vec* left = nullptr;
    const Vec* right = nullptr;
    for (const Vec& p : points) {
        if (p[2] > low + LOWEST_EDGE_TOLERANCE) continue;
        if (left == nullptr || along(p) < along(*left)) left = &p;
        if (right == nullptr || along(p) > along(*right)) right = &p;
    }
    sheet.base = {((*left)[0] + (*right)[0]) / 2, ((*left)[1] + (*right)[1]) / 2, low};
    return sheet;
}

/// Whether two sheets are one flame: one material, bases closer than half
/// the smaller width.
bool oneFlame(const Sheet& a, const Sheet& b) {
    return a.material == b.material && distance(a.base, b.base) < std::min(a.width, b.width) / 2;
}

/// One sheet covering both: its base between theirs at the lower foot, as
/// tall as the taller top, as wide as reaches either's far side.
Sheet merged(const Sheet& a, const Sheet& b) {
    Sheet out;
    out.material = a.material;
    out.surface = std::min(a.surface, b.surface);
    const double low = std::min(a.base[2], b.base[2]);
    out.base = {(a.base[0] + b.base[0]) / 2, (a.base[1] + b.base[1]) / 2, low};
    out.height = std::max(a.base[2] + a.height, b.base[2] + b.height) - low;
    out.width = std::max(2 * horizontalDistance(out.base, a.base) + a.width,
                         2 * horizontalDistance(out.base, b.base) + b.width);
    return out;
}

} // namespace

FlameSheets findFlameSheets(const upkg::Model& model, const FlameMaterialLookup& materials) {
    // Which surfaces carry a sheet's flags and wear a flame, and which record.
    std::vector<bool> candidate(model.surfs.size(), false);
    std::vector<std::uint32_t> recordOf(model.surfs.size(), 0);
    for (std::size_t s = 0; s < model.surfs.size(); ++s) {
        const upkg::BspSurf& surf = model.surfs[s];
        if ((surf.polyFlags & PF_INVISIBLE) != 0 || (surf.polyFlags & PF_NOT_SOLID) == 0
            || (surf.polyFlags & (PF_TRANSLUCENT | PF_MASKED)) == 0
            || surf.texture.kind() == upkg::ObjectReferenceKind::Null)
            continue;
        const auto record = materials(surf.texture, (surf.polyFlags & PF_MASKED) != 0);
        if (!record.has_value()) continue;
        candidate[s] = true;
        recordOf[s] = *record;
    }

    FlameSheets out;
    std::vector<Sheet> sheets;
    for (const auto& [surface, points] : cornersBySurface(model, candidate)) {
        const upkg::BspSurf& surf = model.surfs[surface];
        if (!within(surf.vNormal, model.vectors.size())) continue; // buildGeometry refuses it
        auto sheet = sheetOf(points, model.vectors[static_cast<std::size_t>(surf.vNormal)]);
        if (!sheet.has_value()) {
            out.skipped.push_back(SkippedFlame{surface, std::move(sheet).error()});
            continue;
        }
        sheet->material = recordOf[surface];
        sheet->surface = surface;
        sheets.push_back(*sheet);
        out.surfaces.push_back(surface);
    }

    // Pairwise until nothing merges. The merged sheet keeps the lower
    // surface's place, so the order stays ascending by seed.
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t i = 0; i < sheets.size(); ++i)
            for (std::size_t j = i + 1; j < sheets.size();) {
                if (oneFlame(sheets[i], sheets[j])) {
                    sheets[i] = merged(sheets[i], sheets[j]);
                    sheets.erase(sheets.begin() + static_cast<std::ptrdiff_t>(j));
                    changed = true;
                } else {
                    ++j;
                }
            }
    }

    out.flames.reserve(sheets.size());
    for (const Sheet& sheet : sheets)
        out.flames.push_back(ubundle::Flame{sheet.material,
                                            {static_cast<float>(sheet.base[0]), static_cast<float>(sheet.base[1]),
                                             static_cast<float>(sheet.base[2])},
                                            static_cast<float>(sheet.width),
                                            static_cast<float>(sheet.height),
                                            sheet.surface,
                                            -1});
    return out;
}

void assignFlameLights(std::vector<ubundle::Flame>& flames, const std::vector<ubundle::Light>& lights) {
    for (ubundle::Flame& flame : flames) {
        const Vec base{flame.base[0], flame.base[1], flame.base[2]};
        const double reach = 1.5 * static_cast<double>(flame.height);
        double best = std::numeric_limits<double>::max();
        flame.light = -1;
        for (std::size_t i = 0; i < lights.size(); ++i) {
            if (lights[i].strip == ubundle::STRIP_ABSORBED) continue;
            const double d =
                distance(base, Vec{lights[i].location[0], lights[i].location[1], lights[i].location[2]});
            // Strictly nearer, so a tie keeps the lower index.
            if (d <= reach && d < best) {
                best = d;
                flame.light = static_cast<std::int32_t>(i);
            }
        }
    }
}

} // namespace uta::ubake
