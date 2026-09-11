// UTA-0110's builder cases: buildActors over synthetic maps.
//
// docs/specs/UTA-0110-lights-and-placements.md SS 4.5 and SS 4.6, INV-4 to
// INV-9. The container cases are tests/unit/BundleActorsTest.cpp.
//
// Each case builds a map with tests/unit/BakeFixture.h and bakes its actors
// against ActorPkg, built here: Grand, and Parent under it; a light class
// Lamp; a class Plain with no defaults; and an export Glow, which Lamp's Skin
// default names.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "support/UnrealPackageBuilder.h"
#include "ubake/Actors.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace uta::test::bake;
using uta::ErrorCode;
using uta::test::asBytes;
using uta::ubake::Actors;
using uta::ubake::buildActors;
using uta::ubundle::ActorClass;
using uta::ubundle::AncestryEnd;
using uta::ubundle::Light;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;
using uta::upkg::Level;
using uta::upkg::ObjectReference;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;

namespace {

std::vector<std::uint8_t> actorPkg() {
    Packer pkg;
    const std::int32_t glow =
        pkg.addExport(pkg.importClass("Engine", "Texture"), 0, "Glow", pkg.properties({}));
    const std::int32_t grand = pkg.addClass("Grand", 0, {intProperty("GrandOnly", 1)});
    pkg.addClass("Parent", grand, {intProperty("Shared", 10), intProperty("ParentOnly", 20)});
    pkg.addClass("Lamp", 0,
                 {byteProperty("LightType", 1), byteProperty("LightBrightness", 64),
                  objectProperty("Skin", glow)});
    pkg.addClass("Plain");
    return pkg.build();
}

const uta::upkg::ExportEntry* levelOf(const Package& map) {
    for (const uta::upkg::ExportEntry& entry : map.exports()) {
        if (entry.objectClass.kind() == ObjectReferenceKind::Null) continue;
        const auto name = map.objectName(entry.objectClass);
        if (name.has_value() && *name == "Level") return &entry;
    }
    return nullptr;
}

/// buildActors over the level `bytes` holds, with ActorPkg as the install.
uta::Result<Actors> actorsOf(const std::vector<std::uint8_t>& bytes, MemoryPackages& packages) {
    const auto map = Package::open(asBytes(bytes));
    REQUIRE(map.has_value());
    const uta::upkg::ExportEntry* const entry = levelOf(*map);
    REQUIRE(entry != nullptr);
    const auto level = uta::upkg::readLevel(*map, *entry);
    REQUIRE(level.has_value());
    return buildActors(*map, MAP_NAME, *level, packages.resolver());
}

const ActorClass* classAt(const Actors& actors, std::string_view path) {
    for (const ActorClass& actorClass : actors.placements.classes)
        if (actorClass.path == path) return &actorClass;
    return nullptr;
}

template <class T>
T valueOf(const PropertyRecord& record) {
    REQUIRE(std::holds_alternative<T>(record.value));
    return std::get<T>(record.value);
}

/// Every field of `light` other than those named is zero or false.
void lightIs(const Light& light, std::uint8_t type, std::uint8_t brightness, std::uint8_t hue,
             std::array<float, 3> location) {
    CHECK(light.type == type);
    CHECK(light.brightness == brightness);
    CHECK(light.hue == hue);
    CHECK(light.location == location);
    CHECK(light.rotation == std::array<std::int32_t, 3>{});
    CHECK(light.effect == 0);
    CHECK(light.saturation == 0);
    CHECK(light.radius == 0);
    CHECK(light.period == 0);
    CHECK(light.phase == 0);
    CHECK(light.cone == 0);
    CHECK(light.volumeBrightness == 0);
    CHECK(light.volumeRadius == 0);
    CHECK(light.volumeFog == 0);
    CHECK_FALSE(light.specialLit);
    CHECK_FALSE(light.actorShadows);
    CHECK_FALSE(light.corona);
    CHECK_FALSE(light.lensFlare);
}

/// buildActors over a one-actor map, with `slots` standing in for its level's
/// actor array.
void refusedFor(const std::vector<ObjectReference>& slots, std::string_view says) {
    MapBuilder map;
    map.addActorOfClass("ActorPkg", "Plain"); // export 0: the builder holds nothing before it
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    Level level;
    level.actors = slots;
    const auto actors = buildActors(*package, MAP_NAME, level, packages.resolver());
    REQUIRE_FALSE(actors.has_value());
    CHECK(actors.error().code() == ErrorCode::MalformedData);
    CHECK(actors.error().message().find(says) != std::string_view::npos);
}

} // namespace

