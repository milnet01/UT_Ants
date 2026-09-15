// UTA-0156's builder cases: buildZones over a Model and placements built in
// memory, with UTA-0015's fog flag.
//
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.3, INV-3, and
// docs/specs/UTA-0015-volumetric-fog.md SS 4.2, INV-2. The container's cases
// are tests/unit/BundleZonesTest.cpp.
//
// NO PACKAGE IS BUILT. buildZones takes a upkg::Model and PLAC's placements, so
// both are assembled field by field and each case names exactly the zone,
// actor or default it is about.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/Zones.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using uta::ubake::buildZones;
using uta::ubundle::ActorClass;
using uta::ubundle::ActorPlacement;
using uta::ubundle::Placements;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;
using uta::ubundle::Zone;
using uta::upkg::Model;
using uta::upkg::ObjectReference;
using uta::upkg::ZoneProperties;

namespace {

PropertyRecord byteRecord(std::string name, std::uint8_t value) {
    PropertyRecord record;
    record.name = std::move(name);
    record.kind = ValueKind::Byte;
    record.value = value;
    return record;
}

PropertyRecord boolRecord(std::string name, bool value) {
    PropertyRecord record;
    record.name = std::move(name);
    record.kind = ValueKind::Bool;
    record.value = value;
    return record;
}

ActorClass classOf(std::string path, std::vector<std::string> ancestry, std::vector<PropertyRecord> defaults = {}) {
    ActorClass actorClass;
    actorClass.path = std::move(path);
    actorClass.resolved = true;
    actorClass.ancestry = std::move(ancestry);
    actorClass.defaults = std::move(defaults);
    return actorClass;
}

ActorPlacement actorOf(std::uint32_t exportIndex, std::uint32_t classIndex, std::vector<PropertyRecord> properties) {
    ActorPlacement actor;
    actor.exportIndex = exportIndex;
    actor.path = "dm-fixture.actor" + std::to_string(exportIndex);
    actor.classIndex = classIndex;
    actor.properties = std::move(properties);
    return actor;
}

/// A LevelInfo at export 3 setting brightness 90, hue 12, saturation 200 and
/// bFogZone; a ZoneInfo at export 5 setting 40, 3, 100 and clearing bFogZone;
/// and, at export 8, an actor of a ZoneInfo subclass setting nothing, whose
/// class defaults brightness to 7 and sets bFogZone.
Placements placements() {
    Placements out;
    out.classes = {classOf("engine.levelinfo", {"engine.zoneinfo", "engine.info", "engine.actor"}),
                   classOf("engine.zoneinfo", {"engine.info", "engine.actor"}),
                   classOf("mypkg.darkzone", {"engine.zoneinfo", "engine.info", "engine.actor"},
                           {byteRecord("AmbientBrightness", 7), boolRecord("bFogZone", true)})};
    out.actors = {actorOf(3, 0, {byteRecord("AmbientBrightness", 90), byteRecord("AmbientHue", 12),
                                 byteRecord("AmbientSaturation", 200), boolRecord("bFogZone", true)}),
                  actorOf(5, 1, {byteRecord("AmbientBrightness", 40), byteRecord("AmbientHue", 3),
                                 byteRecord("AmbientSaturation", 100), boolRecord("bFogZone", false)}),
                  actorOf(8, 2, {})};
    return out;
}

/// Zone 0 names no actor; zone 1 the ZoneInfo at export 5; zone 2 the actor at
/// export 8. A reference's raw value is its export index plus one.
Model threeZones() {
    Model model;
    model.zones = {ZoneProperties{ObjectReference{0}, 0, 0}, ZoneProperties{ObjectReference{6}, 0, 0},
                   ZoneProperties{ObjectReference{9}, 0, 0}};
    return model;
}

} // namespace

TEST_CASE("INV-3: a zone takes its own actor's ambient and a zone with none the LevelInfo's", "[ubake][zone]") {
    const std::vector<Zone> zones = buildZones(threeZones(), placements());
    REQUIRE(zones.size() == 3);
    CHECK(int(zones[0].brightness) == 90);
    CHECK(int(zones[0].hue) == 12);
    CHECK(int(zones[0].saturation) == 200);
    CHECK(int(zones[1].brightness) == 40);
    CHECK(int(zones[1].hue) == 3);
    CHECK(int(zones[1].saturation) == 100);
}

TEST_CASE("INV-3: an actor setting nothing takes its class's default", "[ubake][zone]") {
    const std::vector<Zone> zones = buildZones(threeZones(), placements());
    REQUIRE(zones.size() == 3);
    CHECK(int(zones[2].brightness) == 7);
    CHECK(int(zones[2].hue) == 0);
    CHECK(int(zones[2].saturation) == 0);
}

TEST_CASE("INV-3: with no LevelInfo a zone naming no actor is zero", "[ubake][zone]") {
    Placements without = placements();
    without.actors.erase(without.actors.begin()); // the LevelInfo at export 3
    const std::vector<Zone> zones = buildZones(threeZones(), without);
    REQUIRE(zones.size() == 3);
    CHECK(int(zones[0].brightness) == 0);
    CHECK(int(zones[0].fog) == 0);
    CHECK(int(zones[1].brightness) == 40);
}

TEST_CASE("INV-3: a zone naming an export with no placement takes the LevelInfo's", "[ubake][zone]") {
    Model model = threeZones();
    model.zones[1].zoneActor = ObjectReference{100};
    const std::vector<Zone> zones = buildZones(model, placements());
    REQUIRE(zones.size() == 3);
    CHECK(int(zones[1].brightness) == 90);
}

TEST_CASE("INV-3: a Model with no zones gives one entry from the LevelInfo", "[ubake][zone]") {
    const Model noZones;
    const std::vector<Zone> zones = buildZones(noZones, placements());
    REQUIRE(zones.size() == 1);
    CHECK(int(zones[0].brightness) == 90);
}

TEST_CASE("UTA-0015 INV-2: a zone takes its own actor's fog flag and a zone with none the LevelInfo's",
          "[ubake][zone]") {
    const std::vector<Zone> zones = buildZones(threeZones(), placements());
    REQUIRE(zones.size() == 3);
    CHECK(int(zones[0].fog) == 1); // the LevelInfo sets it
    CHECK(int(zones[1].fog) == 0); // its own record clears it

    Placements set = placements();
    set.actors[0].properties.back() = boolRecord("bFogZone", false);
    set.actors[1].properties.back() = boolRecord("bFogZone", true);
    const std::vector<Zone> flipped = buildZones(threeZones(), set);
    CHECK(int(flipped[0].fog) == 0);
    CHECK(int(flipped[1].fog) == 1);
}

TEST_CASE("UTA-0015 INV-2: an actor setting no fog flag takes its class's default", "[ubake][zone]") {
    CHECK(int(buildZones(threeZones(), placements())[2].fog) == 1);

    Placements noDefault = placements();
    noDefault.classes[2].defaults.pop_back();
    CHECK(int(buildZones(threeZones(), noDefault)[2].fog) == 0);
}
