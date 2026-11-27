// Embedded.h's reading.

#include "Embedded.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace uta::dump {
namespace {

using uworld::Vec3;

/// The farthest apart two neighbouring sample points are. UT99's thinnest
/// solid brushes are 8 to 16 across, so no solid that thick hides between two.
constexpr double SPACING = 8;
/// Past a radius or half-height of 512, the spacing is that size over ROW,
/// so a giant costs no more points than a body of 512: real maps place a
/// Squid of radius 4000, which at 8 would be millions of points a push.
constexpr double ROW = 64;

/// The points tested, relative to the centre: the side at every height, both
/// caps on a square lattice, and the axis. A solid wholly inside the body,
/// touching none of them, is missed; a level's solids are walls and floors.
std::vector<Vec3> samplesOf(double radius, double halfHeight) {
    const double r = std::max(radius - 1, 0.0);
    const double h = std::max(halfHeight - 1, 0.0);
    const double spacing = std::max(SPACING, std::max(r, h) / ROW);
    const int levels = std::max(1, static_cast<int>(std::ceil(2 * h / spacing)));
    const int around = std::max(8, static_cast<int>(std::ceil(2 * std::numbers::pi * r / spacing)));
    std::vector<Vec3> out;
    for (int i = 0; i <= levels; ++i) {
        const double z = -h + 2 * h * i / levels;
        out.push_back({0, 0, z});
        for (int k = 0; k < around; ++k) {
            const double angle = 2 * std::numbers::pi * k / around;
            out.push_back({r * std::cos(angle), r * std::sin(angle), z});
        }
    }
    const int across = static_cast<int>(std::ceil(r / spacing));
    for (int ix = -across; ix <= across; ++ix)
        for (int iy = -across; iy <= across; ++iy) {
            const double x = ix * spacing;
            const double y = iy * spacing;
            if ((ix == 0 && iy == 0) || x * x + y * y > r * r) continue;
            out.push_back({x, y, -h});
            out.push_back({x, y, h});
        }
    return out;
}

bool free(const ubundle::CollisionTree& tree, const Vec3& centre, const std::vector<Vec3>& samples) {
    for (const Vec3& offset : samples)
        if (!uworld::isEmpty(tree, centre + offset)) return false;
    return true;
}

/// Up, down, and eight level directions 45 degrees apart.
const std::array<Vec3, 10>& directions() {
    static const std::array<Vec3, 10> all = [] {
        std::array<Vec3, 10> out{Vec3{0, 0, 1}, Vec3{0, 0, -1}};
        for (int k = 0; k < 8; ++k) {
            const double angle = k * std::numbers::pi / 4;
            out[2 + k] = {std::cos(angle), std::sin(angle), 0};
        }
        return out;
    }();
    return all;
}

} // namespace

EmbeddedReading readEmbedded(const ubundle::CollisionTree& tree, const Vec3& centre, double radius,
                             double halfHeight) {
    EmbeddedReading reading;
    reading.centreSolid = !uworld::isEmpty(tree, centre);
    const uworld::Hit down = uworld::trace(tree, centre, centre - Vec3{0, 0, FLOOR_REACH});
    if (down.fraction < 1) reading.floorGap = down.fraction * FLOOR_REACH;
    reading.embedded = reading.floorGap.has_value() && *reading.floorGap < halfHeight - SINK_SLACK;

    const std::vector<Vec3> samples = samplesOf(radius, halfHeight);
    if (free(tree, centre, samples)) {
        reading.depth = 0.0;
        return reading;
    }
    // 1 at a time to 32, then 4: a shallow overlap is read to the unit, and a
    // deep one costs a tenth as many tries.
    for (double push = 1; push <= MAX_PUSH; push += push < 32 ? 1 : 4)
        for (const Vec3& way : directions())
            if (free(tree, centre + way * push, samples)) {
                reading.depth = push;
                return reading;
            }
    return reading;
}

} // namespace uta::dump
