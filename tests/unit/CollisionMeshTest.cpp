// Locks INV-1 of docs/specs/UTA-0457-body-collision.md: the collision mesh
// holds exactly the faces between empty and solid, each facing empty space.
//
// The fixture's outlines are wound however BlockFixture's face table winds
// them, half of them inward, so a mesh wound by outline order fails the
// facing case.

#include "BlockFixture.h"

#include "uworld/CollisionMesh.h"

#include <catch2/catch_test_macros.hpp>

using uta::test::blocks::box;
using uta::test::blocks::Sheet;
using uta::test::blocks::solidBlocks;
using uta::uworld::collisionMesh;
using uta::uworld::isEmpty;
using uta::uworld::Vec3;

namespace {

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

} // namespace

TEST_CASE("UTA-0457 INV-1: a sheet with empty space on both sides is dropped") {
    // A floor, and water across the room above it.
    const auto tree = solidBlocks({box({-200, -200, -40}, {200, 200, 0})},
                                  {Sheet{{Vec3{-100, -100, 50}, {100, -100, 50}, {100, 100, 50}, {-100, 100, 50}}}});
    const auto mesh = collisionMesh(tree);
    CHECK(mesh.sheets == 1);
    CHECK(mesh.buried == 0);
    CHECK(mesh.triangles.size() == 12); // the floor's six faces, two triangles each
}

TEST_CASE("UTA-0457 INV-1: a face covered by another block is dropped") {
    // Two blocks flush against each other: each one's touching face is buried.
    const auto tree = solidBlocks({box({0, 0, 0}, {100, 100, 100}), box({100, 0, 0}, {200, 100, 100})});
    const auto mesh = collisionMesh(tree);
    CHECK(mesh.buried == 2);
    CHECK(mesh.sheets == 0);
    CHECK(mesh.triangles.size() == 20);
}

TEST_CASE("UTA-0457 INV-1: a face partly covered is kept") {
    // A pillar stands on the middle of a floor, over the floor face's centroid.
    // The floor is still open around it, so its face stays; the pillar's
    // bottom, wholly on the floor, goes.
    const auto tree = solidBlocks({box({-200, -200, -40}, {200, 200, 0}), box({-20, -20, 0}, {20, 20, 120}, 30)});
    const auto mesh = collisionMesh(tree);
    CHECK(mesh.buried == 1);
    CHECK(mesh.triangles.size() == 22);
}

TEST_CASE("UTA-0457 INV-1: every kept face points into empty space") {
    const auto tree = solidBlocks({box({-200, -200, -40}, {200, 200, 0}), box({-20, -20, 0}, {20, 20, 120}, 30)});
    const auto mesh = collisionMesh(tree);
    REQUIRE(mesh.triangles.size() == 22);
    for (const auto& t : mesh.triangles) {
        const Vec3 a = mesh.points[t[0]], b = mesh.points[t[1]], c = mesh.points[t[2]];
        Vec3 n = cross(b - a, c - a);
        n = n * (1.0 / uta::uworld::length(n));
        const Vec3 centre = (a + b + c) * (1.0 / 3);
        INFO("triangle at " << centre.x << ", " << centre.y << ", " << centre.z);
        CHECK(isEmpty(tree, centre + n));
        CHECK_FALSE(isEmpty(tree, centre - n));
    }
}
