// The event-wiring graph, against fixtures.
//
// Locks INV-3 (an edge exists for EVERY actor carrying the tag an `Event`
// names, not merely the first) and INV-4 (an `Event` naming a tag no actor
// carries appears in `dangling` and produces no edge). INV-5 and INV-6 are
// declared reading checks for the reason SS 7 tier 2 gives.
//
// docs/specs/UTA-0006-navigation-and-wiring-graphs.md SS 7 tier 1.
//
// Real content supplies none of these on demand: a tag carried by exactly
// three actors, and an event naming a tag that is definitely absent, are
// constructed here so each rule can be wrong in exactly one named way.

#include "support/UnrealPackageBuilder.h"
#include "unav/Build.h"
#include "unav/Graphs.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

using uta::test::asBytes;
using uta::test::ClassExportWriter;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::unav::buildWiringGraph;
using uta::unav::WiringGraph;
using uta::upkg::Package;

namespace {

constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_TAG = 1;
constexpr std::int32_t NAME_EVENT = 2;
constexpr std::int32_t NAME_SWITCH_TAG = 3;
constexpr std::int32_t NAME_DOOR_TAG = 4;
constexpr std::int32_t NAME_MISSING_TAG = 5;
constexpr std::int32_t NAME_SWITCH = 6;
constexpr std::int32_t NAME_DOOR_A = 7;
constexpr std::int32_t NAME_DOOR_B = 8;
constexpr std::int32_t NAME_DOOR_C = 9;
constexpr std::int32_t NAME_ORPHAN = 10;
constexpr std::int32_t NAME_CLASS_WORD = 11;
constexpr std::int32_t NAME_SOME_CLASS = 12;
constexpr std::int32_t NAME_MAP_CLASS = 13;

/// Export positions the fixture builds. The class export at 2 is what pushes
/// every later node's POSITION away from its EXPORT INDEX, so the two spaces
/// cannot be confused for one another here either.
constexpr std::uint32_t EXPORT_SWITCH = 0;
constexpr std::uint32_t EXPORT_DOOR_A = 1;
constexpr std::uint32_t EXPORT_MAP_CLASS = 2;
constexpr std::uint32_t EXPORT_DOOR_B = 3;
constexpr std::uint32_t EXPORT_DOOR_C = 4;
constexpr std::uint32_t EXPORT_ORPHAN = 5;

/// An actor whose property list carries a `Tag`, an `Event`, or both. A name
/// index of `NAME_NONE` means the property is not written at all, which is what
/// "carries an explicit Tag" means in SS 4.3 -- the list stores only what an
/// author set.
void addActor(UnrealPackageBuilder& builder, std::int32_t nameIndex, std::int32_t tagValue,
              std::int32_t eventValue) {
    TaggedPropertyWriter properties;
    if (tagValue != NAME_NONE) {
        properties.addName(NAME_TAG, tagValue);
    }
    if (eventValue != NAME_NONE) {
        properties.addName(NAME_EVENT, eventValue);
    }

    ExportEntry entry;
    entry.objectClass = -1; // import 0: an actor's class is not null
    entry.objectName = nameIndex;
    entry.serialData = properties.build(NAME_NONE);
    builder.addExport(entry);
}

/// A class the map defines itself. SS 4.3 measured this as the ordinary case
/// rather than a rarity, and `readProperties` refuses such an export with
/// InvalidArgument -- so a scan that does not skip it fails on ordinary
/// content.
void addMapLocalClass(UnrealPackageBuilder& builder) {
    ClassExportWriter writer;
    writer.setFriendlyName(NAME_MAP_CLASS).setDefaults(TaggedPropertyWriter{}.build(NAME_NONE));

    ExportEntry entry;
    entry.objectClass = 0; // null: this is what makes it a class export
    entry.objectName = NAME_MAP_CLASS;
    entry.serialData = writer.build(68);
    builder.addExport(entry);
}

/// One switch firing a tag three doors share, one class export in the middle of
/// the actors, and one actor firing at nothing.
std::vector<std::uint8_t> wiringFixture() {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Tag");
    builder.addName("Event");
    builder.addName("SwitchTag");
    builder.addName("DoorTag");
    builder.addName("MissingTag");
    builder.addName("Switch");
    builder.addName("DoorA");
    builder.addName("DoorB");
    builder.addName("DoorC");
    builder.addName("Orphan");
    builder.addName("Class");
    builder.addName("SomeClass");
    builder.addName("MapClass");

    ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_CLASS_WORD;
    import.objectName = NAME_SOME_CLASS;
    builder.addImport(import);

    // The switch carries BOTH: it is fired at by nothing and it fires at three.
    addActor(builder, NAME_SWITCH, NAME_SWITCH_TAG, NAME_DOOR_TAG);
    addActor(builder, NAME_DOOR_A, NAME_DOOR_TAG, NAME_NONE);
    addMapLocalClass(builder);
    addActor(builder, NAME_DOOR_B, NAME_DOOR_TAG, NAME_NONE);
    addActor(builder, NAME_DOOR_C, NAME_DOOR_TAG, NAME_NONE);
    addActor(builder, NAME_ORPHAN, NAME_NONE, NAME_MISSING_TAG);
    return builder.build();
}

WiringGraph graphOf(const std::vector<std::uint8_t>& bytes) {
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    const auto graph = buildWiringGraph(*package);
    REQUIRE(graph.has_value());
    return *graph;
}

} // namespace

