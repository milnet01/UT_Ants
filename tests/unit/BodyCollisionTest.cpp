// Locks INV-2 to INV-8 of docs/specs/UTA-0457-body-collision.md: a round body
// swept through blocks stops SKIN short of what it hits, reaches convex
// corners, never ends inside solid, slides along walls and into corners, and
// says when it starts inside; movers block where they are placed; and the
// results hash to one constant on every compiler.
//
// Every world here is BlockFixture's: solid blocks in empty space, their mesh
// built by collisionMesh, so these cases run on what INV-1 builds.

#include "BlockFixture.h"

#include "uworld/BodyCollision.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using uta::test::blocks::Block;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;
using uta::uworld::BodySize;
using uta::uworld::BodyWorld;
using uta::uworld::dot;
using uta::uworld::SKIN;
using uta::uworld::Vec3;

namespace {

const BodySize BODY{}; // TMale1's 17 by 39

BodyWorld worldOf(const std::vector<Block>& blocks) {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks(blocks);
    auto world = BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    return std::move(*world);
}

/// A unit vector `degrees` counter-clockwise from +X, seen from above.
Vec3 heading(double degrees) {
    const double a = degrees * std::numbers::pi / 180;
    return {std::cos(a), std::sin(a), 0};
}

Vec3 end(const Vec3& from, const Vec3& to, double fraction) { return from + (to - from) * fraction; }

} // namespace

TEST_CASE("UTA-0457 INV-2: a body stops SKIN off a wall, whatever the wall's angle") {
    for (const double yaw : {0.0, 30.0, 45.0}) {
        // A wall whose face, before turning, is the plane x = 0, facing -X.
        const auto world = worldOf({box({0, -400, -200}, {64, 400, 200}, yaw)});
        const Vec3 facing = heading(180 + yaw);           // the wall's normal
        const Vec3 onFace = Vec3{32, 0, 0} - heading(yaw) * 32; // the face's centre after turning
        // Approach at 20 degrees off the normal, from 200 units out.
        const Vec3 from = onFace + facing * 200 + heading(270 + yaw) * 40;
        const Vec3 to = from + heading(yaw + 20) * 400;
        const auto hit = world.sweep(BODY, from, to);
        INFO("yaw " << yaw);
        REQUIRE(hit.fraction < 1);
        CHECK_FALSE(hit.startsInside);
        CHECK(dot(hit.normal, facing) == Catch::Approx(1).margin(1e-3));
        const double gap = dot(end(from, to, hit.fraction) - onFace, facing) - BODY.radius;
        CHECK(gap == Catch::Approx(SKIN).margin(0.01));
    }
}

TEST_CASE("UTA-0457 INV-2: a body falling onto a floor stops SKIN above it") {
    const auto world = worldOf({box({-200, -200, -40}, {200, 200, 0})});
    const Vec3 from{0, 0, 100};
    const Vec3 to{0, 0, -100};
    const auto hit = world.sweep(BODY, from, to);
    REQUIRE(hit.fraction < 1);
    CHECK(end(from, to, hit.fraction).z - BODY.halfHeight == Catch::Approx(SKIN).margin(0.01));
    CHECK(hit.normal.z == Catch::Approx(1).margin(1e-3));
}

TEST_CASE("UTA-0457 INV-3: at a convex corner the body's edge reaches the corner") {
    // A pillar whose corner edge is the Z axis; the body comes at it along
    // the diagonal. A box, or planes pushed out by the body's size, would
    // stop it radius * sqrt(2) out, 24 units.
    const auto world = worldOf({box({0, 0, -200}, {100, 100, 200})});
    const Vec3 from{-100, -100, 0};
    const Vec3 to{50, 50, 0};
    const auto hit = world.sweep(BODY, from, to);
    REQUIRE(hit.fraction < 1);
    const Vec3 at = end(from, to, hit.fraction);
    CHECK(std::hypot(at.x, at.y) <= BODY.radius + SKIN + 0.01);
    CHECK(std::hypot(at.x, at.y) >= BODY.radius);
}

TEST_CASE("UTA-0457 INV-4: a long sweep stops before a thin wall") {
    const auto world = worldOf({box({0, -400, -200}, {8, 400, 200})});
    const Vec3 from{-2048, 0, 0};
    const Vec3 to{2048, 0, 0};
    const auto hit = world.sweep(BODY, from, to);
    REQUIRE(hit.fraction < 1);
    const Vec3 at = end(from, to, hit.fraction);
    CHECK(at.x <= -BODY.radius);
    CHECK(world.fits(BODY, at));
}

TEST_CASE("UTA-0457 INV-4: fits says no where the body overlaps, yes SKIN away") {
    const auto world = worldOf({box({0, -400, -200}, {64, 400, 200})});
    CHECK(world.fits(BODY, {-BODY.radius - SKIN, 0, 0}));
    CHECK_FALSE(world.fits(BODY, {-BODY.radius + 1, 0, 0}));
}

