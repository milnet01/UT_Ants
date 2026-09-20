// UTA-0136: ut-dump's --nav-graph carries each reach spec's flags and collision
// size, so a consumer can decide walkability from the JSON alone.
//
// tools/ut-dump/Cli.cpp is compiled into this binary, so every case drives the
// command line as a function over files written to a temporary install.
//
// The roadmap item's test is the first case: a walking-bot filter applied to
// the JSON reproduces ut-paths' Scene::edges for the same map. The filter here
// is written from the rows alone, as the consumer's would be, and INV-11's
// seven specs of docs/specs/UTA-0121-bot-path-seeds.md are what it sorts.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "BakeFixture.h"

#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"
#include "ut-dump/Cli.h"
#include "ut-paths/Seeds.h"
#include "ut-paths/Walkable.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::bake;

namespace {

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Run result;
    result.code = uta::dump::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

/// INV-11's packages, as PathSeedsTest's seedPackages builds them, by file
/// stem: Engine's navigation classes and MonsterHunt's MonsterEnd, with the
/// collision sizes the install's own classes carry. A second copy rather than
/// a shared one -- two callers, short of coding.md's Rule of Three.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> installPackages() {
    Packer engine;
    const std::int32_t navigation = engine.addClass(
        "NavigationPoint", 0,
        {floatProperty("CollisionRadius", 46), floatProperty("CollisionHeight", 50)});
    engine.addClass("PlayerStart", navigation,
                    {floatProperty("CollisionRadius", 18), floatProperty("CollisionHeight", 40)});
    engine.addClass("PathNode", navigation);
    Packer monsterHunt;
    monsterHunt.addClass("MonsterEnd", 0,
                         {floatProperty("CollisionRadius", 40), floatProperty("CollisionHeight", 40)});
    return {{"Core", tinyPackage("Object")}, {"Engine", engine.build()},
            {"MonsterHunt", monsterHunt.build()}};
}

/// INV-11's map: a PlayerStart, a MonsterEnd, and four PathNodes (actors 2 to
/// 5) joined by seven specs, each on its own ordered pair.
MapBuilder inv11Map() {
    MapBuilder map;
    map.addActorOfClass("Engine", "PlayerStart", {vectorProperty("Location", -200, 0, 0)})
        .addActorOfClass("MonsterHunt", "MonsterEnd", {vectorProperty("Location", -400, 0, 0)});
    for (int i = 0; i < 4; ++i)
        map.addActorOfClass("Engine", "PathNode", {vectorProperty("Location", 100.0F * i, 0, 0)});
    constexpr std::size_t A = 2, B = 3, C = 4, D = 5;
    map.addReachSpec(A, B, 17, 39, 1)      // walking
        .addReachSpec(B, C, 17, 39, 9)     // walking and jumping
        .addReachSpec(C, D, 17, 39, 32)    // special
        .addReachSpec(D, A, 17, 39, 85)    // walking, swimming, doors and player-only
        .addReachSpec(A, C, 17, 39, 2)     // flying
        .addReachSpec(B, D, 16, 39, 1)     // walking, narrower than the body
        .addReachSpec(C, A, 17, 38, 1);    // walking, lower than the body
    return map;
}

struct NodeRow {
    std::string name, className;
};

struct EdgeRow {
    std::size_t from = 0, to = 0;
    std::int32_t radius = 0, height = 0, flags = 0;
};

std::vector<NodeRow> nodeRows(const std::string& json) {
    // A delimiter, because `)"` inside a capture would end a plain raw string.
    static const std::regex row(R"re(\{"export": \d+, "name": "([^"]*)", "class": "([^"]*)"\})re");
    std::vector<NodeRow> rows;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), row); it != std::sregex_iterator(); ++it)
        rows.push_back({(*it)[1].str(), (*it)[2].str()});
    return rows;
}

std::vector<EdgeRow> edgeRows(const std::string& json) {
    static const std::regex row(
        R"re(\{"from": (\d+), "to": (\d+), "distance": -?\d+, "collisionRadius": (-?\d+), )re"
        R"re("collisionHeight": (-?\d+), "reachFlags": (-?\d+), "pruned": \d+\})re");
    std::vector<EdgeRow> rows;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), row); it != std::sregex_iterator(); ++it)
        rows.push_back({std::stoul((*it)[1].str()), std::stoul((*it)[2].str()), std::stoi((*it)[3].str()),
                        std::stoi((*it)[4].str()), std::stoi((*it)[5].str())});
    return rows;
}

