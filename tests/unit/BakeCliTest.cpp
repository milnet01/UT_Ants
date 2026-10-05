// UTA-0011's command-line cases -- INV-15.
//
// docs/specs/UTA-0011-map-baker.md SS 4.8. tools/ut-bake/Cli.cpp is compiled
// into this binary, so every case drives the command line as a function over
// a synthetic install on disk, with no process started.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH. Catch2 treats a
// comma as a filter separator, and parses a leading dash as an option, so ctest
// running such a case by name runs nothing and reports it failed.

#include "BakeFixture.h"

#include "ubake/Name.h"
#include "ut-bake/Cli.h"

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using uta::test::bake::picture;
using uta::test::bake::standardFixture;
using uta::test::bake::TextureSpec;
using uta::test::bake::TempDir;
using uta::test::bake::writeInstall;

namespace {

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args, std::optional<std::uint64_t> budgetBytes = {}) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Run result;
    result.code = budgetBytes.has_value()
                      ? uta::ubake::detail::runCli(views, out, err, *budgetBytes)
                      : uta::ubake::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

/// Whether `text` is exactly one JSON object and a newline: brackets balance
/// outside strings, and the outermost one closes exactly once, at the end.
bool isOneObject(const std::string& text) {
    if (text.size() < 3 || text.front() != '{' || text.back() != '\n') return false;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (--depth < 0) return false;
            if (depth == 0 && i + 2 != text.size()) return false; // something after it
        }
    }
    return depth == 0 && !inString;
}

bool hasKey(const std::string& json, std::string_view key) {
    return json.find("\"" + std::string(key) + "\":") != std::string::npos;
}

bool says(const std::string& json, std::string_view fragment) {
    return json.find(fragment) != std::string::npos;
}

struct Install {
    TempDir dir;
    fs::path install;
    fs::path map;
    fs::path out;

    Install() : Install(standardFixture()) {}

    explicit Install(const uta::test::bake::Fixture& fixture) {
        install = dir.path() / "install";
        map = writeInstall(install, fixture);
        out = dir.path() / "out";
    }

    [[nodiscard]] std::vector<std::string> bake(bool force = false) const {
        // UTA-0148: never the user's own texture cache.
        std::vector<std::string> args = {"--install", install.string(), "--out", out.string(),
                                         "--texture-cache", (dir.path() / "textures").string()};
        if (force) args.emplace_back("--force");
        args.push_back(map.string());
        return args;
    }
};

/// The number after `"<key>": ` inside the report's textureCache object.
std::uint32_t cacheCount(const std::string& json, std::string_view key) {
    const std::size_t block = json.find("\"textureCache\": {");
    REQUIRE(block != std::string::npos);
    const std::string label = "\"" + std::string(key) + "\": ";
    const std::size_t at = json.find(label, block);
    REQUIRE(at != std::string::npos);
    std::uint32_t value = 0;
    const char* const first = json.data() + at + label.size();
    REQUIRE(std::from_chars(first, json.data() + json.size(), value).ec == std::errc{});
    return value;
}

std::string bytesOf(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::size_t filesIn(const fs::path& directory) {
    std::size_t count = 0;
    if (!fs::exists(directory)) return 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(directory)) ++count;
    return count;
}

} // namespace

TEST_CASE("the install check prints one object and exits 0 on a good install",
          "[ubake][cli]") {
    const Install fixture;
    const Run result = run({"--check", fixture.install.string()});
    CHECK(result.code == 0);
    CHECK(isOneObject(result.out));
    CHECK(says(result.out, "\"schema\": 1"));
    CHECK(hasKey(result.out, "install"));
    CHECK(says(result.out, "\"ok\": true"));
    CHECK(says(result.out, "\"problems\": []"));
}

TEST_CASE("the install check exits 1 and names each problem on a bad install",
          "[ubake][cli]") {
    const TempDir empty;
    const Run result = run({"--check", empty.path().string()});
    CHECK(result.code == 1);
    CHECK(isOneObject(result.out));
    CHECK(says(result.out, "\"ok\": false"));
    CHECK(says(result.out, "\"what\": \"System/Botpack.u\""));
    CHECK(hasKey(result.out, "why"));
}