TEST_CASE("SS 4.3: a node is an actor carrying an explicit Tag or Event, and a class "
          "export is skipped",
          "[unav]") {
    const std::vector<std::uint8_t> bytes = wiringFixture();
    const WiringGraph graph = graphOf(bytes);

    // That `graphOf` returned at all is half the case: `readProperties` refuses
    // the class export with InvalidArgument, so a scan that propagated instead
    // of skipping fails here rather than producing a graph. SS 6 carries the
    // row.
    REQUIRE(graph.nodes.size() == 5);
    CHECK(graph.nodes[0].exportIndex == EXPORT_SWITCH);
    CHECK(graph.nodes[1].exportIndex == EXPORT_DOOR_A);
    CHECK(graph.nodes[2].exportIndex == EXPORT_DOOR_B);
    CHECK(graph.nodes[3].exportIndex == EXPORT_DOOR_C);
    CHECK(graph.nodes[4].exportIndex == EXPORT_ORPHAN);
    CHECK_FALSE(uta::unav::nodeOf(graph, EXPORT_MAP_CLASS).has_value());

    // SS 4.1: the tag is empty when the actor only fires.
    CHECK(graph.nodes[0].tag == "SwitchTag");
    CHECK(graph.nodes[1].tag == "DoorTag");
    CHECK(graph.nodes[4].tag.empty());
}

TEST_CASE("INV-3: an Event reaches EVERY actor carrying the tag, not the first", "[unav]") {
    const std::vector<std::uint8_t> bytes = wiringFixture();
    const WiringGraph graph = graphOf(bytes);

    const auto switchNode = uta::unav::nodeOf(graph, EXPORT_SWITCH);
    REQUIRE(switchNode.has_value());
    const auto fired = uta::unav::firedBy(graph, *switchNode);

    // Three, because three actors carry `DoorTag`. SS 2.1 measured that a tag
    // is shared far more often than not, so a builder resolving an event to a
    // single target loses real edges on most maps while every fixture with a
    // unique tag still passes. The three targets here differ only in sharing
    // the tag, so nothing else can reject this.
    REQUIRE(fired.size() == 3);
    CHECK(graph.nodes[fired[0].to].exportIndex == EXPORT_DOOR_A);
    CHECK(graph.nodes[fired[1].to].exportIndex == EXPORT_DOOR_B);
    CHECK(graph.nodes[fired[2].to].exportIndex == EXPORT_DOOR_C);
    for (const uta::unav::WiringEdge& edge : fired) {
        CHECK(edge.from == *switchNode);
        CHECK(edge.event == "DoorTag");
    }
}

TEST_CASE("INV-4: an Event naming a tag no actor carries is returned in dangling", "[unav]") {
    const std::vector<std::uint8_t> bytes = wiringFixture();
    const WiringGraph graph = graphOf(bytes);

    const auto orphan = uta::unav::nodeOf(graph, EXPORT_ORPHAN);
    REQUIRE(orphan.has_value());

    // Returned rather than discarded, because it is the only record that an
    // author wired something and the target went away -- SS 4.5. A builder that
    // invented an edge to nothing, or dropped the event silently, fails here.
    REQUIRE(graph.dangling.size() == 1);
    CHECK(graph.dangling[0].from == *orphan);
    CHECK(graph.dangling[0].event == "MissingTag");
    CHECK(uta::unav::firedBy(graph, *orphan).empty());

    // And nothing else dangled: the switch's own event resolved.
    CHECK(graph.edges.size() == 3);
}

TEST_CASE("SS 4.7: firing is the reverse grouping, and nodeOf is the only bridge", "[unav]") {
    const std::vector<std::uint8_t> bytes = wiringFixture();
    const WiringGraph graph = graphOf(bytes);

    const auto switchNode = uta::unav::nodeOf(graph, EXPORT_SWITCH);
    const auto doorB = uta::unav::nodeOf(graph, EXPORT_DOOR_B);
    REQUIRE(switchNode.has_value());
    REQUIRE(doorB.has_value());

    // One thing fires at DoorB, and it is the switch.
    REQUIRE(uta::unav::firing(graph, *doorB).size() == 1);
    CHECK(graph.nodes[uta::unav::firing(graph, *doorB)[0].from].exportIndex == EXPORT_SWITCH);
    // A door fires at nothing, and nothing fires at the switch.
    CHECK(uta::unav::firedBy(graph, *doorB).empty());
    CHECK(uta::unav::firing(graph, *switchNode).empty());

    // A position out of range is empty rather than undefined behaviour.
    CHECK(uta::unav::firedBy(graph, 999u).empty());
    CHECK(uta::unav::firing(graph, 999u).empty());
}
