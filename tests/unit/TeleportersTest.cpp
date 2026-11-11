// UTA-0205: teleporters by UT99's Teleporter.Touch and Accept (Engine.u, UT
// 469). A body stepping into an enabled teleporter lands at the one whose Tag
// its URL names, turned by the destination's flags, and does not bounce back.

#include "BlockFixture.h"

#include "ut-ants/Walker.h"
#include "uworld/Teleporters.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;
using uta::ubundle::ActorClass;
using uta::ubundle::ActorPlacement;
using uta::ubundle::Placements;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;
using uta::uworld::BodySize;
using uta::uworld::BodyWorld;
using uta::uworld::Teleporter;
using uta::uworld::Teleporters;
using uta::uworld::Vec3;

namespace {

BodyWorld floorWorld() {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-4000, -4000, -64}, {4000, 4000, 0})});
    auto world = BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    return std::move(*world);
}

/// UT's default teleporter: 18 across, 40 high, enabled, turning yaw.
Teleporter teleporter(Vec3 at, std::string tag, std::string url, std::int32_t yaw = 0) {
    Teleporter t;
    t.location = at;
    t.tag = std::move(tag);
    t.url = std::move(url);
    t.yaw = yaw;
    t.sends = !t.url.empty();
    t.radius = 18;
    t.height = 40;
    t.changesYaw = true;
    return t;
}

bool near(const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6 && std::abs(a.z - b.z) < 1e-6;
}

PropertyRecord record(std::string name, ValueKind kind, decltype(PropertyRecord::value) value) {
    PropertyRecord r;
    r.name = std::move(name);
    r.kind = kind;
    r.value = std::move(value);
    return r;
}

} // namespace

TEST_CASE("UTA-0205: a body stepping into a teleporter lands at the one its URL names") {
    const BodyWorld world = floorWorld();
    Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), teleporter({1000, 0, 40}, "b", "a")});
    const BodySize size;

    // Not yet touching: nothing happens.
    CHECK_FALSE(teleporters.touch(world, size, {-100, 0, 40}, {}, 0));
    const auto arrival = teleporters.touch(world, size, {-30, 0, 40}, {}, 0);
    REQUIRE(arrival);
    CHECK(near(arrival->location, {1000, 0, 40}));

    // Standing in the destination, which leads back, does not bounce back;
    // leaving it and stepping in again does.
    CHECK_FALSE(teleporters.touch(world, size, {1000, 0, 40}, {}, 0));
    CHECK_FALSE(teleporters.touch(world, size, {1030, 0, 40}, {}, 0));
    CHECK_FALSE(teleporters.touch(world, size, {1200, 0, 40}, {}, 0));
    const auto back = teleporters.touch(world, size, {1030, 0, 40}, {}, 0);
    REQUIRE(back);
    CHECK(near(back->location, {0, 0, 40}));
}

TEST_CASE("UTA-0205: the touch is UT's cylinder overlap, strict on both axes") {
    const BodyWorld world = floorWorld();
    const BodySize size; // 17 across, 39 high
    // 18 + 17 = 35 apart sideways, 40 + 39 = 79 apart in height: just apart.
    for (const Vec3& at : {Vec3{35, 0, 40}, Vec3{0, 35, 40}, Vec3{0, 0, 40 + 79}, Vec3{0, 0, 40 - 79}}) {
        Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), teleporter({1000, 0, 40}, "b", "")});
        CHECK_FALSE(teleporters.touch(world, size, at, {}, 0));
    }
    for (const Vec3& at : {Vec3{34.9, 0, 40}, Vec3{24, 24, 40}, Vec3{0, 0, 40 + 78.9}}) {
        Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), teleporter({1000, 0, 40}, "b", "")});
        CHECK(teleporters.touch(world, size, at, {}, 0));
    }
}