TEST_CASE("a bake prints written and then cached and exits 0", "[ubake][cli]") {
    const Install fixture;

    const Run written = run(fixture.bake());
    CHECK(written.code == 0);
    CHECK(isOneObject(written.out));
    CHECK(says(written.out, "\"verdict\": \"written\""));
    for (const std::string_view key : {"schema", "map", "bakerVersion", "name", "path", "rooms",
                                       "budget", "skipped", "textureCache"}) {
        INFO("key: " << key);
        CHECK(hasKey(written.out, key));
    }
    CHECK_FALSE(hasKey(written.out, "error"));
    CHECK(filesIn(fixture.out) == 1);

    const Run cached = run(fixture.bake());
    CHECK(cached.code == 0);
    CHECK(isOneObject(cached.out));
    CHECK(says(cached.out, "\"verdict\": \"cached\""));
    CHECK(hasKey(cached.out, "name"));
    CHECK(hasKey(cached.out, "path"));
    for (const std::string_view key : {"rooms", "budget", "skipped", "textureCache", "error"}) {
        INFO("key: " << key);
        CHECK_FALSE(hasKey(cached.out, key));
    }

    const Run forced = run(fixture.bake(true));
    CHECK(forced.code == 0);
    CHECK(says(forced.out, "\"verdict\": \"written\""));
}

TEST_CASE("a refused bake prints its error and exits 1", "[ubake][cli]") {
    Install fixture;
    fixture.map = fixture.install / "Maps" / "No-Such-Map.unr";
    const Run refused = run(fixture.bake());
    CHECK(refused.code == 1);
    CHECK(isOneObject(refused.out));
    CHECK(says(refused.out, "\"verdict\": \"refused\""));
    CHECK(hasKey(refused.out, "error"));
    for (const std::string_view key : {"name", "path", "rooms", "budget", "skipped", "textureCache"}) {
        INFO("key: " << key);
        CHECK_FALSE(hasKey(refused.out, key));
    }
}

TEST_CASE("an over-budget bake exits 1 and leaves no file", "[ubake][cli]") {
    // INV-15's over-budget row, through the seam with a budget of one byte.
    const Install fixture;
    const Run over = run(fixture.bake(), 1);
    CHECK(over.code == 1);
    CHECK(isOneObject(over.out));
    CHECK(says(over.out, "\"verdict\": \"over-budget\""));
    for (const std::string_view key : {"name", "path", "rooms", "budget", "skipped", "textureCache"}) {
        INFO("key: " << key);
        CHECK(hasKey(over.out, key));
    }
    CHECK(says(over.out, "\"budgetBytes\": 1"));
    CHECK_FALSE(hasKey(over.out, "error"));
    CHECK(filesIn(fixture.out) == 0);
}

TEST_CASE("UTA-0245: --fit-budget shrinks an over-budget bake and names it apart", "[ubake][cli]") {
    const Install fixture;
    const Run over = run(fixture.bake(), 1);
    REQUIRE(says(over.out, "\"verdict\": \"over-budget\""));
    const std::size_t at = over.out.find("\"workingSetBytes\": ");
    REQUIRE(at != std::string::npos);
    const std::uint64_t full = std::stoull(over.out.substr(at + 19));
    const std::string overName = over.out.substr(over.out.find("\"name\": \"") + 9, 64);

    // One byte under the whole: a round of fitting is enough.
    std::vector<std::string> fitArgs = fixture.bake();
    fitArgs.insert(fitArgs.begin(), "--fit-budget");
    const Run fitted = run(fitArgs, full - 1);
    INFO(fitted.out << fitted.err);
    CHECK(fitted.code == 0);
    CHECK(says(fitted.out, "\"verdict\": \"written\""));
    CHECK(says(fitted.out, "\"fitted\": {\"upscaleRounds\": "));
    CHECK_FALSE(says(fitted.out, overName)); // a fitted bake never takes the full bake's name
    CHECK(filesIn(fixture.out) == 1);

    // Without the switch the same budget still refuses: nobody gets the
    // fitted bake without asking for it (user, 2026-09-29).
    const Run plain = run(fixture.bake(), full - 1);
    CHECK(says(plain.out, "\"verdict\": \"over-budget\""));
    // Asked again, it is the cached fitted bake.
    const Run again = run(fitArgs, full - 1);
    CHECK(says(again.out, "\"verdict\": \"cached\""));
}

