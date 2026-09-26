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

#include "common/Json.h"
#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"
#include "ut-dump/Cli.h"
#include "ut-paths/Seeds.h"
#include "ut-paths/Walkable.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
    static const std::regex row(R"re(\{"export": \d+, "name": "([^"]*)", "class": "([^"]*)", "location": (?:null|\[[^\]]*\]), "paths")re");
    std::vector<NodeRow> rows;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), row); it != std::sregex_iterator(); ++it)
        rows.push_back({(*it)[1].str(), (*it)[2].str()});
    return rows;
}

std::vector<EdgeRow> edgeRows(const std::string& json) {
    static const std::regex row(
        R"re(\{"from": (\d+), "to": (\d+), "distance": -?\d+, "collisionRadius": (-?\d+), )re"
        R"re("collisionHeight": (-?\d+), "reachFlags": (-?\d+), "pruned": \d+, "spec": \d+\})re");
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

TEST_CASE("UTA-0213: --surface-list places each surface and names its built flags and brush", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    map.addActorOfClass("Engine", "Brush"); // Brush0
    const std::int32_t water = map.importTexture("RainFX", "", "Swater4a", "Texture");
    // One surface cut from a brush's third polygon, one no brush owns and no
    // node draws: the two values GAME-0008's fake-wall check reads.
    map.addSurface(water, 0).ownSurface(0, 3).addSurface(water, 0x40, /*drawn=*/false);

    const fs::path mapPath = writeMap(dir, map);
    const std::string system = (dir.path() / "System").string();
    const Run result = run({"--system", system, "--surface-list", mapPath.string()});
    REQUIRE(result.code == 0);

    // Index order, and the fixture's own points: surface i's square sits at
    // height 8i, facing up.
    CHECK(result.out.find(R"({"index": 0, "texture": "Swater4a", "polyFlags": 0, "brush": "Brush0", )"
                          R"("brushPoly": 3, "base": [0, 0, 0], "normal": [0, 0, 1], "drawnNodes": )")
          != std::string::npos);
    CHECK(result.out.find(R"({"index": 1, "texture": "Swater4a", "polyFlags": 64, "brush": null, )"
                          R"("brushPoly": 0, "base": [0, 0, 8], "normal": [0, 0, 1], "drawnNodes": 0})")
          != std::string::npos);

    // The drawn surface's count is its group's: it is that group's only member.
    static const std::regex firstRow(R"re("index": 0, [^}]*"drawnNodes": (\d+)\})re");
    std::smatch counted;
    REQUIRE(std::regex_search(result.out, counted, firstRow));
    const std::vector<SurfaceRow> groups = surfaceRows(result.out);
    const SurfaceRow* const group = rowFor(groups, "Swater4a", 0);
    REQUIRE(group != nullptr);
    CHECK(group->drawnNodes >= 1);
    CHECK(std::stoll(counted[1].str()) == group->drawnNodes);

    // Without the flag the groups stand alone: a map's thousands of rows are
    // opt-in.
    const Run plain = run({"--system", system, mapPath.string()});
    REQUIRE(plain.code == 0);
    CHECK(plain.out.find("\"list\"") == std::string::npos);
}

// ------------------------------------------------------------------ UTA-0172
// Per-actor event wiring. docs/specs/UTA-0172-actor-event-wiring.md; the
// invariant numbers below are that spec's.
//
// The consumer is UT_MonsterHunt's exit survey, and three of these cases exist
// because they told us what their rule turns on: the OutEvents index, the case
// difference across the join, and the difference between "switched off" and
// "the class has no such property".

namespace {

/// The `actors` array's text, or empty when the key is absent. Matched by
/// bracket depth: each element carries a nested `classChain` array, so the
/// first `]` closes that rather than this.
std::string actorsArray(const std::string& out) {
    const std::size_t at = out.find("\"actors\": [");
    if (at == std::string::npos) return {};
    const std::size_t open = out.find('[', at);
    int depth = 0;
    for (std::size_t i = open; i < out.size(); ++i) {
        if (out[i] == '[') ++depth;
        else if (out[i] == ']' && --depth == 0) return out.substr(at, i - at + 1);
    }
    return {};
}

/// One actor's object within `actors`, found by its `"name": "<name>"`.
std::string actorNamed(const std::string& actors, std::string_view name) {
    const std::string key = "\"name\": \"" + std::string{name} + "\"";
    const std::size_t at = actors.find(key);
    if (at == std::string::npos) return {};
    const std::size_t open = actors.rfind('{', at);
    const std::size_t close = actors.find('}', actors.find("\"initialState\"", at));
    if (open == std::string::npos || close == std::string::npos) return {};
    return actors.substr(open, close - open + 1);
}

} // namespace

TEST_CASE("UTA-0172 INV-1: without --wiring-graph the wiring object carries counts only", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    map.addActorOfClass("MonsterHunt", "MonsterEnd");
    const fs::path mapPath = writeMap(dir, map);

    const Run plain = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(plain.code == 0);
    // The ARRAY form specifically: `level` already carries an "actors" COUNT,
    // so a bare "actors" search passes for the wrong reason.
    CHECK(plain.out.find("\"actors\": [") == std::string::npos);
    CHECK(plain.out.find("\"level\": {\"actors\": ") != std::string::npos);
    // wiring's own count, which follows `dangling`'s array. `level` carries a
    // chainsUnresolved of its own without any flag (UTA-0012 § 4.8).
    CHECK(plain.out.find("], \"chainsUnresolved\"") == std::string::npos);
    // The keys that were there before this item are untouched.
    CHECK(plain.out.find("\"wiring\": {\"nodes\": ") != std::string::npos);
    CHECK(plain.out.find("\"edges\": ") != std::string::npos);
    CHECK(plain.out.find("\"dangling\": [") != std::string::npos);

    const Run withFlag =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    REQUIRE(withFlag.code == 0);
    CHECK(withFlag.out.find("\"actors\": [") != std::string::npos);
}

TEST_CASE("UTA-0172 INV-2: a property the actor never stored comes from its class defaults", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // The shape every exit in the four never-maps has: Tag is the class
    // default and is stored on no actor. A reader of stored properties alone
    // emits "" here and the consumer's whole rule collapses.
    const std::int32_t switcher = map.addClass("Switcher", 0, {nameProperty("Tag", "inheritedtag")});
    map.addActor("Switcher0", switcher);
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    const std::string actor = actorNamed(actorsArray(result.out), "Switcher0");
    INFO(actor);
    CHECK(actor.find("\"tag\": \"inheritedtag\"") != std::string::npos);
}

TEST_CASE("UTA-0172 INV-3: OutEvents keeps its index and an index above zero survives", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // MH-3072-FloorWaysSBMod's shape: a Dispatcher naming the exit through
    // OutEvents(1), with nothing at index 0. A reader that takes only the
    // first element, or flattens the array, reads that map as never when it
    // is live.
    map.addActorOfClass("Engine", "Dispatcher", {nameAtProperty("OutEvents", 1, "MissionDone")});
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    const std::string actors = actorsArray(result.out);
    INFO(actors);
    CHECK(actors.find("\"OutEvents\": {\"1\": \"MissionDone\"}") != std::string::npos);
    CHECK(actors.find("\"0\": \"MissionDone\"") == std::string::npos);
}

TEST_CASE("UTA-0172 INV-4: names are emitted in the case stored", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // The two ends of FloorWaysSBMod's join differ in case. Folding either on
    // emit would make them compare equal here and destroy a distinction a
    // consumer may need.
    const std::int32_t exit = map.addClass("Exit", 0, {nameProperty("Tag", "missiondone")});
    map.addActor("Exit0", exit);
    map.addActorOfClass("Engine", "Trigger", {nameProperty("Event", "MissionDone")});
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    const std::string actors = actorsArray(result.out);
    INFO(actors);
    CHECK(actors.find("\"tag\": \"missiondone\"") != std::string::npos);
    CHECK(actors.find("\"Event\": \"MissionDone\"") != std::string::npos);
}

TEST_CASE("UTA-0172 INV-5: a chain the resolver cannot complete says so", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // A class from a package the install does not carry -- an ordinary mod
    // nobody installed. The walk is honest but incomplete, and a consumer
    // reading a short chain as "not an exit" gets a false negative.
    const std::int32_t ghost = map.importClass("NoSuchPackage", "Ghost");
    map.addActor("Ghost0", ghost);
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    const std::string actor = actorNamed(actorsArray(result.out), "Ghost0");
    INFO(actor);
    CHECK(actor.find("\"chainEnd\": \"root\"") == std::string::npos);
    CHECK(result.out.find("\"chainsUnresolved\": 0") == std::string::npos);
}

TEST_CASE("UTA-0172 INV-6: a Latin-1 byte in a tag is emitted as valid UTF-8", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // UTA-0202's rule, reached through this key. 0xf1 is the n-tilde the
    // install's own Telarana texture carries.
    const std::int32_t spanish = map.addClass("Spanish", 0, {nameProperty("Tag", "Telara\xf1" "a")});
    map.addActor("Spanish0", spanish);
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    CHECK(uta::tools::isUtf8(result.out));
    CHECK(result.out.find("Telara\xc3\xb1" "a") != std::string::npos);
}

TEST_CASE("UTA-0172 INV-8: an actor with no tag and no events is still emitted", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // The consumer's never test divides by the exit count, so any filter that
    // can drop an actor can shrink that denominator and manufacture a false
    // never. There is no filter, and this is what says so.
    const std::int32_t bare = map.addClass("Bare");
    map.addActor("Bare0", bare);
    map.addActor("Bare1", bare);
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    const std::string actors = actorsArray(result.out);
    INFO(actors);
    CHECK(actors.find("\"name\": \"Bare0\"") != std::string::npos);
    CHECK(actors.find("\"name\": \"Bare1\"") != std::string::npos);
}

TEST_CASE("UTA-0172 INV-9: bInitiallyActive is null where the class family has no such property", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    // "No such property" and "switched off" must not be the same value:
    // off-ness is half the consumer's never test, and most actors in a real
    // map have no such property at all.
    const std::int32_t bare = map.addClass("Bare");
    map.addActor("Bare0", bare);
    const std::int32_t gated = map.addClass("Gated", 0, {boolProperty("bInitiallyActive", false)});
    map.addActor("Gated0", gated);
    const fs::path mapPath = writeMap(dir, map);

    const Run result =
        run({"--system", (dir.path() / "System").string(), "--wiring-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    const std::string actors = actorsArray(result.out);
    CHECK(actorNamed(actors, "Bare0").find("\"bInitiallyActive\": null") != std::string::npos);
    CHECK(actorNamed(actors, "Gated0").find("\"bInitiallyActive\": false") != std::string::npos);
}

// ------------------------------------------------------------------ UTA-0012
// The output-shape contract. docs/specs/UTA-0012-ut-dump-output-shape.md; the
// invariant numbers below are that spec's. INV-1 is the UTA-0145 case above,
// and INV-7's row shape is also held by the UTA-0136 cases.

namespace {

/// The text of the object that starts at `open` (a `{`), by brace depth,
/// skipping strings.
std::string objectAt(const std::string& json, std::size_t open) {
    int depth = 0;
    bool inString = false;
    for (std::size_t i = open; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (ch == '\\') ++i;
            else if (ch == '"') inString = false;
        } else if (ch == '"') {
            inString = true;
        } else if (ch == '{' || ch == '[') {
            ++depth;
        } else if ((ch == '}' || ch == ']') && --depth == 0) {
            return json.substr(open, i - open + 1);
        }
    }
    return {};
}

/// The keys of the object `object`, at its own depth only, in order. A key is
/// a string at depth 1 followed by a colon. Local to this test, so INV-4 needs
/// no JSON library (spec § 7).
std::vector<std::string> keysOf(const std::string& object) {
    std::vector<std::string> keys;
    int depth = 0;
    for (std::size_t i = 0; i < object.size(); ++i) {
        const char ch = object[i];
        if (ch == '"') {
            std::size_t end = i + 1;
            while (end < object.size() && object[end] != '"') end += object[end] == '\\' ? 2 : 1;
            std::size_t after = end + 1;
            while (after < object.size() && object[after] == ' ') ++after;
            if (depth == 1 && after < object.size() && object[after] == ':')
                keys.push_back(object.substr(i + 1, end - i - 1));
            i = end;
        } else if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
        }
    }
    return keys;
}

/// The object held by `"key": {` inside `json`, or empty when absent.
std::string member(const std::string& json, std::string_view key) {
    const std::size_t at = json.find("\"" + std::string{key} + "\": {");
    if (at == std::string::npos) return {};
    return objectAt(json, json.find('{', at));
}

/// Every package object of a document-form run, in order.
std::vector<std::string> packagesOf(const std::string& out) {
    std::vector<std::string> packages;
    const std::size_t list = out.find("\"packages\": [");
    if (list == std::string::npos) return packages;
    for (std::size_t at = out.find('{', list); at != std::string::npos; at = out.find('{', at)) {
        const std::string object = objectAt(out, at);
        if (object.empty()) break;
        packages.push_back(object);
        at += object.size();
    }
    return packages;
}

/// The integer after `"key": ` in `json`, or -1 when absent.
long long integerOf(const std::string& json, std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\": ";
    const std::size_t at = json.find(needle);
    if (at == std::string::npos) return -1;
    return std::stoll(json.substr(at + needle.size()));
}

using Keys = std::vector<std::string>;

} // namespace

TEST_CASE("UTA-0012 INV-2: packages come in path order and each names its file", "[dump]") {
    const TempDir dir;
    writeMap(dir, inv11Map());
    const fs::path a = dir.path() / "Maps" / "A.unr";
    const fs::path b = dir.path() / "Maps" / "B.unr";
    writeFile(a, inv11Map().build());
    writeFile(b, inv11Map().build());

    // Named B first: argument order and path order disagree.
    const Run result = run({"--system", (dir.path() / "System").string(), b.string(), a.string()});
    REQUIRE(result.code == 0);
    const std::vector<std::string> packages = packagesOf(result.out);
    REQUIRE(packages.size() == 2);
    // Spelled as the tool writes it: JSON doubles every Windows backslash,
    // so the raw path matches on Linux only.
    const auto fileKey = [](const fs::path& path) {
        std::ostringstream key;
        key << "\"file\": ";
        uta::tools::writeJsonString(key, path.string());
        return key.str();
    };
    CHECK(packages[0].find(fileKey(a)) != std::string::npos);
    CHECK(packages[1].find(fileKey(b)) != std::string::npos);
}

TEST_CASE("UTA-0012 INV-3: a package that does not open is its own element and the run goes on", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const fs::path empty = dir.path() / "Maps" / "Empty.unr";
    const fs::path garbage = dir.path() / "Maps" / "Garbage.unr";
    writeFile(empty, std::vector<std::uint8_t>{});
    writeFile(garbage, std::vector<std::uint8_t>{'n', 'o', 't'});

    const Run result = run({"--system", (dir.path() / "System").string(), empty.string(),
                            garbage.string(), mapPath.string()});
    REQUIRE(result.code == 0);
    const std::vector<std::string> packages = packagesOf(result.out);
    REQUIRE(packages.size() == 3);
    // Path order: Empty, Garbage, MH-Fixture.
    CHECK(keysOf(packages[0]) == Keys{"file", "ok", "error"});
    CHECK(packages[0].find("\"error\": \"unreadable or empty\"") != std::string::npos);
    CHECK(keysOf(packages[1]) == Keys{"file", "ok", "error"});
    CHECK(packages[1].find("\"error\": \"package did not open\"") != std::string::npos);
    CHECK(packages[2].find("\"ok\": true") != std::string::npos);
}

TEST_CASE("UTA-0012 INV-4: the key sets are exactly the contract's", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const fs::path engine = dir.path() / "System" / "Engine.u";
    const std::string system = (dir.path() / "System").string();

    // A package with no Level.
    const Run plainSystem = run({"--system", system, engine.string()});
    REQUIRE(plainSystem.code == 0);
    const std::vector<std::string> nonMap = packagesOf(plainSystem.out);
    REQUIRE(nonMap.size() == 1);
    CHECK(keysOf(nonMap[0])
          == Keys{"file", "ok", "bytes", "exports", "imports", "importedPackages", "classCounts", "level"});

    const Keys mapKeys{"file",    "ok",        "bytes",        "exports",  "imports",
                       "importedPackages", "classCounts", "level", "surfaces", "levelInfo",
                       "levelSummary", "monsters", "nav", "wiring", "exits"};
    const Run plain = run({"--system", system, mapPath.string()});
    REQUIRE(plain.code == 0);
    const std::string map = packagesOf(plain.out).at(0);
    CHECK(keysOf(map) == mapKeys);
    CHECK(keysOf(member(map, "level")) == Keys{"actors", "rawSlots", "reachSpecs", "chainsUnresolved"});
    CHECK(keysOf(member(map, "surfaces")) == Keys{"total", "byTextureAndFlags"});
    CHECK(keysOf(member(map, "monsters"))
          == Keys{"factories", "capacity", "unlimitedFactories", "unknownCapacityFactories", "placedPawns",
                  "unresolvedActors"});
    CHECK(keysOf(member(map, "nav")) == Keys{"nodes", "edges", "discardedEndpoints", "nodesWithNoExit"});
    CHECK(keysOf(member(map, "wiring")) == Keys{"nodes", "edges", "dangling"});

    const Run full =
        run({"--system", system, "--nav-graph", "--wiring-graph", "--surface-list", mapPath.string()});
    REQUIRE(full.code == 0);
    const std::string fullMap = packagesOf(full.out).at(0);
    CHECK(keysOf(fullMap) == mapKeys);
    CHECK(keysOf(member(fullMap, "surfaces")) == Keys{"total", "byTextureAndFlags", "list"});
    CHECK(keysOf(member(fullMap, "nav"))
          == Keys{"nodes", "edges", "discardedEndpoints", "nodesWithNoExit", "nodeList", "edgeList"});
    CHECK(keysOf(member(fullMap, "wiring")) == Keys{"nodes", "edges", "dangling", "chainsUnresolved", "actors"});
}

TEST_CASE("UTA-0012 INV-5: classCounts counts every export and sums to exports", "[dump]") {
    const TempDir dir;
    MapBuilder map = inv11Map();
    // A non-actor export beside the actors: a Level-only count misses it.
    map.addObject("Engine", "LevelSummary", "LevelSummary", {});
    const fs::path mapPath = writeMap(dir, map);

    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);
    const std::string package = packagesOf(result.out).at(0);
    const std::string counts = member(package, "classCounts");
    INFO(counts);
    long long sum = 0;
    for (const std::string& key : keysOf(counts)) sum += integerOf(counts, key);
    CHECK(sum == integerOf(package, "exports"));
    const Keys keys = keysOf(counts);
    CHECK(std::find(keys.begin(), keys.end(), "LevelSummary") != keys.end());
}