TEST_CASE("UTA-0205: what a teleporter does nothing for") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    const Vec3 inside{0, 0, 40};

    Teleporter off = teleporter({0, 0, 40}, "a", "b");
    off.enabled = false;
    Teleporter toLevel = teleporter({0, 0, 40}, "a", "otherlevel/b");
    Teleporter toNet = teleporter({0, 0, 40}, "a", "b#start");
    Teleporter noCollision = teleporter({0, 0, 40}, "a", "b");
    noCollision.sends = false;
    Teleporter toNowhere = teleporter({0, 0, 40}, "a", "c");
    for (const Teleporter& source : {off, toLevel, toNet, noCollision, toNowhere}) {
        Teleporters teleporters({source, teleporter({1000, 0, 40}, "b", "")});
        CHECK_FALSE(teleporters.touch(world, size, inside, {}, 0));
    }
    // A URL with a / or a # names another level, even where a Tag here
    // spells the same.
    for (const char* url : {"otherlevel/b", "b#start"}) {
        Teleporters teleporters({teleporter({0, 0, 40}, "a", url), teleporter({1000, 0, 40}, url, "")});
        CHECK_FALSE(teleporters.touch(world, size, inside, {}, 0));
    }
    // UT skips the teleporter itself among those carrying the Tag.
    Teleporters alone({teleporter({0, 0, 40}, "b", "b")});
    CHECK_FALSE(alone.touch(world, size, inside, {}, 0));

    // A destination the body does not fit in, here inside the floor.
    Teleporters buried({teleporter({0, 0, 40}, "a", "b"), teleporter({1000, 0, -20}, "b", "")});
    CHECK_FALSE(buried.touch(world, size, inside, {}, 0));
}

TEST_CASE("UTA-0205: the destination turns the body and its velocity, as Accept does") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    const Vec3 inside{0, 0, 40};

    // Source faces 0, destination a quarter turn: UT turns the body by the
    // destination's yaw + half a turn - the source's. A body facing and
    // moving along +X comes out facing three quarters round, -Y, its speed
    // along +X now along -Y. What was across the old heading is not turned:
    // UT leaves it pointing where it was, here +Y.
    {
        Teleporters teleporters({teleporter({0, 0, 40}, "a", "b", 0), teleporter({1000, 0, 40}, "b", "", 16384)});
        const auto arrival = teleporters.touch(world, size, inside, {300, 20, -50}, 0);
        REQUIRE(arrival);
        CHECK(arrival->yaw == 49152);
        CHECK(near(arrival->velocity, {0, 20 - 300, -50}));
    }
    // A negative sum wraps into one turn.
    {
        Teleporters teleporters({teleporter({0, 0, 40}, "a", "b", 60000), teleporter({1000, 0, 40}, "b", "", 0)});
        const auto arrival = teleporters.touch(world, size, inside, {}, 0);
        REQUIRE(arrival);
        CHECK(std::abs(arrival->yaw - (32768 - 60000 + 65536)) < 1e-9);
    }
    // Without bChangesYaw the yaw and velocity pass through; the reverse
    // flags flip their axes; bChangesVelocity sets TargetVelocity.
    {
        Teleporter dest = teleporter({1000, 0, 40}, "b", "", 16384);
        dest.changesYaw = false;
        dest.reversesX = true;
        dest.reversesZ = true;
        Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), dest});
        const auto arrival = teleporters.touch(world, size, inside, {1, 2, 3}, 500);
        REQUIRE(arrival);
        CHECK(arrival->yaw == 500);
        CHECK(near(arrival->velocity, {-1, 2, -3}));
    }
    {
        Teleporter dest = teleporter({1000, 0, 40}, "b", "");
        dest.changesVelocity = true;
        dest.reversesX = true;
        dest.targetVelocity = {7, 8, 9};
        Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), dest});
        const auto arrival = teleporters.touch(world, size, inside, {1, 2, 3}, 0);
        REQUIRE(arrival);
        CHECK(near(arrival->velocity, {7, 8, 9}));
    }
}

TEST_CASE("UTA-0205: several destinations with the Tag are each reached") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), teleporter({1000, 0, 40}, "b", ""),
                             teleporter({-1000, 0, 40}, "b", "")});
    bool east = false, west = false;
    for (int i = 0; i < 40; ++i) {
        CHECK_FALSE(teleporters.touch(world, size, {200, 200, 40}, {}, 0)); // step out
        const auto arrival = teleporters.touch(world, size, {0, 0, 40}, {}, 0);
        REQUIRE(arrival);
        east = east || arrival->location.x > 0;
        west = west || arrival->location.x < 0;
    }
    CHECK(east);
    CHECK(west);
}