TEST_CASE("UTA-0457 INV-5: a slide keeps the move's part along the wall") {
    // The wall's face is y = 0, facing +Y. The body starts just off it and
    // moves 100 along it and 50 into it.
    const auto world = worldOf({box({-1000, -64, -200}, {1000, 0, 200})});
    const Vec3 from{0, BODY.radius + 10, 0};
    const auto move = world.slide(BODY, from, {100, -50, 0});
    CHECK(move.end.x == Catch::Approx(100).margin(0.01));
    CHECK(move.end.y - BODY.radius == Catch::Approx(SKIN).margin(0.01));
    CHECK(move.hits.size() == 2);
}

TEST_CASE("UTA-0457 INV-5: a slide into an inside corner stops in the corner") {
    // Walls at y = 0 (facing +Y) and x = 0 (facing +X).
    const auto world = worldOf({box({-1000, -64, -200}, {1000, 0, 200}), box({-64, 1, -200}, {0, 1000, 200})});
    const Vec3 from{100, 100, 0};
    const auto move = world.slide(BODY, from, {-200, -150, 0});
    CHECK(move.end.x - BODY.radius == Catch::Approx(SKIN).margin(0.02));
    CHECK(move.end.y - BODY.radius == Catch::Approx(SKIN).margin(0.02));
    CHECK(world.fits(BODY, move.end));
}

TEST_CASE("UTA-0457 INV-5: a slide into a sharp corner follows the crease") {
    // Wall A's face is y = 0, facing +Y. Wall B faces 30 degrees below +X, so
    // the corner between them is sharper than square. A body falling into it
    // slides along A, meets B, and falls on down the crease. Sliding along B
    // alone would turn it back into A and stop it there.
    const auto world = worldOf({box({-1000, -64, -400}, {1000, 0, 400}), box({-64, -400, -400}, {0, 400, 400}, -30)});
    const Vec3 from{150, 60, 0};
    const auto move = world.slide(BODY, from, {-300, -120, -100});
    CHECK(move.end.z == Catch::Approx(-100).margin(1));
    CHECK(world.fits(BODY, move.end));
}

TEST_CASE("UTA-0457 INV-6: a body starting inside says so and does not move") {
    // Its centre in empty space, its side 7 units into a wall.
    const auto world = worldOf({box({0, -400, -200}, {64, 400, 200})});
    const Vec3 from{-10, 0, 0};
    const auto hit = world.sweep(BODY, from, {-500, 0, 0});
    CHECK(hit.startsInside);
    CHECK(hit.fraction == 0);
    CHECK(dot(hit.normal, {-1, 0, 0}) == Catch::Approx(1).margin(1e-3));
    const auto move = world.slide(BODY, from, {-500, 0, 0});
    CHECK(move.end == from);
    CHECK_FALSE(world.fits(BODY, from));
}

TEST_CASE("UTA-0457 INV-6: a body whose centre is in solid gives no way out") {
    // Half through a wall, centre inside it: the face would push it deeper.
    const auto world = worldOf({box({0, -400, -200}, {64, 400, 200})});
    const auto hit = world.sweep(BODY, {10, 0, 0}, {-500, 0, 0});
    CHECK(hit.startsInside);
    CHECK(hit.normal == Vec3{});
}

TEST_CASE("UTA-0457 INV-6: a body wholly inside solid, touching no surface, says so") {
    const auto world = worldOf({box({-500, -500, -500}, {500, 500, 500})});
    const auto hit = world.sweep(BODY, {0, 0, 0}, {100, 0, 0});
    CHECK(hit.startsInside);
    CHECK(hit.fraction == 0);
    CHECK_FALSE(world.fits(BODY, {0, 0, 0}));
}

TEST_CASE("UTA-0457 INV-7: a mover blocks where it is placed") {
    // A door 128 wide, 16 thick, in its pivot space; placed at x = 300.
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({});
    collision.movers.push_back({7, solidBlocks({box({-8, -64, -100}, {8, 64, 100})})});
    uta::ubundle::MoverShape shape;
    shape.exportIndex = 7;
    shape.location = {300, 0, 0};
    const std::vector<uta::ubundle::MoverShape> movers{shape};
    auto built = BodyWorld::build(collision, movers);
    REQUIRE(built.has_value());
    auto& world = *built;
    REQUIRE(world.moverCount() == 1);

    const Vec3 from{0, 0, 0};
    CHECK(world.sweep(BODY, from, {600, 0, 0}).fraction < 1);

    // Moved aside: the old place is open, the new one blocks.
    REQUIRE(world.placeMover(0, {300, 500, 0}, {0, 0, 0}).has_value());
    CHECK(world.sweep(BODY, from, {600, 0, 0}).fraction == 1);
    CHECK(world.sweep(BODY, {0, 500, 0}, {600, 500, 0}).fraction < 1);

    // Turned a quarter: it now lies along X, so a sweep along Y at x = 300
    // hits it, and one along X past its end does not.
    REQUIRE(world.placeMover(0, {300, 0, 0}, {0, 16384, 0}).has_value());
    CHECK(world.sweep(BODY, {300, -300, 0}, {300, 300, 0}).fraction < 1);
    CHECK(world.sweep(BODY, {0, 40, 0}, {600, 40, 0}).fraction == 1);

    CHECK_FALSE(world.placeMover(1, {0, 0, 0}, {0, 0, 0}).has_value());
}