TEST_CASE("UTA-0012 INV-6: importedPackages is the outermost names once each and ascending", "[dump]") {
    const TempDir dir;
    MapBuilder map = inv11Map();
    // A texture two outers deep: one link gives the GROUP, not the package.
    map.addSurface(map.importTexture("Pkg", "Group", "Tex"));
    const fs::path mapPath = writeMap(dir, map);

    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);
    const std::string package = packagesOf(result.out).at(0);
    const std::size_t at = package.find("\"importedPackages\": [");
    REQUIRE(at != std::string::npos);
    const std::string list = package.substr(at, package.find(']', at) - at);
    INFO(list);
    std::vector<std::string> names;
    static const std::regex name(R"re("([^"]*)")re");
    for (auto it = std::sregex_iterator(list.begin() + 20, list.end(), name); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    CHECK(std::count(names.begin(), names.end(), "Engine") == 1);
    CHECK(std::count(names.begin(), names.end(), "Pkg") == 1);
    CHECK(std::count(names.begin(), names.end(), "Group") == 0);
    CHECK(std::is_sorted(names.begin(), names.end()));
    CHECK(std::set<std::string>(names.begin(), names.end()).size() == names.size());
}

TEST_CASE("UTA-0012 INV-7: nodeList and edgeList rows carry exactly the contract's keys", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    REQUIRE(result.code == 0);
    const std::string nav = member(packagesOf(result.out).at(0), "nav");
    const std::size_t node = nav.find('{', nav.find("\"nodeList\": ["));
    const std::size_t edge = nav.find('{', nav.find("\"edgeList\": ["));
    REQUIRE(node != std::string::npos);
    REQUIRE(edge != std::string::npos);
    CHECK(keysOf(objectAt(nav, node))
          == Keys{"export", "name", "class", "location", "paths", "upstreamPaths", "prunedPaths"});
    CHECK(keysOf(objectAt(nav, edge))
          == Keys{"from", "to", "distance", "collisionRadius", "collisionHeight", "reachFlags", "pruned", "spec"});
}

TEST_CASE("UTA-0012 INV-8: level.chainsUnresolved counts each unresolved actor once without a flag", "[dump]") {
    const TempDir dir;
    const std::string system = (dir.path() / "System").string();

    // The same map with and without two more actors: one whose class lives in
    // a package the install lacks, named in two Level slots so a count over
    // slots adds 2 rather than 1; and one with no class at all, which has no
    // chain to cut short and adds nothing, as wiring.actors reports it.
    const fs::path base = writeMap(dir, inv11Map());
    MapBuilder ghostMap = inv11Map();
    ghostMap.addActor("Ghost0", ghostMap.importClass("NoSuchPackage", "Ghost"));
    ghostMap.addActor("Nothing0", 0);
    ghostMap.repeatActorSlot(6);
    const fs::path ghost = dir.path() / "Maps" / "MH-Ghost.unr";
    writeFile(ghost, ghostMap.build());

    const Run plain = run({"--system", system, base.string(), ghost.string()});
    REQUIRE(plain.code == 0);
    const std::vector<std::string> packages = packagesOf(plain.out);
    REQUIRE(packages.size() == 2);
    // Path order: MH-Fixture, MH-Ghost.
    const long long without = integerOf(member(packages[0], "level"), "chainsUnresolved");
    const long long with = integerOf(member(packages[1], "level"), "chainsUnresolved");
    REQUIRE(without >= 0);
    CHECK(with == without + 1);

    const Run flagged = run({"--system", system, "--wiring-graph", ghost.string()});
    REQUIRE(flagged.code == 0);
    const std::string package = packagesOf(flagged.out).at(0);
    CHECK(integerOf(member(package, "level"), "chainsUnresolved") == with);
    CHECK(integerOf(member(package, "wiring"), "chainsUnresolved") == with);
}

TEST_CASE("UTA-0206: --install finds a class that lives in a texture package", "[dump]") {
    // MH-Lego-VS-Mario-2D&3D's shape: its sky class lives in Textures/, which
    // --system never reads.
    const TempDir dir;
    MapBuilder map;
    map.addActorOfClass("Mario-snes", "MultiSkyZoneInfo");
    const fs::path mapPath = writeMap(dir, map);
    writeFile(dir.path() / "Textures" / "Mario-snes.utx", classPackage("MultiSkyZoneInfo"));

    const Run system = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(system.code == 0);
    CHECK(system.out.find("\"chainsUnresolved\": 1") != std::string::npos);

    const Run install = run({"--install", dir.path().string(), mapPath.string()});
    INFO(install.err);
    REQUIRE(install.code == 0);
    CHECK(install.out.find("\"chainsUnresolved\": 0") != std::string::npos);
}

TEST_CASE("UTA-0206: a class the map names under UnrealI is found in UnrealShare", "[dump]") {
    // MH-SPNaliRescue's shape, under the built-in remap in upkg.
    const TempDir dir;
    MapBuilder map;
    map.addActorOfClass("UnrealI", "Barrel");
    const fs::path mapPath = writeMap(dir, map);
    writeFile(dir.path() / "System" / "UnrealI.u", classPackage("Health"));
    writeFile(dir.path() / "System" / "UnrealShare.u", classPackage("Barrel"));

    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    CHECK(result.out.find("\"chainsUnresolved\": 0") != std::string::npos);
}

TEST_CASE("UTA-0206: --install and --system together are refused", "[dump]") {
    const TempDir dir;
    const Run result = run({"--install", dir.path().string(), "--system", dir.path().string(), "x.unr"});
    CHECK(result.code == 2);
    CHECK(result.out.empty());
}

TEST_CASE("UTA-0198: each edge names the reach spec it came from", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);

    // inv11Map adds its specs in file order 0 to 6, and reach flags tell them
    // apart. The graph groups edges by `from`, so the flying spec (index 4,
    // flags 2) sits second in edgeList, under its source node.
    static const std::regex row(R"re("reachFlags": (-?\d+), "pruned": \d+, "spec": (\d+)\})re");
    std::vector<std::pair<int, int>> flagsAndSpec;
    for (auto it = std::sregex_iterator(result.out.begin(), result.out.end(), row); it != std::sregex_iterator(); ++it)
        flagsAndSpec.emplace_back(std::stoi((*it)[1].str()), std::stoi((*it)[2].str()));
    REQUIRE(flagsAndSpec.size() == 7);
    const std::vector<std::pair<int, int>> fileOrder{{1, 0}, {9, 1}, {32, 2}, {85, 3}, {2, 4}};
    for (const auto& expected : fileOrder)
        CHECK(std::ranges::find(flagsAndSpec, expected) != flagsAndSpec.end());
    CHECK(flagsAndSpec[1] == std::pair{2, 4}); // position 1, spec 4
}

TEST_CASE("UTA-0198: a node lists its own Paths, upstreamPaths and PrunedPaths", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    map.addActorOfClass("Engine", "PathNode",
                        {vectorProperty("Location", 100, 0, 50), intProperty("Paths", 0),
                         intAtProperty("Paths", 1, 4), intProperty("upstreamPaths", 3),
                         intProperty("PrunedPaths", 6)})
        .addActorOfClass("Engine", "PathNode")
        .addReachSpec(0, 1, 17, 39, 1);
    const fs::path mapPath = writeMap(dir, map);
    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    INFO(result.out);
    REQUIRE(result.code == 0);
    CHECK(result.out.find("\"name\": \"PathNode0\", \"class\": \"PathNode\", \"location\": [100, 0, 50], "
                          "\"paths\": [0, 4], \"upstreamPaths\": [3], \"prunedPaths\": [6]}")
          != std::string::npos);
    CHECK(result.out.find("\"name\": \"PathNode1\", \"class\": \"PathNode\", \"location\": null, "
                          "\"paths\": [], \"upstreamPaths\": [], \"prunedPaths\": []}")
          != std::string::npos);
}

TEST_CASE("UTA-0189: exits lists each MonsterEnd-family actor with its location and tag", "[dump]") {
    const TempDir dir;
    MapBuilder map;
    map.addActorOfClass("MonsterHunt", "MonsterEnd",
                        {vectorProperty("Location", -400, 0, 0), nameProperty("Tag", "finalexit"),
                         byteProperty("TriggerType", 4), floatProperty("DamageThreshold", 50.5F),
                         boolProperty("bInitiallyActive", false)})
        .addActorOfClass("Engine", "PathNode", {vectorProperty("Location", 0, 0, 0)})
        .addActorOfClass("MonsterHunt", "MonsterArenaEnd", {vectorProperty("Location", 1.5F, 2, 3)});
    const fs::path mapPath = writeMap(dir, map);
    // No flag: the list is short, and a consumer needs it on every run.
    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    INFO(result.out);
    REQUIRE(result.code == 0);
    CHECK(result.out.find("\"name\": \"MonsterEnd0\", \"class\": \"MonsterEnd\", \"location\": [-400, 0, 0], "
                          "\"tag\": \"finalexit\", \"triggerType\": 4, \"damageThreshold\": 50.5, "
                          "\"bInitiallyActive\": false}")
          != std::string::npos);
    // UTA-0130: nothing in the fixture sets these for the arena end.
    CHECK(result.out.find("\"triggerType\": null, \"damageThreshold\": null, \"bInitiallyActive\": null}")
          != std::string::npos);
    CHECK(result.out.find("\"name\": \"MonsterArenaEnd2\", \"class\": \"MonsterArenaEnd\", "
                          "\"location\": [1.5, 2, 3], \"tag\": ")
          != std::string::npos);
    // The PathNode is not an exit.
    const std::size_t exits = result.out.find("\"exits\": [");
    REQUIRE(exits != std::string::npos);
    CHECK(result.out.find("PathNode1", exits) == std::string::npos);
}
