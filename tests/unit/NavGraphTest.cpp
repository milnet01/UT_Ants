// The navigation graph, against fixtures.
//
// Locks INV-1 (an edge names the actors its reach spec named, in the EXPORT
// index space) and INV-2 (an endpoint that does not resolve to a node is
// counted, and the unit counted is endpoints rather than specs). The two
// declared reading checks, INV-5 and INV-6, are not testable here for the
// reason SS 7 tier 2 gives: no fixture demonstrates the absence of a member or
// of a call that was never added.
//
// docs/specs/UTA-0006-navigation-and-wiring-graphs.md SS 7 tier 1.
//
// THE FIXTURE MAKES THREE INDEX SPACES DISAGREE, and that is the whole point of
// it. For every node an assertion names, its export index, its node position
// and its position in `Level::actors` are three different numbers -- so a
// builder reading the right value out of the wrong space produces a well-formed
// edge to the wrong actor, and an assertion on identity catches what a range
// check cannot.

#include "support/UnrealPackageBuilder.h"
#include "unav/Build.h"
#include "unav/Graphs.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

using uta::test::asBytes;
using uta::test::ClassExportWriter;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::LevelExportWriter;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::unav::buildNavGraph;
using uta::unav::NavGraph;
using uta::upkg::Package;
using uta::upkg::PackageResolver;