TEST_CASE("UTA-0179: the game-type list prints each type and the distinct prefixes", "[ubake][cli]") {
    const TempDir dir;
    using uta::test::bake::Packer;
    using uta::test::bake::strProperty;
    Packer botpack;
    const std::int32_t tournament = botpack.addClass("TournamentGameInfo");
    const std::int32_t deathmatch = botpack.addClass("DeathMatchPlus", tournament, {strProperty("MapPrefix", "DM")});
    botpack.addClass("TeamGamePlus", deathmatch);
    botpack.addClass("CTFGame", tournament, {strProperty("MapPrefix", "CTF")});
    uta::test::bake::writeFile(dir.path() / "System" / "Botpack.u", botpack.build());
    const std::string entries = "Object=(Name=Botpack.TeamGamePlus,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n"
                                "Object=(Name=Botpack.DeathMatchPlus,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n"
                                "Object=(Name=Botpack.CTFGame,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n"
                                "Object=(Name=Gone.GoneGame,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n";
    uta::test::bake::writeFile(dir.path() / "System" / "Botpack.int", {entries.begin(), entries.end()});

    const Run result = run({"--game-types", dir.path().string()});
    INFO(result.out);
    CHECK(result.code == 0);
    CHECK(isOneObject(result.out));
    CHECK(says(result.out, "\"schema\": 1"));
    CHECK(says(result.out, "\"gameTypes\": [{\"name\": \"Botpack.CTFGame\", \"mapPrefix\": \"CTF\"}, "
                           "{\"name\": \"Botpack.DeathMatchPlus\", \"mapPrefix\": \"DM\"}, "
                           "{\"name\": \"Botpack.TeamGamePlus\", \"mapPrefix\": \"DM\"}]"));
    CHECK(says(result.out, "\"unresolved\": [\"Gone.GoneGame\"]"));
    CHECK(says(result.out, "\"mapPrefixes\": [\"CTF\", \"DM\"]"));
    // UTA-0208: the launcher compares this with the baker that made each bake.
    CHECK(says(result.out, "\"bakerVersion\": \"" + uta::ubake::bakerVersion() + "\""));
}

TEST_CASE("UTA-0179: the game-type list exits 1 when the install is not a directory", "[ubake][cli]") {
    const TempDir dir;
    const Run result = run({"--game-types", (dir.path() / "absent").string()});
    CHECK(result.code == 1);
    CHECK(isOneObject(result.out));
    CHECK(hasKey(result.out, "error"));
}

TEST_CASE("wrong arguments exit 2 and print nothing on standard output", "[ubake][cli]") {
    const std::vector<std::vector<std::string>> wrong = {
        {},
        {"--check"},
        {"--bogus"},
        {"--install", "i", "--out", "o"},
        {"--install", "i", "map"},
        {"--check", "i", "--force"},
        {"--game-types"},
        {"--game-types", "i", "map"},
        {"--game-types", "i", "--check", "j"},
        {"--install", "i", "--out", "o", "first", "second"},
        {"--install", "i", "--install", "j", "--out", "o", "map"},
    };
    for (const std::vector<std::string>& args : wrong) {
        const Run result = run(args);
        CHECK(result.code == 2);
        CHECK(result.out.empty());
        CHECK(says(result.err, "usage:"));
    }

    const Run help = run({"--help"});
    CHECK(help.code == 0);
    CHECK(help.out.empty());
    CHECK(says(help.err, "usage:"));
}

TEST_CASE("UTA-0141: a clash that hides what the map asks for is warned about", "[ubake][cli]") {
    const Install fixture;

    // A shared name the map does not suffer from: Engine.u wins the name and
    // holds every Engine object the map asks for. Stock installs are full of
    // these (BotPack, Engine, UnrealShare), so they stay quiet. The shadow must
    // open: one that does not is named whenever the winner falls short, and
    // this fixture's Engine.u lacks the Palette class the map imports.
    uta::test::bake::writeFile(fixture.install / "Textures" / "Engine.utx", uta::test::bake::tinyPackage("Unrelated"));
    const Run quiet = run(fixture.bake());
    REQUIRE(quiet.code == 0);
    CHECK(says(quiet.out, "\"packageClashes\": []"));
    CHECK_FALSE(says(quiet.err, "warning"));

    // One it does: System/TexPkg.u wins the name, as in the game's Paths
    // order, and does not hold the map's TexPkg.Metal.Plate, which only the
    // shadowed Textures/TexPkg.utx does -- MH-MeltTown's shape.
    uta::test::bake::writeFile(fixture.install / "System" / "TexPkg.u", uta::test::bake::classPackage("Unrelated"));
    const Run clash = run(fixture.bake(true));
    INFO(clash.out << clash.err);
    CHECK(clash.code == 0); // warn and carry on (user decisions, 2026-09-13 and 2026-09-25)
    CHECK(says(clash.out, "\"verdict\": \"written\""));
    CHECK(says(clash.out, "\"packageClashes\": [{\"package\": \"texpkg\", \"object\": \""));
    const std::size_t shadowed = clash.out.find("\"shadowed\": [");
    REQUIRE(shadowed != std::string::npos);
    CHECK(clash.out.find("TexPkg.utx", shadowed) != std::string::npos);
    CHECK(says(clash.err, "warning"));
    CHECK(says(clash.err, "TexPkg.utx"));

    // A cached bake warns too: the clash is a fact about the install.
    const Run cached = run(fixture.bake());
    CHECK(says(cached.out, "\"verdict\": \"cached\""));
    CHECK(says(cached.out, "\"packageClashes\": [{\"package\": \"texpkg\""));
}

