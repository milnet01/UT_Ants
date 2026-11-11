// UTA-0205: teleporters by UT99's Teleporter.Touch and Accept (Engine.u, UT
// 469). A body stepping into an enabled teleporter lands at the one whose Tag
// its URL names, turned by the destination's flags, and does not bounce back.

#include "BlockFixture.h"

#include "ut-ants/Walker.h"
#include "uworld/Teleporters.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

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

namespace {

/// A swJumpPad as its class sets it: 64 across, 24 high, 45 degrees, aimed
/// at `target` with no ground trace.
Teleporter jumpPad(Vec3 at, Vec3 target, double angle = 45) {
    Teleporter t = teleporter(at, "pad", "there");
    t.radius = 64;
    t.height = 24;
    uta::uworld::JumpPad pad;
    pad.angle = angle;
    pad.traceGround = false;
    pad.target = target;
    pad.targetHeight = 0;
    t.jumpPad = pad;
    return t;
}

/// Where a launch from `feet` with `v` under gravity `g` is, at the target's
/// horizontal distance: the arc's height there, less the target's.
double missAtTarget(const Vec3& feet, const Vec3& v, const Vec3& target, double g) {
    const double out = std::hypot(target.x - feet.x, target.y - feet.y);
    const double t = out / std::hypot(v.x, v.y);
    return feet.z + v.z * t - 0.5 * g * t * t - target.z;
}

} // namespace

TEST_CASE("UTA-0463: a jump pad launches a body on an arc through its target") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    for (const double angle : {30.0, 45.0, 75.0}) {
        for (const Vec3 target : {Vec3{192, 0, 256}, Vec3{-600, 400, 0}, Vec3{0, 900, -200}}) {
            Teleporters teleporters({jumpPad({0, 0, 24}, target, angle)});
            const Vec3 at{10, 0, 39};
            const auto launched = teleporters.touch(world, size, at, {}, 1234, {}, 1.0 / 60);
            REQUIRE(launched);
            CHECK(launched->launched);
            CHECK(near(launched->location, at)); // it stays where it is
            CHECK(launched->yaw == 1234);        // and keeps its view
            // Aimed straight at the target, and coming down on it.
            const Vec3& v = launched->velocity;
            const Vec3 feet{at.x, at.y, at.z - size.halfHeight};
            const double across = (target.x - feet.x) * v.y - (target.y - feet.y) * v.x;
            CHECK(std::abs(across) / std::hypot(v.x, v.y) < 0.01 * std::hypot(target.x - feet.x, target.y - feet.y));
            CHECK(std::abs(missAtTarget(feet, v, target, 950)) < 2);
        }
    }
}

TEST_CASE("UTA-0463: the walker rides a jump pad to its target") {
    const BodyWorld world = floorWorld();
    // The target is the floor 900 units along +Y; the pad is on the floor.
    Teleporters teleporters({jumpPad({0, 300, 24}, {0, 1200, 0}, 60)});
    uta::client::Walker walker({0, 0, 100}, 0, 16384, std::move(teleporters));
    for (int frame = 0; frame < 60; ++frame) walker.update({}, 1.0 / 60, world);
    uta::uinput::Frame forward;
    forward.forward = 1;
    bool flying = false;
    for (int frame = 0; frame < 120 && !flying; ++frame) {
        walker.update(forward, 1.0 / 60, world);
        flying = walker.state().velocity.z > 100;
    }
    REQUIRE(flying);
    double highest = 0;
    for (int frame = 0; frame < 600 && walker.state().physics == uta::uworld::Physics::Falling; ++frame) {
        walker.update({}, 1.0 / 60, world);
        highest = std::max(highest, walker.state().location.z);
    }
    CHECK(highest > 200); // it flew, rather than walking there
    CHECK(walker.state().physics == uta::uworld::Physics::Walking);
    CHECK(std::abs(walker.state().location.x) < 20);
    CHECK(std::abs(walker.state().location.y - 1200) < 60);
}