namespace {

constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_NAVIGATION_POINT = 1;
constexpr std::int32_t NAME_PATH_NODE = 2;
constexpr std::int32_t NAME_LIGHT = 3;
constexpr std::int32_t NAME_NODE_A = 4;
constexpr std::int32_t NAME_LAMP = 5;
constexpr std::int32_t NAME_NODE_B = 6;
constexpr std::int32_t NAME_NODE_C = 7;
constexpr std::int32_t NAME_NODE_D = 8;
constexpr std::int32_t NAME_LEVEL = 9;
constexpr std::int32_t NAME_CLASS_WORD = 10;

/// Export positions the fixture builds, named because every assertion below is
/// about which index space a value lives in and a bare integer says nothing
/// about that.
///
/// EXPORT 0 IS A NODE ON PURPOSE, and it is what makes INV-2's Null and Import
/// cases bite at all. `ObjectReference::index()` returns 0 for a null reference,
/// so a builder missing SS 4.2's `kind()` guard aliases such an endpoint onto
/// export 0 -- and export 0 has to be a real node for that to surface as an
/// EDGE rather than as another discard. Put a class export at 0 and the fixture
/// passes with the guard removed, which is the fixture testing nothing.
constexpr std::uint32_t EXPORT_NODE_A = 0;
constexpr std::uint32_t EXPORT_LAMP = 1;
constexpr std::uint32_t EXPORT_NODE_B = 2;
constexpr std::uint32_t EXPORT_NODE_C = 6;
constexpr std::uint32_t EXPORT_NODE_D = 7;

/// An object reference to an export, in the format's one-based spelling.
constexpr std::int32_t refTo(std::uint32_t exportIndex) {
    return static_cast<std::int32_t>(exportIndex) + 1;
}

/// Nothing is on disk in a unit test, and SS 4.3's walk stays inside the
/// fixture package: every class the actors name is defined in it. So a resolver
/// that supplies nothing is the honest one here, and it is also what proves the
/// map-local branch of the class resolution is the one taken.
const PackageResolver NOTHING_AVAILABLE =
    [](std::string_view) -> uta::Result<const Package*> { return nullptr; };

std::vector<std::uint8_t> emptyProperties() {
    return TaggedPropertyWriter{}.build(NAME_NONE);
}

/// A class export. `tableSuper` is the parent as an object reference, which is
/// where `readClass` takes it from (UTA-0005 INV-2).
void addClass(UnrealPackageBuilder& builder, std::int32_t nameIndex, std::int32_t tableSuper) {
    ClassExportWriter writer;
    writer.setFriendlyName(nameIndex).setDefaults(TaggedPropertyWriter{}.build(NAME_NONE));

    ExportEntry entry;
    entry.objectClass = 0; // null: this is what makes it a class export
    entry.super = tableSuper;
    entry.objectName = nameIndex;
    entry.serialData = writer.build(68);
    builder.addExport(entry);
}

void addActor(UnrealPackageBuilder& builder, std::int32_t nameIndex, std::int32_t classRef) {
    ExportEntry entry;
    entry.objectClass = classRef;
    entry.objectName = nameIndex;
    entry.serialData = emptyProperties();
    builder.addExport(entry);
}

/// The package every case here starts from: four navigation points, one actor
/// that is not one, the class chain SS 4.3's filter walks, and the one import an
/// INV-2 case needs. The Level export is added last, by `levelWith`, so each
/// case states only the reach specs it is about.
///
/// The classes are declared AFTER the actors that name them, which real content
/// also does -- an object reference names a table slot and implies no ordering.
/// It is what lets a navigation point sit at export 0.
UnrealPackageBuilder navFixture() {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("NavigationPoint");
    builder.addName("PathNode");
    builder.addName("Light");
    builder.addName("NodeA");
    builder.addName("Lamp");
    builder.addName("NodeB");
    builder.addName("NodeC");
    builder.addName("NodeD");
    builder.addName("Level");
    builder.addName("Class");

    // Import 0. It is the class the Level export names, and it is the IMPORT
    // endpoint INV-2's third case points a reach spec at.
    ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_CLASS_WORD;
    import.objectName = NAME_LEVEL;
    builder.addImport(import);

    constexpr std::uint32_t EXPORT_PATH_NODE_CLASS = 3;
    constexpr std::uint32_t EXPORT_NAVIGATION_POINT_CLASS = 4;
    constexpr std::uint32_t EXPORT_LIGHT_CLASS = 5;

    addActor(builder, NAME_NODE_A, refTo(EXPORT_PATH_NODE_CLASS)); // export 0
    addActor(builder, NAME_LAMP, refTo(EXPORT_LIGHT_CLASS));       // export 1
    addActor(builder, NAME_NODE_B, refTo(EXPORT_PATH_NODE_CLASS)); // export 2

    // `PathNode` descends from `NavigationPoint` and `Light` does not, so the
    // filter has to WALK rather than match a name: no actor here names the
    // literal `NavigationPoint` class, which is the shape SS 4.3 measured.
    addClass(builder, NAME_PATH_NODE, refTo(EXPORT_NAVIGATION_POINT_CLASS)); // export 3
    addClass(builder, NAME_NAVIGATION_POINT, 0);                             // export 4
    addClass(builder, NAME_LIGHT, 0);                                        // export 5

    addActor(builder, NAME_NODE_C, refTo(EXPORT_PATH_NODE_CLASS)); // export 6
    addActor(builder, NAME_NODE_D, refTo(EXPORT_PATH_NODE_CLASS)); // export 7
    return builder;
}

/// Close the fixture with a Level export carrying whatever reach specs the case
/// needs.
std::vector<std::uint8_t> levelWith(const std::function<void(LevelExportWriter&)>& addSpecs) {
    UnrealPackageBuilder builder = navFixture();

    LevelExportWriter writer;
    writer.setProperties(emptyProperties());
    // Interleaved null slots (UTA-0004 INV-9), and the order is chosen so that
    // no node's actor position equals either its export index or its node
    // position: NodeC is export 6 / node 2 / actor 0, NodeD export 7 / node 3 /
    // actor 1, NodeA export 0 / node 0 / actor 2, NodeB export 2 / node 1 /
    // actor 3.
    writer.addActor(0)
        .addActor(refTo(EXPORT_NODE_C))
        .addActor(0)
        .addActor(refTo(EXPORT_NODE_D))
        .addActor(0)
        .addActor(refTo(EXPORT_NODE_A))
        .addActor(0)
        .addActor(refTo(EXPORT_NODE_B))
        .addActor(0);
    writer.setURL("unreal", "host", "Fixture.unr", "portal", {}, 7777, 1);
    writer.setModel(0);
    addSpecs(writer);
    writer.setTrailerFloat(1.0F);

    ExportEntry entry;
    entry.objectName = NAME_LEVEL;
    entry.objectClass = -1; // import 0
    entry.serialData = writer.build();
    builder.addExport(entry);
    return builder.build();
}

/// Build the navigation graph for a fixture, failing the case rather than an
/// assertion if the package or the level does not read.
NavGraph graphOf(const std::vector<std::uint8_t>& bytes) {
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    // The Level export is the last one the fixture adds.
    const uta::upkg::ExportEntry& levelExport = package->exports().back();
    const auto level = uta::upkg::readLevel(*package, levelExport);
    REQUIRE(level.has_value());
    const auto graph = buildNavGraph(*package, *level, NOTHING_AVAILABLE);
    REQUIRE(graph.has_value());
    return *graph;
}

/// The node set every case shares, checked here so the cases about edges do not
/// each restate it.
void checkNodeSet(const NavGraph& graph) {
    REQUIRE(graph.nodes.size() == 4);
    CHECK(graph.nodes[0].exportIndex == EXPORT_NODE_A);
    CHECK(graph.nodes[1].exportIndex == EXPORT_NODE_B);
    CHECK(graph.nodes[2].exportIndex == EXPORT_NODE_C);
    CHECK(graph.nodes[3].exportIndex == EXPORT_NODE_D);
}

} // namespace