TEST_CASE("INV-4: each actor is placed at its export slot with its own path and list",
          "[ubake][actors]") {
    MapBuilder map;
    // A palette and a texture take exports 0 and 1, so the two actors sit at 2
    // and 3 while their positions in the level's list are 0 and 1.
    map.addTexture(TextureSpec{"Wall", "", picture(1), false});
    map.addActorOfClass("ActorPkg", "Plain");
    map.addActorOfClass("ActorPkg", "Lamp", {intProperty("Second", 2), intProperty("First", 1)});
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    const std::vector<std::uint8_t> bytes = map.build();
    const auto actors = actorsOf(bytes, packages);
    REQUIRE(actors.has_value());

    const auto& placed = actors->placements.actors;
    REQUIRE(placed.size() == 2);
    CHECK(placed[0].exportIndex == 2);
    CHECK(placed[1].exportIndex == 3);
    CHECK(placed[0].path == "dm-fixture.plain0");
    CHECK(placed[1].path == "dm-fixture.lamp1");
    CHECK(placed[0].properties.empty());
    REQUIRE(placed[1].properties.size() == 2);
    CHECK(placed[1].properties[0].name == "Second");
    CHECK(placed[1].properties[1].name == "First");
    CHECK(actors->placements.classes[placed[0].classIndex].path == "actorpkg.plain");
    CHECK(actors->placements.classes[placed[1].classIndex].path == "actorpkg.lamp");
}

TEST_CASE("INV-5: actors of one class share one entry with the chain and merged defaults",
          "[ubake][actors]") {
    MapBuilder map;
    const std::int32_t child =
        map.addClass("Child", map.importClass("ActorPkg", "Parent"), {intProperty("Shared", 99)});
    map.addActor("Child0", child).addActor("Child1", child);
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    const std::vector<std::uint8_t> bytes = map.build();
    const auto actors = actorsOf(bytes, packages);
    REQUIRE(actors.has_value());

    REQUIRE(actors->placements.classes.size() == 1);
    const ActorClass& entry = actors->placements.classes[0];
    CHECK(entry.path == "dm-fixture.child");
    CHECK(entry.resolved);
    CHECK(entry.ancestry == std::vector<std::string>{"actorpkg.parent", "actorpkg.grand"});
    CHECK(entry.end == AncestryEnd::Root);
    CHECK(entry.missing.empty());
    // Sorted by folded name: the grandparent's, the parent's own, and the one
    // the child overrides.
    REQUIRE(entry.defaults.size() == 3);
    CHECK(entry.defaults[0].name == "GrandOnly");
    CHECK(valueOf<std::int32_t>(entry.defaults[0]) == 1);
    CHECK(entry.defaults[1].name == "ParentOnly");
    CHECK(valueOf<std::int32_t>(entry.defaults[1]) == 20);
    CHECK(entry.defaults[2].name == "Shared");
    CHECK(valueOf<std::int32_t>(entry.defaults[2]) == 99);

    REQUIRE(actors->placements.actors.size() == 2);
    CHECK(actors->placements.actors[0].classIndex == 0);
    CHECK(actors->placements.actors[1].classIndex == 0);
}

TEST_CASE("INV-6: a class not found is recorded as such and the bake goes on",
          "[ubake][actors]") {
    MapBuilder map;
    map.addActorOfClass("NoSuchPkg", "Thing");
    map.addActorOfClass("ActorPkg", "NoSuchThing");
    map.addActor("Orphan0", map.addClass("Orphan", map.importClass("NoSuchPkg", "Base")));
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    const std::vector<std::uint8_t> bytes = map.build();
    const auto actors = actorsOf(bytes, packages);
    REQUIRE(actors.has_value());

    const ActorClass* const thing = classAt(*actors, "nosuchpkg.thing");
    REQUIRE(thing != nullptr);
    CHECK_FALSE(thing->resolved);
    CHECK(thing->end == AncestryEnd::PackageMissing);
    CHECK(thing->missing == "nosuchpkg");
    CHECK(thing->ancestry.empty());
    CHECK(thing->defaults.empty());

    const ActorClass* const absent = classAt(*actors, "actorpkg.nosuchthing");
    REQUIRE(absent != nullptr);
    CHECK_FALSE(absent->resolved);
    CHECK(absent->end == AncestryEnd::ClassMissing);
    CHECK(absent->missing == "NoSuchThing");
    CHECK(absent->ancestry.empty());
    CHECK(absent->defaults.empty());

    // Found itself, with its parent's package absent: the same end and the
    // same `missing` as `thing`, told apart by `resolved`.
    const ActorClass* const orphan = classAt(*actors, "dm-fixture.orphan");
    REQUIRE(orphan != nullptr);
    CHECK(orphan->resolved);
    CHECK(orphan->end == AncestryEnd::PackageMissing);
    CHECK(orphan->missing == "nosuchpkg");
    CHECK(orphan->ancestry.empty());
}

TEST_CASE("INV-7: a light is its own value else its class's else zero", "[ubake][actors]") {
    MapBuilder map;
    map.addActorOfClass("ActorPkg", "Lamp",
                        {vectorProperty("Location", 1.0F, 2.0F, 3.0F), byteProperty("LightHue", 3),
                         byteProperty("LightBrightness", 200)});
    // LightRadius as an Int is the right name and the wrong type: passed over.
    map.addActorOfClass("ActorPkg", "Plain", {byteProperty("LightType", 1), intProperty("LightRadius", 50)});
    map.addActorOfClass("ActorPkg", "Plain");
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    const std::vector<std::uint8_t> bytes = map.build();
    const auto actors = actorsOf(bytes, packages);
    REQUIRE(actors.has_value());

    REQUIRE(actors->lights.size() == 2);
    const auto& placed = actors->placements.actors;
    REQUIRE(placed.size() == 3);
    CHECK(actors->lights[0].exportIndex == placed[0].exportIndex);
    CHECK(actors->lights[1].exportIndex == placed[1].exportIndex);
    lightIs(actors->lights[0], 1, 200, 3, {1.0F, 2.0F, 3.0F});
    lightIs(actors->lights[1], 1, 0, 0, {0.0F, 0.0F, 0.0F});
}