/// A walking bot's rule, from the row alone: the body fits, and every flag is
/// one APawn::calcMoveFlags gives a walker -- all but R_FLY, 2.
bool walkerMayUse(const EdgeRow& edge) {
    constexpr std::int32_t WALKER = 1 | 4 | 8 | 16 | 32 | 64;
    return edge.radius >= uta::paths::RADIUS && edge.height >= uta::paths::HALF_HEIGHT
           && (edge.flags & WALKER) == edge.flags;
}

/// The map and its install on disk; the map's path.
fs::path writeMap(const TempDir& dir, const MapBuilder& map) {
    for (const auto& [stem, bytes] : installPackages())
        writeFile(dir.path() / "System" / (stem + ".u"), bytes);
    const fs::path path = dir.path() / "Maps" / "MH-Fixture.unr";
    writeFile(path, map.build());
    return path;
}

} // namespace

TEST_CASE("UTA-0136: a walking filter over --nav-graph reproduces the scene's edges", "[dump]") {
    const MapBuilder map = inv11Map();
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, map);

    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);

    const std::vector<NodeRow> nodes = nodeRows(result.out);
    const std::vector<EdgeRow> edges = edgeRows(result.out);
    // Every spec, the unusable ones too: the filter is the consumer's to apply.
    REQUIRE(edges.size() == 7);

    // The same map through ut-paths, over the same packages.
    MemoryPackages packages;
    for (const auto& [stem, bytes] : installPackages()) {
        std::string folded = stem;
        for (char& ch : folded) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        packages.add(folded, bytes);
    }
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());
    const auto scene = uta::paths::sceneOf(*package, MAP_NAME, packages.resolver());
    INFO((scene.has_value() ? std::string() : std::string(scene.error().message())));
    REQUIRE(scene.has_value());
    // Every navigation point is placed, so a node's position in the JSON is its
    // position in the scene's network.
    REQUIRE(scene->network.size() == nodes.size());

    std::set<std::pair<std::size_t, std::size_t>> filtered;
    for (const EdgeRow& edge : edges)
        if (walkerMayUse(edge)) filtered.emplace(edge.from, edge.to);
    const std::set<std::pair<std::size_t, std::size_t>> kept(scene->edges.begin(), scene->edges.end());
    CHECK(filtered.size() == 4);
    CHECK(filtered == kept);
}

TEST_CASE("UTA-0136: an edge names its nodes by actor name and class", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    REQUIRE(result.code == 0);

    const std::vector<NodeRow> nodes = nodeRows(result.out);
    const std::vector<EdgeRow> edges = edgeRows(result.out);
    // The flying spec, the only one carrying flag 2, runs PathNode2 to PathNode4.
    std::size_t flying = 0;
    for (const EdgeRow& edge : edges) {
        if (edge.flags != 2) continue;
        ++flying;
        REQUIRE(edge.from < nodes.size());
        REQUIRE(edge.to < nodes.size());
        CHECK(nodes[edge.from].name == "PathNode2");
        CHECK(nodes[edge.from].className == "PathNode");
        CHECK(nodes[edge.to].name == "PathNode4");
    }
    CHECK(flying == 1);
}

namespace {

/// UTA-0101's install. ThingFactory's default capacity is UnrealShare's own,
/// 1000000, and CreatureFactory overrides it to 1, as UnrealShare's source does.
/// UTA-0173: KrallFactory names its prototype in its class defaults, and
/// HealthVial is an item rather than a pawn.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> factsPackages() {
    Packer engine;
    engine.addClass("LevelInfo");
    Packer unrealShare;
    const std::int32_t thing = unrealShare.addClass("ThingFactory", 0, {intProperty("capacity", 1000000)});
    const std::int32_t creature = unrealShare.addClass("CreatureFactory", thing, {intProperty("capacity", 1)});
    const std::int32_t pawn = unrealShare.addClass("ScriptedPawn");
    const std::int32_t krall = unrealShare.addClass("Krall", pawn);
    const std::int32_t nali = unrealShare.addClass("Nali", pawn);
    unrealShare.addClass("NaliPriest", nali);
    unrealShare.addClass("Cow", pawn);
    unrealShare.addClass("HealthVial");
    unrealShare.addClass("KrallFactory", creature, {objectProperty("prototype", krall)});
    // A class that resolves, whose parent lives in a package the install lacks:
    // its family is cut short, so it cannot be sorted either.
    Packer orphan;
    orphan.addClass("Thing", orphan.importClass("Gone", "Base"));
    return {{"Core", tinyPackage("Object")}, {"Engine", engine.build()},
            {"UnrealShare", unrealShare.build()}, {"Orphan", orphan.build()}};
}