TEST_CASE("SS 4.3: a node is an actor whose class DESCENDS from NavigationPoint", "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter&) {}));

    // `Lamp` is an actor of a class that does not descend, so it is not a node,
    // and the class exports are not actors at all. No actor names the literal
    // `NavigationPoint` class, so a filter matching a name list finds nothing.
    checkNodeSet(graph);
    CHECK(graph.nodes[0].className == "PathNode");
    CHECK_FALSE(uta::unav::nodeOf(graph, EXPORT_LAMP).has_value());
    CHECK(graph.edges.empty());
    CHECK(graph.discardedEndpoints == 0u);
}

TEST_CASE("INV-1: an edge carries the EXPORT indices its reach spec named", "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        writer.addReachSpec(1234, refTo(EXPORT_NODE_B), refTo(EXPORT_NODE_C), 22, 44, 8, 1);
    }));

    checkNodeSet(graph);
    REQUIRE(graph.edges.size() == 1);
    const uta::unav::NavEdge& edge = graph.edges[0];

    // The claim is an IDENTITY one, and that is the point. `from < nodes.size()`
    // passes on exactly the defect this case exists to catch: SS 4.2 says the
    // wrong index space yields a well-formed edge to the WRONG ACTOR, because
    // the index it produces is usually a real node. So the assertion is on WHICH
    // actor, resolved back through the node table.
    CHECK(graph.nodes[edge.from].exportIndex == EXPORT_NODE_B);
    CHECK(graph.nodes[edge.to].exportIndex == EXPORT_NODE_C);

    // NodeB is export 2, node 1, actor 3; NodeC is export 6, node 2, actor 0. So
    // a builder confusing any pair of the three spaces fails above.
    CHECK(edge.from == 1u);
    CHECK(edge.to == 2u);

    // SS 4.1: the collision pair and `pruned` are carried through ungraded, and
    // `pruned` keeps the file's own byte. Nothing else pins their order.
    CHECK(edge.distance == 1234);
    CHECK(edge.collisionRadius == 22);
    CHECK(edge.collisionHeight == 44);
    CHECK(edge.reachFlags == 8);
    CHECK(edge.pruned == 1u);
}

TEST_CASE("INV-2: a level whose every endpoint resolves discards nothing", "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        writer.addReachSpec(10, refTo(EXPORT_NODE_A), refTo(EXPORT_NODE_B), 0, 0, 0, 0);
        writer.addReachSpec(20, refTo(EXPORT_NODE_B), refTo(EXPORT_NODE_C), 0, 0, 0, 0);
        writer.addReachSpec(30, refTo(EXPORT_NODE_C), refTo(EXPORT_NODE_D), 0, 0, 0, 0);
    }));

    // A count of zero is not the same as an absence, which is why the zero is
    // asserted rather than assumed -- SS 4.5.
    CHECK(graph.discardedEndpoints == 0u);
    CHECK(graph.edges.size() == 3);
}

TEST_CASE("INV-2: a NULL endpoint is discarded and counted, not aliased onto export 0",
          "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        writer.addReachSpec(10, 0, refTo(EXPORT_NODE_B), 0, 0, 0, 0);
    }));

    // This is the case that catches SS 4.2's guard, and it does so because
    // export 0 is a node. `ObjectReference::index()` returns 0 for a null
    // reference and the value is MEANINGLESS, so a builder without the `kind()`
    // guard produces an EDGE from NodeA here instead of a discard, and this
    // count reads zero.
    CHECK(graph.discardedEndpoints == 1u);
    CHECK(graph.edges.empty());
}