TEST_CASE("UTA-0205: a teleporter's fields come from the actor, else its class") {
    Placements placements;
    ActorClass base;
    base.path = "engine.teleporter";
    base.resolved = true;
    base.defaults = {record("bCollideActors", ValueKind::Bool, true), record("bEnabled", ValueKind::Bool, true),
                     record("bChangesYaw", ValueKind::Bool, true),
                     record("CollisionRadius", ValueKind::Float, 18.0f),
                     record("CollisionHeight", ValueKind::Float, 40.0f)};
    ActorClass visible = base;
    visible.path = "botpack.visibleteleporter";
    visible.ancestry = {"engine.teleporter"};
    visible.defaults[1] = record("bEnabled", ValueKind::Bool, false);
    ActorClass pad = base;
    pad.path = "mymap.jumppad";
    pad.ancestry = {"engine.teleporter"};
    ActorClass light;
    light.path = "engine.light";
    placements.classes = {visible, light, base, pad}; // ascending by path, as a bundle holds them

    ActorPlacement source;
    source.exportIndex = 1;
    source.classIndex = 2;
    source.properties = {record("Location", ValueKind::Vector, std::array<float, 3>{1, 2, 3}),
                         record("Rotation", ValueKind::Rotator, std::array<std::int32_t, 3>{0, 16384, 0}),
                         record("Tag", ValueKind::Name, std::string("Here")),
                         record("URL", ValueKind::String, std::string("There")),
                         record("CollisionRadius", ValueKind::Float, 30.0f)};
    ActorPlacement dest;
    dest.exportIndex = 2;
    dest.classIndex = 0;
    dest.properties = {record("tag", ValueKind::Name, std::string("THERE"))};
    ActorPlacement lamp;
    lamp.exportIndex = 3;
    lamp.classIndex = 1;
    ActorPlacement jumpPad;
    jumpPad.exportIndex = 4;
    jumpPad.classIndex = 3;
    jumpPad.properties = {record("Tag", ValueKind::Name, std::string("Pad")),
                          record("URL", ValueKind::String, std::string("There"))};
    placements.actors = {source, dest, lamp, jumpPad};

    const Teleporters teleporters = Teleporters::fromPlacements(placements);
    REQUIRE(teleporters.list().size() == 3);
    const Teleporter& a = teleporters.list()[0];
    CHECK(near(a.location, {1, 2, 3}));
    CHECK(a.yaw == 16384);
    CHECK(a.tag == "here");
    CHECK(a.url == "there");
    CHECK(a.enabled);
    CHECK(a.sends);
    CHECK(a.changesYaw);
    CHECK(a.radius == 30);
    CHECK(a.height == 40);
    const Teleporter& b = teleporters.list()[1];
    CHECK(b.tag == "there"); // UT's ~= ignores case
    CHECK_FALSE(b.enabled);  // the subclass's default
    CHECK_FALSE(b.sends); // no URL: a destination only
    const Teleporter& c = teleporters.list()[2];
    CHECK(c.tag == "pad");
    CHECK(c.url == "there");
    CHECK(c.enabled);
    CHECK_FALSE(c.sends); // a map's own subclass may replace Touch
}

TEST_CASE("UTA-0205: the walker walks into a teleporter and comes out at its destination") {
    const BodyWorld world = floorWorld();
    // Standing, a body's centre is 39 above the floor; a teleporter's is 40.
    Teleporters teleporters({teleporter({0, 300, 40}, "a", "b", 16384), teleporter({2000, 0, 40}, "b", "", 0)});
    uta::client::Walker walker({0, 0, 100}, 0, 16384, std::move(teleporters)); // facing +Y, towards it
    for (int frame = 0; frame < 60; ++frame) walker.update({}, 1.0 / 60, world);
    uta::uinput::Frame forward;
    forward.forward = 1;
    bool arrived = false;
    for (int frame = 0; frame < 120 && !arrived; ++frame) {
        walker.update(forward, 1.0 / 60, world);
        arrived = walker.state().location.x > 1000;
    }
    REQUIRE(arrived);
    // Facing +Y into a teleporter facing +Y, out of one facing +X: UT faces
    // the body along +X + half a turn + (+Y - +Y), so -X.
    walker.update({}, 1.0 / 60, world);
    const auto eye = walker.eye(90).camera(1920, 1080);
    CHECK(std::abs(eye.location[0] - 2000) < 30);
    CHECK(std::abs(eye.location[1]) < 30);
    CHECK(std::abs(eye.rotation[1] - 32768) < 2);
}