/// A map whose LevelInfo carries Windows-1252 text and UTF-8 text, whose
/// LevelSummary carries a title and a byte Windows-1252 leaves undefined, and
/// whose actors are factories making monsters and other things, placed pawns,
/// and actors of classes no package supplies.
Run runFacts(const TempDir& dir) {
    MapBuilder map;
    const std::int32_t krall = map.importClass("UnrealShare", "Krall");
    const auto making = [](std::int32_t prototype, std::vector<PropertySpec> properties) {
        properties.push_back(objectProperty("prototype", prototype));
        return properties;
    };
    // A class the map itself exports, as MH-2001v14's MyLevel.NukeRocket is.
    const std::int32_t rocket = map.addClass("NukeRocket");
    map.addActorOfClass("Engine", "LevelInfo",
                        {strProperty("Title", "Caf\xE9 \xB7 Test"), strProperty("Author", "J\xC3\xBCrgen")})
        .addActorOfClass("UnrealShare", "CreatureFactory", making(krall, {intProperty("capacity", 5)}))
        .addActorOfClass("UnrealShare", "CreatureFactory", making(krall, {}))   // the class's 1
        .addActorOfClass("UnrealShare", "ThingFactory", making(krall, {}))      // 1000000: no limit
        // Capacity 0 or below sends one monster: Spawning's Begin runs Timer
        // once, and StartBuilding re-arms only while capacity > 0.
        .addActorOfClass("UnrealShare", "CreatureFactory", making(krall, {intProperty("capacity", -1)}))
        .addActorOfClass("UnrealShare", "CreatureFactory", making(krall, {intProperty("capacity", 0)}))
        .addActorOfClass("UnrealShare", "KrallFactory", {intProperty("capacity", 2)}) // prototype by default
        // Not monsters: an item, twice; no prototype; a descendant of Nali; a
        // Cow; the map's own non-pawn class.
        .addActorOfClass("UnrealShare", "ThingFactory",
                         making(map.importClass("UnrealShare", "HealthVial"), {intProperty("capacity", 3)}))
        .addActorOfClass("UnrealShare", "ThingFactory", making(map.importClass("UnrealShare", "HealthVial"), {}))
        .addActorOfClass("UnrealShare", "ThingFactory", {intProperty("capacity", 4)})
        .addActorOfClass("UnrealShare", "CreatureFactory",
                         making(map.importClass("UnrealShare", "NaliPriest"), {intProperty("capacity", 7)}))
        .addActorOfClass("UnrealShare", "CreatureFactory",
                         making(map.importClass("UnrealShare", "Cow"), {intProperty("capacity", 9)}))
        .addActorOfClass("UnrealShare", "CreatureFactory", making(rocket, {intProperty("capacity", 8)}))
        // A prototype no package supplies cannot be sorted.
        .addActorOfClass("UnrealShare", "CreatureFactory",
                         making(map.importClass("Mystery", "Beast"), {intProperty("capacity", 6)}))
        .addActorOfClass("UnrealShare", "Krall")
        .addActorOfClass("UnrealShare", "Krall")
        .addActorOfClass("UnrealShare", "Nali")
        .addActorOfClass("Mystery", "Thing")
        .addActorOfClass("Orphan", "Thing")
        // The capacity-5 factory named in a second slot is still one factory
        // (UTA-0124), so the total stays 10.
        .repeatActorSlot(1);
    // A second LevelInfo, outside the level's actor list and ahead of the real
    // one in the export table, as maps the editor has left one behind carry.
    map.addObject("Engine", "LevelInfo", "LevelInfo99", {strProperty("Title", "Left behind")});
    map.addObject("Engine", "LevelSummary", "LevelSummary",
                  {strProperty("Title", "Summary\x99"), strProperty("Author", "X\x81")});
    for (const auto& [stem, bytes] : factsPackages())
        writeFile(dir.path() / "System" / (stem + ".u"), bytes);
    const fs::path path = dir.path() / "Maps" / "MH-Facts.unr";
    writeFile(path, map.build());
    return run({"--system", (dir.path() / "System").string(), path.string()});
}

} // namespace

