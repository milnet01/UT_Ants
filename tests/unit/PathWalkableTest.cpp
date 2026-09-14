// Locks INV-3, INV-4 and INV-15 of docs/specs/UTA-0121-bot-path-seeds.md: where
// a spot stands, which spots join, and where the grid stops.
//
// Worlds are built with tests/unit/PathFixture.h. Every one's points but
// INV-15's start at X and Y 0, so column i stands at X = 32 i and row j at
// Y = 32 j.

#include "PathFixture.h"

#include "ut-paths/Walkable.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

using namespace uta::paths;
using uta::test::paths::box;
using uta::test::paths::Plane;
using uta::test::paths::Region;
using uta::test::paths::worldOf;

namespace {

/// The one spot in the column at X = `x`, row Y = 128.
std::optional<std::uint32_t> spotAt(const WalkGraph& graph, double x) {
    const WalkGraph::Range cell =
        graph.cell(static_cast<std::int32_t>(std::lround(x / COLUMN)), 4);
    if (cell.end - cell.begin != 1) return std::nullopt;
    return cell.begin;
}

bool joined(const WalkGraph& graph, std::uint32_t a, std::uint32_t b) {
    const auto& from = graph.joins[a];
    return std::find(from.begin(), from.end(), b) != from.end();
}

/// A room 256 square whose floor has normal (-s, 0, z) and passes through
/// the origin, with a ceiling parallel to it `headroom` above it.
Region tiltedRoom(double normalZ, double headroom) {
    const double s = std::sqrt(1 - normalZ * normalZ);
    Region room = box({0, 0, -1000}, {256, 256, 1000});
    room.push_back(Plane{{-s, 0, normalZ}, 0});
    room.push_back(Plane{{s, 0, -normalZ}, -normalZ * headroom});
    return room;
}

} // namespace

TEST_CASE("INV-3: a spot stands H above a floor where the body fits", "[paths][walkable]") {
    const WalkGraph room =
        walkGraph(worldOf({box({0, 0, 0}, {256, 256, 100})}, {0, 0, -50}, {256, 256, 150}));
    REQUIRE_FALSE(room.spots.empty());
    for (const Spot& spot : room.spots) CHECK(static_cast<float>(spot.centre.z) == 39.0F);

    const WalkGraph low =
        walkGraph(worldOf({box({0, 0, 0}, {256, 256, 70})}, {0, 0, -50}, {256, 256, 150}));
    CHECK(low.spots.empty());
}

TEST_CASE("INV-3: a floor too steep gives no spots", "[paths][walkable]") {
    CHECK(walkGraph(worldOf({tiltedRoom(0.6, 150)}, {0, 0, -50}, {256, 256, 600})).spots.empty());
    CHECK_FALSE(walkGraph(worldOf({tiltedRoom(0.8, 150)}, {0, 0, -50}, {256, 256, 600})).spots.empty());
}

TEST_CASE("INV-4: spots join within a step and not past it", "[paths][walkable]") {
    // The step's face is 24 from the lower spot, past its radius, so the body
    // there fits beside a step of either height.
    for (const double rise : {20.0, 30.0}) {
        INFO("rise " << rise);
        const WalkGraph graph = walkGraph(worldOf(
            {box({0, 0, 0}, {120, 256, 200}), box({120, 0, rise}, {256, 256, 200})},
            {0, 0, -50}, {256, 256, 250}));
        const auto low = spotAt(graph, 96);
        const auto high = spotAt(graph, 128);
        REQUIRE(low.has_value());
        REQUIRE(high.has_value());
        CHECK(static_cast<float>(graph.spots[*high].centre.z) == static_cast<float>(39 + rise));
        CHECK(joined(graph, *low, *high) == (rise <= STEP));
        CHECK(joined(graph, *high, *low) == (rise <= STEP));
    }
}