TEST_CASE("UTA-0463: a jump pad waits its JumpWait before launching the same body again") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    Teleporters teleporters({jumpPad({0, 0, 24}, {500, 0, 0})});
    const Vec3 on{0, 0, 39}, off{300, 0, 39};
    REQUIRE(teleporters.touch(world, size, on, {}, 0, {}, 0.1));
    CHECK_FALSE(teleporters.touch(world, size, off, {}, 0, {}, 0.1));
    CHECK_FALSE(teleporters.touch(world, size, on, {}, 0, {}, 0.1)); // back 0.2 s later: too soon
    CHECK_FALSE(teleporters.touch(world, size, off, {}, 0, {}, 0.5));
    CHECK(teleporters.touch(world, size, on, {}, 0, {}, 0.5)); // 1.2 s after the launch
}

TEST_CASE("UTA-0463: a jump pad launches nobody without a target, for bots only, or for another team") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    const Vec3 on{0, 0, 39};
    Teleporter none = jumpPad({0, 0, 24}, {500, 0, 0});
    none.jumpPad->target.reset();
    Teleporter bots = jumpPad({0, 0, 24}, {500, 0, 0});
    bots.jumpPad->botsOnly = true;
    Teleporter team = jumpPad({0, 0, 24}, {500, 0, 0});
    team.jumpPad->teamOnly = true;
    for (const Teleporter& pad : {none, bots, team}) {
        Teleporters teleporters({pad});
        CHECK_FALSE(teleporters.touch(world, size, on, {}, 0, {}, 0.1));
    }
    // The player has no team: UT's 255, the one TeamNumber a team-only pad
    // launches it for.
    team.jumpPad->teamNumber = 255;
    Teleporters open({team});
    CHECK(open.touch(world, size, on, {}, 0, {}, 0.1));
}

TEST_CASE("UTA-0463: AM_Extremes alternates JumpAngle + AngleRand with JumpAngle; AM_Owned takes the steep one") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    const auto steepness = [&](Teleporters& teleporters, double dt) {
        const auto launched = teleporters.touch(world, size, {0, 0, 39}, {}, 0, {}, dt);
        REQUIRE(launched);
        CHECK_FALSE(teleporters.touch(world, size, {300, 0, 39}, {}, 0, {}, 0)); // step off
        return std::atan2(launched->velocity.z, std::hypot(launched->velocity.x, launched->velocity.y)) * 180
               / std::numbers::pi;
    };
    Teleporter extremes = jumpPad({0, 0, 24}, {800, 0, 0}, 30);
    extremes.jumpPad->angleRand = 40;
    extremes.jumpPad->angleMode = 1;
    Teleporters alternating({extremes});
    CHECK(std::abs(steepness(alternating, 2) - 70) < 0.1);
    CHECK(std::abs(steepness(alternating, 2) - 30) < 0.1);
    CHECK(std::abs(steepness(alternating, 2) - 70) < 0.1);

    Teleporter owned = extremes;
    owned.jumpPad->angleMode = 2;
    owned.jumpPad->teamNumber = 1;
    Teleporters others({owned});
    CHECK(std::abs(steepness(others, 2) - 70) < 0.1);
}

TEST_CASE("UTA-0463: a target above the jump angle's line raises the angle until the arc reaches it") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    // 100 out and 400 up: the line to it is about 76 degrees, above 45.
    Teleporter steep = jumpPad({0, 0, 24}, {100, 0, 400});
    steep.jumpPad->minAngle = 1;
    steep.jumpPad->maxAngle = 89;
    steep.jumpPad->minAngleCurve = 3;
    steep.jumpPad->minAngleFinder = 0.75;
    Teleporters teleporters({steep});
    const auto launched = teleporters.touch(world, size, {0, 0, 39}, {}, 0, {}, 0.1);
    REQUIRE(launched);
    const Vec3& v = launched->velocity;
    CHECK(std::atan2(v.z, std::hypot(v.x, v.y)) > 76 * std::numbers::pi / 180);
    CHECK(std::abs(missAtTarget({0, 0, 0}, v, {100, 0, 400}, 950)) < 2);
}

TEST_CASE("UTA-0463: a jump pad aims at the floor below its target when it traces the ground, raised by TargetZOffset") {
    const BodyWorld world = floorWorld();
    const BodySize size;
    Teleporter pad = jumpPad({0, 0, 24}, {600, 0, 300});
    pad.jumpPad->traceGround = true;
    pad.jumpPad->targetHeight = 24;
    pad.jumpPad->targetZOffset = 40;
    Teleporters teleporters({pad});
    const auto launched = teleporters.touch(world, size, {0, 0, 39}, {}, 0, {}, 0.1);
    REQUIRE(launched);
    CHECK(std::abs(missAtTarget({0, 0, 0}, launched->velocity, {600, 0, 40}, 950)) < 2); // the floor, raised 40
}