TEST_CASE("UTA-0101: Title and Author are written as UTF-8 from both credit exports", "[dump]") {
    const TempDir dir;
    const Run result = runFacts(dir);
    INFO(result.out);
    REQUIRE(result.code == 0);
    // Windows-1252 decoded; UTF-8 kept; 0x99 is Windows-1252's trade mark; 0x81
    // is undefined there and falls back to Latin-1.
    CHECK(result.out.find("\"levelInfo\": {\"title\": \"Caf\xC3\xA9 \xC2\xB7 Test\", "
                          "\"author\": \"J\xC3\xBCrgen\"}")
          != std::string::npos);
    CHECK(result.out.find("\"levelSummary\": {\"title\": \"Summary\xE2\x84\xA2\", "
                          "\"author\": \"X\xC2\x81\"}")
          != std::string::npos);
}

TEST_CASE("UTA-0101: a map with no LevelSummary says null", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);
    CHECK(result.out.find("\"levelSummary\": null") != std::string::npos);
}

TEST_CASE("UTA-0101: monster capacity sums limited factories through their class family", "[dump]") {
    const TempDir dir;
    const Run result = runFacts(dir);
    INFO(result.out);
    REQUIRE(result.code == 0);
    // UTA-0173: only factories whose prototype is a monster count. 5, the
    // class's 1, one each for -1 and 0, and 2 are summed; 1000000 is no limit.
    // The placed Nali is not a monster; the Mystery prototype is unresolved.
    CHECK(result.out.find("\"monsters\": {\"factories\": 6, \"capacity\": 10, \"unlimitedFactories\": 1, "
                          "\"unknownCapacityFactories\": 0, \"placedPawns\": 2, \"unresolvedActors\": 3}")
          != std::string::npos);
}

namespace {

/// `json` with every whitespace byte outside a string removed, so two layouts of
/// one document compare equal. A string is kept exactly as written.
std::string withoutLayout(std::string_view json) {
    std::string kept;
    bool inString = false, escaped = false;
    for (const char ch : json) {
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') inString = false;
        } else if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            continue;
        } else if (ch == '"') {
            inString = true;
        }
        kept += ch;
    }
    return kept;
}

/// Whether `line` is one whole JSON object and nothing else: no raw control byte
/// in a string, every bracket closed by its own kind, and the object closing at
/// the last byte.
bool isOneObject(std::string_view line) {
    if (line.empty() || line.front() != '{') return false;
    std::string open;
    bool inString = false, escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (inString) {
            if (static_cast<unsigned char>(ch) < 0x20) return false;
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') inString = false;
        } else if (ch == '"') {
            inString = true;
        } else if (ch == '{' || ch == '[') {
            open += ch;
        } else if (ch == '}' || ch == ']') {
            if (open.empty() || open.back() != (ch == '}' ? '{' : '[')) return false;
            open.pop_back();
            if (open.empty()) return i + 1 == line.size();
        }
    }
    return false;
}

} // namespace

TEST_CASE("UTA-0145: --ndjson writes a header line then each package on its own line", "[dump]") {
    const TempDir dir;
    writeMap(dir, inv11Map());
    // The two refusals too, which close their objects on their own paths.
    writeFile(dir.path() / "Maps" / "Empty.unr", std::vector<std::uint8_t>{});
    writeFile(dir.path() / "Maps" / "Garbage.unr", std::vector<std::uint8_t>{'n', 'o', 't'});
    const std::vector<std::string> args{"--system", (dir.path() / "System").string(), "--nav-graph",
                                        (dir.path() / "Maps").string()};

    std::vector<std::string> ndjsonArgs = args;
    ndjsonArgs.insert(ndjsonArgs.begin(), "--ndjson");
    const Run ndjson = run(ndjsonArgs);
    const Run document = run(args);
    INFO(ndjson.out);
    REQUIRE(ndjson.code == 0);
    REQUIRE(document.code == 0);

    REQUIRE(!ndjson.out.empty());
    CHECK(ndjson.out.back() == '\n');
    std::vector<std::string> lines;
    std::istringstream stream(ndjson.out);
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == R"({"schema":1})");
    for (const std::string& line : lines) CHECK(isOneObject(line));

    // The lines carry the document's values in the document's order.
    std::string rebuilt = R"({"schema":1,"packages":[)";
    for (std::size_t i = 1; i < lines.size(); ++i) rebuilt += (i == 1 ? "" : ",") + withoutLayout(lines[i]);
    rebuilt += "]}";
    CHECK(withoutLayout(document.out) == rebuilt);
    CHECK(lines[3].find(R"("nodeList":)") != std::string::npos);
}