TEST_CASE("INV-8: an object value is the folded path of what it names", "[ubake][actors]") {
    MapBuilder map;
    const std::int32_t wall = map.addTexture(TextureSpec{"Wall", "Base", picture(1), false});
    const std::int32_t plate = map.importTexture("TexPkg", "Metal", "Plate");
    map.addActorOfClass("ActorPkg", "Lamp",
                        {objectProperty("Imported", plate), objectProperty("Local", wall),
                         objectProperty("Nothing", 0), nameProperty("Tag", "Lamp0")});
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    const std::vector<std::uint8_t> bytes = map.build();
    const auto actors = actorsOf(bytes, packages);
    REQUIRE(actors.has_value());

    REQUIRE(actors->placements.actors.size() == 1);
    const auto& properties = actors->placements.actors[0].properties;
    REQUIRE(properties.size() == 4);
    CHECK(properties[0].kind == ValueKind::Object);
    CHECK(valueOf<std::string>(properties[0]) == "texpkg.metal.plate");
    CHECK(valueOf<std::string>(properties[1]) == "dm-fixture.base.wall");
    CHECK(valueOf<std::string>(properties[2]).empty());
    CHECK(properties[3].kind == ValueKind::Name);
    CHECK(valueOf<std::string>(properties[3]) == "Lamp0");

    // A default read out of ActorPkg names an export of ActorPkg, so its path
    // starts with that package's name and not the map's.
    const ActorClass* const lamp = classAt(*actors, "actorpkg.lamp");
    REQUIRE(lamp != nullptr);
    const PropertyRecord* skin = nullptr;
    for (const PropertyRecord& record : lamp->defaults)
        if (record.name == "Skin") skin = &record;
    REQUIRE(skin != nullptr);
    CHECK(skin->kind == ValueKind::Object);
    CHECK(valueOf<std::string>(*skin) == "actorpkg.glow");
}

TEST_CASE("actors and lights are written in export order whatever the level's order",
          "[ubake][actors]") {
    // SS 4.4: both strictly ascending by export index. A level lists its
    // actors in its own order, so the builder sorts; this level lists them in
    // reverse, which a fixture built by MapBuilder never does.
    MapBuilder map;
    map.addActorOfClass("ActorPkg", "Lamp"); // export 0: the builder holds nothing before it
    map.addActorOfClass("ActorPkg", "Lamp"); // export 1
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    Level level;
    level.actors = {ObjectReference{2}, ObjectReference{1}};
    const auto actors = buildActors(*package, MAP_NAME, level, packages.resolver());
    REQUIRE(actors.has_value());
    REQUIRE(actors->placements.actors.size() == 2);
    CHECK(actors->placements.actors[0].exportIndex == 0);
    CHECK(actors->placements.actors[1].exportIndex == 1);
    REQUIRE(actors->lights.size() == 2);
    CHECK(actors->lights[0].exportIndex == 0);
    CHECK(actors->lights[1].exportIndex == 1);
}

TEST_CASE("INV-9: an actor slot naming an import refuses the bake", "[ubake][actors]") {
    refusedFor({ObjectReference{-1}}, "slot 0");
}

TEST_CASE("INV-9: an actor slot past the export table refuses the bake", "[ubake][actors]") {
    refusedFor({ObjectReference{1000}}, "slot 0");
}

TEST_CASE("INV-4: an actor named in two slots is placed once", "[ubake][actors]") {
    // SS 4.5 step 1, UTA-0124: UT99's own maps name one export in two slots,
    // and the second slot is skipped. The repeat is not in the next slot, so
    // a check that looks only at the slot before it would miss it.
    MapBuilder map;
    map.addActorOfClass("ActorPkg", "Lamp"); // export 0
    map.addActorOfClass("ActorPkg", "Lamp"); // export 1
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    MemoryPackages packages;
    packages.add("actorpkg", actorPkg());

    Level level;
    level.actors = {ObjectReference{1}, ObjectReference{2}, ObjectReference{1}};
    const auto actors = buildActors(*package, MAP_NAME, level, packages.resolver());
    REQUIRE(actors.has_value());
    REQUIRE(actors->placements.actors.size() == 2);
    CHECK(actors->placements.actors[0].exportIndex == 0);
    CHECK(actors->placements.actors[1].exportIndex == 1);
    REQUIRE(actors->lights.size() == 2);
    CHECK(actors->lights[0].exportIndex == 0);
    CHECK(actors->lights[1].exportIndex == 1);
}
