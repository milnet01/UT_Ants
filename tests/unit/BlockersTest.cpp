// UTA-0377: docs/specs/UTA-0457-body-collision.md SS 4.7 and INV-10. A placed
// actor whose bCollideActors and bBlockPlayers are both true stops a body by
// its own cylinder; any other actor, a pawn, and an unread class block nothing.

#include "BlockFixture.h"

#include "uworld/BodyCollision.h"
#include "uworld/Blockers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;
using uta::ubundle::ActorClass;
using uta::ubundle::ActorPlacement;
using uta::ubundle::Placements;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;
using uta::uworld::Blocker;
using uta::uworld::blockersOf;
using uta::uworld::BodySize;
using uta::uworld::BodyWorld;
using uta::uworld::SKIN;
using uta::uworld::Vec3;

namespace {

PropertyRecord record(std::string name, ValueKind kind, decltype(PropertyRecord::value) value) {
    PropertyRecord r;
    r.name = std::move(name);
    r.kind = kind;
    r.value = std::move(value);
    return r;
}

PropertyRecord flag(std::string name, bool on) { return record(std::move(name), ValueKind::Bool, on); }

/// UT99's Barrel2 as its class says: collides, blocks players, 22 by 30.
ActorClass barrelClass(std::string path = "botpack.barrel2", std::vector<std::string> ancestry = {"engine.decoration",
                                                                                                  "engine.actor"}) {
    ActorClass c;
    c.path = std::move(path);
    c.resolved = true;
    c.ancestry = std::move(ancestry);
    c.defaults = {flag("bCollideActors", true), flag("bBlockPlayers", true),
                  record("CollisionRadius", ValueKind::Float, 22.0f), record("CollisionHeight", ValueKind::Float, 30.0f)};
    return c;
}

ActorPlacement placed(std::uint32_t exportIndex, std::uint32_t classIndex, Vec3 at,
                      std::vector<PropertyRecord> own = {}) {
    ActorPlacement a;
    a.exportIndex = exportIndex;
    a.path = "map.actor" + std::to_string(exportIndex);
    a.classIndex = classIndex;
    a.properties = std::move(own);
    a.properties.push_back(record("Location", ValueKind::Vector,
                                  std::array<float, 3>{float(at.x), float(at.y), float(at.z)}));
    return a;
}

BodyWorld floorWorld(const std::vector<Blocker>& blockers) {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-4000, -4000, -64}, {4000, 4000, 0})});
    auto world = BodyWorld::build(collision, {}, blockers);
    REQUIRE(world.has_value());
    return std::move(*world);
}

} // namespace

TEST_CASE("UTA-0457 INV-10: blockersOf keeps an actor only when both flags are true") {
    Placements placements;
    placements.classes.push_back(barrelClass());                                         // 0
    placements.classes.push_back(barrelClass("unrealshare.brute", {"engine.pawn", "engine.actor"})); // 1
    ActorClass unread = barrelClass("mymap.crate");
    unread.resolved = false;
    placements.classes.push_back(unread); // 2
    ActorClass lightBox = barrelClass("botpack.lightbox");
    lightBox.defaults[1] = flag("bBlockPlayers", false);
    placements.classes.push_back(lightBox); // 3

    placements.actors = {
        placed(1, 0, {100, 0, 30}),                                       // a barrel: blocks
        placed(2, 0, {200, 0, 30}, {flag("bBlockPlayers", false)}),       // its own flag says no
        placed(3, 0, {300, 0, 30}, {flag("bCollideActors", false)}),      // nor this one's
        placed(4, 1, {400, 0, 30}),                                       // a pawn
        placed(5, 2, {500, 0, 30}),                                       // an unread class
        placed(6, 3, {600, 0, 30}),                                       // its class says no
        placed(7, 3, {700, 0, 30}, {flag("bBlockPlayers", true)}),        // its own flag says yes
        placed(8, 0, {800, 0, 30}, {record("CollisionRadius", ValueKind::Float, 0.0f)}), // no size
    };

    const std::vector<Blocker> blockers = blockersOf(placements);
    REQUIRE(blockers.size() == 2);
    CHECK(blockers[0].location.x == 100);
    CHECK(blockers[0].radius == 22);
    CHECK(blockers[0].halfHeight == 30);
    CHECK(blockers[1].location.x == 700);
}

TEST_CASE("UTA-0457 INV-10: a body swept at a blocking actor stops SKIN off its side") {
    const BodySize size;
    const Blocker barrel{{0, 0, 30}, 22, 30};
    const Vec3 from{-200, 0, 40}, to{200, 0, 40};

    // Without it, the same sweep travels the whole way: the floor is below.
    CHECK(floorWorld({}).sweep(size, from, to).fraction == 1);

    const BodyWorld world = floorWorld({barrel});
    const auto hit = world.sweep(size, from, to);
    REQUIRE(hit.fraction < 1);
    const double side = from.x + (to.x - from.x) * hit.fraction + size.radius;
    CHECK(std::abs((barrel.location.x - barrel.radius) - side - SKIN) < 0.01);
    CHECK(hit.normal.x < -0.99);
}

TEST_CASE("UTA-0457 INV-10: a body falling onto a blocking actor rests SKIN above its top") {
    const BodySize size;
    const Blocker barrel{{0, 0, 30}, 22, 30};
    const BodyWorld world = floorWorld({barrel});
    const Vec3 from{0, 0, 300}, to{0, 0, -100};
    const auto hit = world.sweep(size, from, to);
    REQUIRE(hit.fraction < 1);
    const double feet = from.z + (to.z - from.z) * hit.fraction - size.halfHeight;
    CHECK(std::abs(feet - (barrel.location.z + barrel.halfHeight) - SKIN) < 0.01);

    // Standing there fits; standing in it does not.
    CHECK(world.fits(size, {0, 0, barrel.location.z + barrel.halfHeight + size.halfHeight + SKIN}));
    CHECK_FALSE(world.fits(size, {10, 0, 40}));
}