TEST_CASE("INV-2: an IMPORT endpoint is discarded and counted", "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        // -1 is import 0. Unguarded, `index()` reads it as export 0 -- NodeA,
        // an unrelated actor that is a real node.
        writer.addReachSpec(10, -1, refTo(EXPORT_NODE_B), 0, 0, 0, 0);
    }));

    CHECK(graph.discardedEndpoints == 1u);
    CHECK(graph.edges.empty());
}

TEST_CASE("INV-2: an endpoint naming an export that is not a node is discarded and counted",
          "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        writer.addReachSpec(10, refTo(EXPORT_LAMP), refTo(EXPORT_NODE_B), 0, 0, 0, 0);
    }));

    CHECK(graph.discardedEndpoints == 1u);
    CHECK(graph.edges.empty());
}

TEST_CASE("INV-2: an endpoint past the end of the export table is discarded and counted",
          "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        // SS 4.5: `Package::open` validates the three TABLES, and a reach spec's
        // endpoints live in an export's serialised DATA, which that validation
        // does not cover. So this reference opens fine and must be bounded by
        // the builder -- without which this case reads out of bounds instead of
        // discarding.
        writer.addReachSpec(10, 5000, refTo(EXPORT_NODE_B), 0, 0, 0, 0);
    }));

    CHECK(graph.discardedEndpoints == 1u);
    CHECK(graph.edges.empty());
}

TEST_CASE("INV-2: a spec with BOTH endpoints unresolvable counts two", "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        writer.addReachSpec(10, 0, refTo(EXPORT_LAMP), 0, 0, 0, 0);
    }));

    // The case that pins the unit: `discardedEndpoints` counts ENDPOINTS and not
    // specs, so a builder that stopped at the first bad endpoint, or counted per
    // spec, reads one here.
    CHECK(graph.discardedEndpoints == 2u);
    CHECK(graph.edges.empty());
}

TEST_CASE("SS 4.7: edges are grouped by source, and nodeOf is the only bridge", "[unav]") {
    const NavGraph graph = graphOf(levelWith([](LevelExportWriter& writer) {
        // Deliberately not in source order, so a builder that trusted file order
        // instead of grouping hands back the wrong runs.
        writer.addReachSpec(10, refTo(EXPORT_NODE_A), refTo(EXPORT_NODE_B), 0, 0, 0, 0);
        writer.addReachSpec(20, refTo(EXPORT_NODE_B), refTo(EXPORT_NODE_C), 0, 0, 0, 0);
        writer.addReachSpec(30, refTo(EXPORT_NODE_A), refTo(EXPORT_NODE_C), 0, 0, 0, 0);
    }));

    checkNodeSet(graph);

    const auto nodeA = uta::unav::nodeOf(graph, EXPORT_NODE_A);
    const auto nodeB = uta::unav::nodeOf(graph, EXPORT_NODE_B);
    const auto nodeC = uta::unav::nodeOf(graph, EXPORT_NODE_C);
    REQUIRE(nodeA.has_value());
    REQUIRE(nodeB.has_value());
    REQUIRE(nodeC.has_value());

    REQUIRE(uta::unav::edgesFrom(graph, *nodeA).size() == 2);
    CHECK(uta::unav::edgesFrom(graph, *nodeB).size() == 1);
    CHECK(uta::unav::edgesFrom(graph, *nodeC).empty());

    // Stable grouping: the file's own spec order survives inside a run, so the
    // two edges out of NodeA arrive in the order the level stated them.
    CHECK(uta::unav::edgesFrom(graph, *nodeA)[0].distance == 10);
    CHECK(uta::unav::edgesFrom(graph, *nodeA)[1].distance == 30);

    // An export that is not a node has no position, which is what stands between
    // a consumer and another actor's edges.
    CHECK_FALSE(uta::unav::nodeOf(graph, EXPORT_LAMP).has_value());
    // A position out of range is empty rather than undefined behaviour.
    CHECK(uta::unav::edgesFrom(graph, 999u).empty());
}