TEST_CASE("UTA-0463: a stock teleporter cannot send a body to a jump pad, whose Accept refuses") {
    const BodyWorld world = floorWorld();
    Teleporter pad = jumpPad({1000, 0, 40}, {2000, 0, 0}); // high enough for a body to fit
    pad.tag = "b";
    Teleporters teleporters({teleporter({0, 0, 40}, "a", "b"), pad});
    CHECK_FALSE(teleporters.touch(world, BodySize{}, {-30, 0, 40}, {}, 0));
}

TEST_CASE("UTA-0463: effects-only subclasses send, jump pads read their settings and target, others stay destinations") {
    Placements placements;
    ActorClass base;
    base.path = "engine.teleporter";
    base.resolved = true;
    base.defaults = {record("bCollideActors", ValueKind::Bool, true), record("bEnabled", ValueKind::Bool, true)};
    ActorClass effects = base;
    effects.path = "mh-somemap.silentteleporter";
    effects.ancestry = {"engine.teleporter"};
    ActorClass custom = effects;
    custom.path = "mh-somemap.playerteleporter";
    ActorClass pad = effects;
    pad.path = "dm-somemap.swjumppad";
    pad.defaults.push_back(record("JumpAngle", ValueKind::Float, 45.0f));
    pad.defaults.push_back(record("JumpWait", ValueKind::Float, 1.0f));
    pad.defaults.push_back(record("bTraceGround", ValueKind::Bool, true));
    ActorClass node;
    node.path = "engine.pathnode";
    placements.classes = {pad, node, base, custom, effects}; // ascending by path

    const auto actor = [](std::uint32_t index, std::uint32_t classIndex, std::vector<PropertyRecord> properties) {
        ActorPlacement a;
        a.exportIndex = index;
        a.classIndex = classIndex;
        a.properties = std::move(properties);
        return a;
    };
    const auto at = [](float x) { return record("Location", ValueKind::Vector, std::array<float, 3>{x, 0, 0}); };
    placements.actors = {
        actor(1, 4, {at(0), record("URL", ValueKind::String, std::string("Out"))}),
        actor(2, 3, {at(100), record("URL", ValueKind::String, std::string("Out"))}),
        actor(3, 1, {at(300), record("Tag", ValueKind::Name, std::string("up"))}),
        actor(4, 1, {at(400), record("Tag", ValueKind::Name, std::string("UP")),
                     record("CollisionHeight", ValueKind::Float, 50.0f)}),
        // Last, and tagged as its own URL names: it does not aim at itself.
        actor(5, 0, {at(200), record("Tag", ValueKind::Name, std::string("Up")),
                     record("URL", ValueKind::String, std::string("Up")),
                     record("JumpAngle", ValueKind::Float, 75.0f), record("JumpWait", ValueKind::Float, 9.0f),
                     record("AngleRandMode", ValueKind::Byte, std::uint8_t{2})}),
    };

    const Teleporters teleporters = Teleporters::fromPlacements(placements);
    REQUIRE(teleporters.list().size() == 3);
    CHECK(teleporters.list()[0].sends);       // effects only: Teleporter's Touch
    CHECK_FALSE(teleporters.list()[1].sends); // its own Touch, unread
    const Teleporter& jump = teleporters.list()[2];
    CHECK(jump.sends);
    REQUIRE(jump.jumpPad);
    CHECK(jump.jumpPad->angle == 75);
    CHECK(jump.jumpPad->angleMode == 2);
    CHECK(jump.jumpPad->traceGround);
    CHECK(jump.jumpPad->wait == 1); // default.JumpWait: the class's, not the actor's
    // The last actor tagged Up, of any class and itself excepted.
    REQUIRE(jump.jumpPad->target);
    CHECK(near(*jump.jumpPad->target, {400, 0, 0}));
    CHECK(jump.jumpPad->targetHeight == 50);
    CHECK_FALSE(teleporters.list()[0].jumpPad);
}