TEST_CASE("UTA-0141: a winner holding the name in another group still clashes", "[ubake][cli]") {
    const Install fixture;
    // The winner holds Metal.Door and a Plate, but in the group Other; the map
    // asks for Metal.Plate, which only the shadowed Textures/TexPkg.utx holds.
    // Matched by name and class alone, the winner's Other.Plate hid the clash.
    const uta::test::bake::Picture art = uta::test::bake::picture(7);
    uta::test::bake::writeFile(fixture.install / "System" / "TexPkg.u",
                               uta::test::bake::texturePackage({{.name = "Plate", .group = "Other", .picture = art},
                                                                {.name = "Door", .group = "Metal", .picture = art}}));
    const Run clash = run(fixture.bake());
    INFO(clash.out << clash.err);
    CHECK(clash.code == 0);
    CHECK(says(clash.out, "\"packageClashes\": [{\"package\": \"texpkg\", \"object\": \"texpkg.Metal.Plate\""));
}

TEST_CASE("UTA-0141: a shadowed file that cannot be read is named when the winner falls short", "[ubake][cli]") {
    const Install fixture;
    // The winner lacks Metal.Plate, and the only other TexPkg does not open, so
    // whether it holds the object cannot be known. It was dropped in silence.
    uta::test::bake::writeFile(fixture.install / "System" / "TexPkg.u", uta::test::bake::classPackage("Unrelated"));
    uta::test::bake::writeFile(fixture.install / "Textures" / "TexPkg.utx", std::vector<std::uint8_t>{'x'});
    const Run clash = run(fixture.bake());
    INFO(clash.out << clash.err);
    CHECK(clash.code == 0);
    const std::size_t shadowed = clash.out.find("\"shadowed\": [");
    REQUIRE(shadowed != std::string::npos);
    CHECK(clash.out.find("TexPkg.utx", shadowed) != std::string::npos);
}

TEST_CASE("UTA-0117: the install check names the version and warns unless it is 469", "[ubake][cli]") {
    const Install fixture;
    const auto writeIni = [&](const fs::path& relative, const std::string& text) {
        uta::test::bake::writeFile(fixture.install / relative, std::vector<std::uint8_t>(text.begin(), text.end()));
    };

    // Never run: nothing records a version.
    const Run unknown = run({"--check", fixture.install.string()});
    CHECK(unknown.code == 0); // warned about, never refused (user decision, 2026-09-25)
    CHECK(says(unknown.out, "\"ok\": true"));
    CHECK(says(unknown.out, "\"version\": null"));
    CHECK(says(unknown.out, "\"warnings\": [\""));
    CHECK(says(unknown.err, "warning"));

    // The game writes FirstRun when a version first runs.
    writeIni("System/UnrealTournament.ini", "[FirstRun]\r\nFirstRun=436\r\n");
    const Run old = run({"--check", fixture.install.string()});
    CHECK(old.code == 0);
    CHECK(says(old.out, "\"version\": 436"));
    CHECK(says(old.out, "436"));
    CHECK(says(old.err, "469"));

    // A 64-bit System64 beside it that has run 469 wins: the higher is current.
    writeIni("System64/UnrealTournament.ini", "[FirstRun]\nfirstrun=469\n");
    const Run tested = run({"--check", fixture.install.string()});
    CHECK(tested.code == 0);
    CHECK(says(tested.out, "\"version\": 469"));
    CHECK(says(tested.out, "\"warnings\": []"));
    CHECK_FALSE(says(tested.err, "warning"));
}