TEST_CASE("UTA-0457 INV-7: a mover with no MOVR shape refuses the build") {
    uta::ubundle::Collision collision;
    collision.movers.push_back({7, solidBlocks({box({-8, -64, -100}, {8, 64, 100})})});
    CHECK_FALSE(BodyWorld::build(collision, {}).has_value());
}

TEST_CASE("UTA-0462: a mover with no surface builds, keeps its index and blocks nothing") {
    // AS-Guardia's Mover8 is crossed not-solid sheets, so its mesh is empty.
    // The worst such tree is one of no nodes and not outside: solid at every
    // point, so a check that read it would put every body in solid.
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({});
    collision.movers.push_back({7, uta::ubundle::CollisionTree{}});
    collision.movers.push_back({9, solidBlocks({box({-8, -64, -100}, {8, 64, 100})})});
    std::vector<uta::ubundle::MoverShape> movers(2);
    movers[0].exportIndex = 7;
    movers[1].exportIndex = 9;
    movers[1].location = {300, 0, 0};
    auto built = BodyWorld::build(collision, movers);
    REQUIRE(built.has_value());
    auto& world = *built;
    REQUIRE(world.moverCount() == 2);

    const Vec3 from{0, 0, 0};
    const auto blocked = world.sweep(BODY, from, {600, 0, 0});
    CHECK_FALSE(blocked.startsInside);
    CHECK(blocked.fraction < 1); // mover 1, the door, still blocks
    CHECK(world.sweep(BODY, from, {0, 600, 0}).fraction == 1);

    // Index 0 is still the surfaceless mover: placing and turning it works,
    // and leaves the door at index 1 where it was.
    REQUIRE(world.placeMover(0, {0, 300, 0}, {0, 16384, 0}).has_value());
    CHECK(world.sweep(BODY, from, {0, 600, 0}).fraction == 1);
    REQUIRE(world.placeMover(1, {300, 500, 0}, {0, 0, 0}).has_value());
    CHECK(world.sweep(BODY, from, {600, 0, 0}).fraction == 1);
}

TEST_CASE("UTA-0457 INV-8: a fixed sequence of sweeps hashes to one constant") {
    // A room of turned walls and a pillar. Nothing here reads std::sin or
    // std::cos, which no standard promises are correctly rounded: the walls
    // turn by 3-4-5 and 7-24-25 triangles' angles, the moves head along
    // Pythagorean triples, and no mover turns. (A first version turned walls
    // and moves by degrees, and MSVC's hash differed from GCC's and Clang's.)
    const auto world = worldOf({box({-400, -400, -64}, {400, 400, 0}), box({300, -400, 0}, {364, 400, 200}, 0.8, 0.6),
                                box({-60, 80, 0}, {20, 140, 200}, 0.28, 0.96)});
    std::uint64_t hash = 14695981039346656037ULL; // FNV-1a
    const auto mix = [&](double v) {
        const auto bits = std::bit_cast<std::uint64_t>(v);
        for (int k = 0; k < 8; ++k) {
            hash ^= (bits >> (8 * k)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };
    // Six triples, each in four quadrants: 24 headings, every one exact.
    constexpr std::array<std::array<double, 3>, 6> TRIPLES{{
        {3, 4, 5}, {5, 12, 13}, {8, 15, 17}, {7, 24, 25}, {20, 21, 29}, {12, 35, 37}}};
    for (int k = 0; k < 24; ++k) {
        const auto& t = TRIPLES[static_cast<std::size_t>(k % 6)];
        const double sx = (k / 6) % 2 == 0 ? 1 : -1, sy = (k / 12) == 0 ? 1 : -1;
        const Vec3 from{-200.0 + 7 * k, -150.0 + 3 * k, 40};
        const Vec3 delta = Vec3{sx * t[0] / t[2], sy * t[1] / t[2], 0} * 600 + Vec3{0, 0, -30};
        const auto move = world.slide(BODY, from, delta);
        mix(move.end.x);
        mix(move.end.y);
        mix(move.end.z);
        for (const auto& hit : move.hits) {
            mix(hit.fraction);
            mix(hit.normal.x);
            mix(hit.normal.y);
            mix(hit.normal.z);
        }
    }
    INFO("hash " << hash);
    CHECK(hash == 6334859207609988200ULL);
}