TEST_CASE("UTA-0136: without --nav-graph the nav object carries counts only", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);
    CHECK(result.out.find("\"nav\": {\"nodes\": ") != std::string::npos);
    CHECK(result.out.find("nodeList") == std::string::npos);
    CHECK(result.out.find("edgeList") == std::string::npos);
}

// -- UTA-0201: surfaces by texture and flags ---------------------------------
//
// Built because UTA-0188 could not ask whether AS-Frigate's water surface was
// in the map at all. The two cases below are the two answers that item needed:
// a surface is present under a texture and flags, and a surface is present
// that nothing draws.

namespace {

struct SurfaceRow {
    std::string texture;
    std::uint32_t polyFlags = 0;
    long long surfaces = 0;
    long long drawnNodes = 0;
};

std::vector<SurfaceRow> surfaceRows(const std::string& json) {
    static const std::regex row(
        R"re(\{"texture": "([^"]*)", "polyFlags": (\d+), "surfaces": (\d+), "drawnNodes": (\d+)\})re");
    std::vector<SurfaceRow> rows;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), row); it != std::sregex_iterator(); ++it)
        rows.push_back({(*it)[1].str(), static_cast<std::uint32_t>(std::stoul((*it)[2].str())),
                        std::stoll((*it)[3].str()), std::stoll((*it)[4].str())});
    return rows;
}

const SurfaceRow* rowFor(const std::vector<SurfaceRow>& rows, std::string_view texture,
                         std::uint32_t polyFlags) {
    for (const SurfaceRow& row : rows)
        if (row.texture == texture && row.polyFlags == polyFlags) return &row;
    return nullptr;
}

} // namespace

TEST_CASE("UTA-0201: one texture under two flag sets is two groups", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // AS-Frigate's own case: one water texture worn by surfaces flagged
    // differently, which is what made UTA-0188 hard to ask about.
    const std::int32_t water = map.importTexture("RainFX", "", "Swater4a", "Texture");
    const std::int32_t wood = map.importTexture("GenEarth", "", "Planks", "Texture");
    constexpr std::uint32_t SEA = 0x04002108;  // portal, two-sided, not solid
    constexpr std::uint32_t POOL = 0x0000010c; // translucent, two-sided, not solid
    map.addSurface(water, SEA).addSurface(water, POOL).addSurface(water, POOL).addSurface(wood, 0);

    const fs::path mapPath = writeMap(dir, map);
    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);

    const std::vector<SurfaceRow> rows = surfaceRows(result.out);
    CHECK(result.out.find("\"surfaces\": {\"total\": 4,") != std::string::npos);

    // The same texture under two flag sets does not collapse into one row.
    const SurfaceRow* const sea = rowFor(rows, "Swater4a", SEA);
    const SurfaceRow* const pool = rowFor(rows, "Swater4a", POOL);
    REQUIRE(sea != nullptr);
    REQUIRE(pool != nullptr);
    CHECK(sea->surfaces == 1);
    CHECK(pool->surfaces == 2); // two surfaces, one row
    CHECK(rowFor(rows, "Planks", 0) != nullptr);

    // Every surface this map has is drawn by something.
    CHECK(sea->drawnNodes >= 1);
    CHECK(pool->drawnNodes >= 2);
}

TEST_CASE("UTA-0201: a surface nothing draws is reported with no drawn nodes", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    const std::int32_t water = map.importTexture("RainFX", "", "Swater4a", "Texture");
    // The drawn one first, so it rather than the unseen one collects the
    // fixture's floor node, which names surface 0.
    map.addSurface(water, 0, /*drawn=*/true).addSurface(water, 0x40, /*drawn=*/false);

    const fs::path mapPath = writeMap(dir, map);
    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);

    const std::vector<SurfaceRow> rows = surfaceRows(result.out);
    const SurfaceRow* const drawn = rowFor(rows, "Swater4a", 0);
    const SurfaceRow* const unseen = rowFor(rows, "Swater4a", 0x40);
    REQUIRE(drawn != nullptr);
    REQUIRE(unseen != nullptr);

    // Both are IN the map -- that is the point. Only one reaches a screen, and
    // telling those apart is the whole reason this field exists.
    CHECK(unseen->surfaces == 1);
    CHECK(unseen->drawnNodes == 0);
    CHECK(drawn->surfaces == 1);
    CHECK(drawn->drawnNodes >= 1);
}