TEST_CASE("UTA-0148: a second bake takes every material from the texture cache, byte for byte",
          "[ubake][cli]") {
    const Install fixture;
    const Run cold = run(fixture.bake());
    REQUIRE(cold.code == 0);
    CHECK(cacheCount(cold.out, "hits") == 0);
    const std::uint32_t made = cacheCount(cold.out, "misses");
    REQUIRE(made > 0);
    std::string first;
    for (const auto& entry : fs::directory_iterator(fixture.out)) first = bytesOf(entry.path());
    REQUIRE_FALSE(first.empty());

    // Forced, so the bundle is made again: every material now comes from the
    // cache, and the file is the same to the byte.
    const Run warm = run(fixture.bake(true));
    REQUIRE(warm.code == 0);
    CHECK(cacheCount(warm.out, "hits") == made);
    CHECK(cacheCount(warm.out, "misses") == 0);
    std::string second;
    for (const auto& entry : fs::directory_iterator(fixture.out)) second = bytesOf(entry.path());
    CHECK(second == first);

    // And with the cache off the same bytes again, and nothing counted.
    std::vector<std::string> off = {"--install", fixture.install.string(), "--out",
                                    fixture.out.string(), "--force", fixture.map.string()};
    const Run plain = run(off);
    REQUIRE(plain.code == 0);
    CHECK(cacheCount(plain.out, "hits") == 0);
    CHECK(cacheCount(plain.out, "misses") == 0);
    std::string third;
    for (const auto& entry : fs::directory_iterator(fixture.out)) third = bytesOf(entry.path());
    CHECK(third == first);
}

/// How many `"name": ` entries the report's byTexture list holds.
std::size_t listedTextures(const std::string& json) {
    const std::size_t from = json.find("\"byTexture\": [");
    const std::size_t to = json.find(']', from);
    REQUIRE(from != std::string::npos);
    REQUIRE(to != std::string::npos);
    std::size_t count = 0;
    for (std::size_t at = json.find("\"name\": ", from); at < to; at = json.find("\"name\": ", at + 1))
        ++count;
    return count;
}

TEST_CASE("UTA-0264: a written bake lists its ten largest textures unless asked for all",
          "[ubake][cli]") {
    // Eight more map textures, each worn by a surface, put the budget list
    // well past the short list's ten.
    uta::test::bake::Fixture many = standardFixture();
    for (std::uint8_t i = 0; i < 8; ++i) {
        const std::int32_t texture =
            many.map.addTexture(TextureSpec{"Extra" + std::to_string(i), "", picture(10 + i), false});
        many.map.addSurface(texture);
    }
    const Install fixture(many);

    std::vector<std::string> fullArgs = fixture.bake();
    fullArgs.insert(fullArgs.begin(), "--full-budget");
    const Run full = run(fullArgs);
    REQUIRE(full.code == 0);
    const std::size_t all = listedTextures(full.out);
    REQUIRE(all > 10);
    CHECK(says(full.out, "\"byTextureOmitted\": 0"));

    const Run written = run(fixture.bake(true));
    REQUIRE(written.code == 0);
    CHECK(isOneObject(written.out));
    CHECK(listedTextures(written.out) == 10);
    CHECK(says(written.out, "\"byTextureOmitted\": " + std::to_string(all - 10)));

    // Over budget, the whole list is what says which texture to cap.
    const Run over = run(fixture.bake(true), 1);
    REQUIRE(over.code == 1);
    CHECK(listedTextures(over.out) == all);
    CHECK(says(over.out, "\"byTextureOmitted\": 0"));
}

TEST_CASE("UTA-0113: a recipe given by --recipe renames the bake and is reported", "[ubake][cli]") {
    const Install fixture;
    const fs::path recipe = fixture.dir.path() / "mine.recipe";
    std::ofstream(recipe, std::ios::binary) << "ut-ants recipe 1\n[map]\nfile = DM-Fixture\n"
                                               "[material dm-fixture.floor]\nmetallic = true\n"
                                               "[material otherpkg.nothing]\nmetallic = true\n";

    const Run plain = run(fixture.bake());
    REQUIRE(plain.code == 0);
    CHECK(says(plain.out, "\"recipe\": null"));

    std::vector<std::string> args = fixture.bake();
    args.insert(args.end() - 1, {"--recipe", recipe.string()});
    const Run with = run(args);
    REQUIRE(with.code == 0);
    CHECK(isOneObject(with.out));
    CHECK(says(with.out, "\"verdict\": \"written\""));
    CHECK(says(with.out, "mine.recipe\""));
    CHECK(says(with.out, "\"recipeUnused\": [\"otherpkg.nothing\"]"));
    CHECK(says(with.err, "otherpkg.nothing"));
    CHECK(filesIn(fixture.out) == 2); // a second name, beside the plain bake

    std::ofstream(recipe, std::ios::binary) << "ut-ants recipe 1\n[map]\nfile = dm-other\n";
    const Run wrongMap = run(args);
    CHECK(wrongMap.code == 1);
    CHECK(says(wrongMap.out, "\"verdict\": \"refused\""));
    CHECK(says(wrongMap.err, "dm-other"));
}