TEST_CASE("INV-4: spots on one ramp join past the step", "[paths][walkable]") {
    const WalkGraph graph = walkGraph(worldOf({tiltedRoom(0.75, 200)}, {0, 0, -50}, {256, 256, 600}));
    const auto low = spotAt(graph, 96);
    const auto high = spotAt(graph, 128);
    REQUIRE(low.has_value());
    REQUIRE(high.has_value());
    const double rise = graph.spots[*high].centre.z - graph.spots[*low].centre.z;
    CHECK(rise > STEP);
    CHECK(rise < 29);
    CHECK(joined(graph, *low, *high));
}

TEST_CASE("INV-4: a wall between two spots keeps them apart", "[paths][walkable]") {
    const WalkGraph graph = walkGraph(worldOf(
        {box({0, 0, 0}, {111.5, 256, 200}), box({112.5, 0, 0}, {256, 256, 200})},
        {0, 0, -50}, {256, 256, 250}));
    // Both exist: a wall thick enough to remove one would pass for any join rule.
    const auto west = spotAt(graph, 96);
    const auto east = spotAt(graph, 128);
    REQUIRE(west.has_value());
    REQUIRE(east.has_value());
    CHECK_FALSE(joined(graph, *west, *east));
    CHECK_FALSE(joined(graph, *east, *west));
}

namespace {

/// A room 100 high with its floor at z = 0, from X `from` to `to` and Y 0 to
/// 256, in a world whose points' box runs from X `boxFrom` to `boxTo`.
WalkGraph roomAlongX(double from, double to, double boxFrom, double boxTo) {
    return walkGraph(worldOf({box({from, 0, 0}, {to, 256, 100})}, {boxFrom, 0, -50}, {boxTo, 256, 150}));
}

std::vector<std::array<double, 3>> centres(const WalkGraph& graph) {
    std::vector<std::array<double, 3>> out;
    for (const Spot& spot : graph.spots) out.push_back({spot.centre.x, spot.centre.y, spot.centre.z});
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("INV-15: no column is laid past the world bound, and one on it is", "[paths][walkable]") {
    const WalkGraph high = roomAlongX(32000, 33000, 32000, 33000);
    REQUIRE_FALSE(high.spots.empty());
    double highest = high.spots[0].centre.x;
    for (const Spot& spot : high.spots) highest = std::max(highest, spot.centre.x);
    CHECK(highest == 32768);

    // The box starts off the 32 grid, so its columns stand at -33010 + 32 k:
    // the lowest within the bound is -32754, where a clamped corner would put
    // one at -32768 and no bound at all one at -32978.
    const WalkGraph low = roomAlongX(-33000, -32000, -33010, -32000);
    REQUIRE_FALSE(low.spots.empty());
    double lowest = low.spots[0].centre.x;
    for (const Spot& spot : low.spots) lowest = std::min(lowest, spot.centre.x);
    CHECK(lowest == -32754);
}

TEST_CASE("INV-15: the walk graph indexes the laid columns alone", "[paths][walkable]") {
    const Region room = box({0, 0, 0}, {256, 256, 100});
    const WalkGraph near = walkGraph(worldOf({room}, {0, 0, -50}, {256, 256, 150}));
    REQUIRE_FALSE(near.spots.empty());

    for (const bool farOnY : {false, true}) {
        INFO((farOnY ? "the box far on Y" : "the box far on X"));
        const WalkGraph far = walkGraph(worldOf({room},
                                                farOnY ? Vec3{0, -40000, -50} : Vec3{-40000, 0, -50},
                                                farOnY ? Vec3{256, 40000, 150} : Vec3{40000, 256, 150}));
        CHECK((farOnY ? far.rows : far.columns) <= 2049);
        CHECK(centres(far) == centres(near));
        std::size_t misplaced = 0;
        for (std::uint32_t s = 0; s < far.spots.size(); ++s)
            if (place(far, far.spots[s].centre) != s) ++misplaced;
        CHECK(misplaced == 0);
    }
}
