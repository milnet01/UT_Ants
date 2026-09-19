// UTA-0170: the map launcher's parts that need no window.
// apps/ut-ants/MapList.cpp is compiled into this binary.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/MapList.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace uta::client;
namespace stdfs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = stdfs::temp_directory_path() /
                ("uta-maplist-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(counter++));
        stdfs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        stdfs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const stdfs::path& path() const { return path_; }

private:
    stdfs::path path_;
};

void touch(const stdfs::path& path) {
    stdfs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

std::vector<std::string> namesOf(const std::vector<MapFile>& maps) {
    std::vector<std::string> names;
    for (const MapFile& map : maps) names.push_back(map.name);
    return names;
}

} // namespace

TEST_CASE("UTA-0170: every .unr in Maps is listed and sorted ignoring case", "[client]") {
    const TempDir install;
    touch(install.path() / "maps" / "MH-Zeta.unr");
    touch(install.path() / "maps" / "dm-alpha.UNR");
    touch(install.path() / "maps" / "CTF-Beta.unr");
    touch(install.path() / "maps" / "notes.txt");
    touch(install.path() / "maps" / "Leftover.unr.tmp");
    stdfs::create_directories(install.path() / "maps" / "Folder.unr");

    const std::vector<MapFile> maps = listMaps(install.path());
    CHECK(namesOf(maps) == std::vector<std::string>{"CTF-Beta", "dm-alpha", "MH-Zeta"});
    REQUIRE(maps.size() == 3);
    CHECK(maps[1].path.filename() == "dm-alpha.UNR");
}

TEST_CASE("UTA-0170: an install with no Maps directory lists nothing", "[client]") {
    const TempDir install;
    CHECK(listMaps(install.path()).empty());
    CHECK(listMaps(install.path() / "absent").empty());
}

TEST_CASE("UTA-0170: the filter keeps names containing it ignoring case", "[client]") {
    const std::vector<MapFile> maps{{"MH-NivenSB", {}}, {"DM-Deck16", {}}, {"mh-ancient", {}}};
    CHECK(filterMaps(maps, "") == std::vector<std::size_t>{0, 1, 2});
    CHECK(filterMaps(maps, "mh-") == std::vector<std::size_t>{0, 2});
    CHECK(filterMaps(maps, "DECK") == std::vector<std::size_t>{1});
    CHECK(filterMaps(maps, "ctf").empty());
}

TEST_CASE("UTA-0179: game-type prefixes are read from ut-bake's top-level array", "[client]") {
    const auto prefixes = readMapPrefixes(
        R"({"schema": 1, "install": "/ut", "gameTypes": [{"name": "Botpack.CTFGame", "mapPrefix": "no"}], )"
        R"("unresolved": [], "mapPrefixes": ["CTF", "D\u004d", ""]})" "\n",
        0);
    REQUIRE(prefixes.has_value());
    CHECK(*prefixes == std::vector<std::string>{"CTF", "DM", ""});
}

TEST_CASE("UTA-0179: a failed or silent game-type run gives no prefixes", "[client]") {
    CHECK_FALSE(readMapPrefixes("", 1).has_value());
    CHECK_FALSE(readMapPrefixes(R"({"schema": 1, "error": "gone"})", 1).has_value());
    CHECK_FALSE(readMapPrefixes(R"({"mapPrefixes": ["DM"]})", 1).has_value());
    CHECK_FALSE(readMapPrefixes(R"({"mapPrefixes": "DM"})", 0).has_value());
    CHECK_FALSE(readMapPrefixes(R"({"gameTypes": [{"mapPrefixes": ["DM"]}]})", 0).has_value());
}

TEST_CASE("UTA-0179: only maps a game type's prefix names are playable", "[client]") {
    const std::vector<MapFile> maps = {{"CityIntro", {}}, {"ctf-Face", {}}, {"DM-Deck16][", {}},
                                       {"EOL_Assault", {}}, {"MH_Backhome-[WEO]", {}}, {"UT-Logo-Map", {}}};
    std::vector<std::string> kept;
    for (const MapFile& map : playableMaps(maps, {"DM", "CTF", "MH"})) kept.push_back(map.name);
    CHECK(kept == std::vector<std::string>{"ctf-Face", "DM-Deck16][", "MH_Backhome-[WEO]"});
    CHECK(playableMaps(maps, {}).empty());
    CHECK(playableMaps(maps, {"DM", ""}).size() == maps.size());
}

TEST_CASE("UTA-0170: a written or cached bake gives its path", "[client]") {
    const BakeAnswer written = readBakeAnswer(
        R"({"schema": 1, "map": "/i/Maps/A.unr", "bakerVersion": "18", "verdict": "written", "name": "ab",)"
        R"( "path": "/c/bakes/ab.utab", "rooms": {"withoutFootprint": [1, 2]}, "skipped": []})",
        0);
    CHECK(written.baked);
    CHECK(written.path == stdfs::path("/c/bakes/ab.utab"));
    CHECK(written.failure.empty());

    const BakeAnswer cached =
        readBakeAnswer(R"({"schema": 1, "verdict": "cached", "name": "ab", "path": "C:\\c\\ab.utab"})" "\n", 0);
    CHECK(cached.baked);
    CHECK(cached.path == stdfs::path("C:\\c\\ab.utab"));
}

TEST_CASE("UTA-0170: only a top-level path counts", "[client]") {
    // A nested member of the same name, and one inside a string, are not it.
    const BakeAnswer answer = readBakeAnswer(
        R"({"map": "x \"path\": \"/wrong\"", "budget": {"path": "/nested", "list": [{"path": "/deeper"}]},)"
        R"( "verdict": "written", "path": "/right.utab"})",
        0);
    CHECK(answer.baked);
    CHECK(answer.path == stdfs::path("/right.utab"));
}

TEST_CASE("UTA-0170: a refused bake gives ut-bake's reason", "[client]") {
    const BakeAnswer answer = readBakeAnswer(
        R"({"schema": 1, "map": "m", "bakerVersion": "18", "verdict": "refused", "error": "no \"Level\"\u0021"})", 1);
    CHECK_FALSE(answer.baked);
    CHECK(answer.failure == "no \"Level\"!");
}

TEST_CASE("UTA-0170: an over-budget bake says so", "[client]") {
    const BakeAnswer answer =
        readBakeAnswer(R"({"verdict": "over-budget", "name": "ab", "path": "/c/ab.utab", "budget": {}})", 1);
    CHECK_FALSE(answer.baked);
    CHECK(answer.failure.find("budget") != std::string::npos);
}

TEST_CASE("UTA-0170: no JSON or a bad exit is a failure naming the exit code", "[client]") {
    const BakeAnswer silent = readBakeAnswer("", 3);
    CHECK_FALSE(silent.baked);
    CHECK(silent.failure.find('3') != std::string::npos);

    CHECK_FALSE(readBakeAnswer(R"({"verdict": "written"})", 0).baked);
    CHECK_FALSE(readBakeAnswer(R"({"verdict": "written", "path": "/a.utab"})", 1).baked);
    CHECK_FALSE(readBakeAnswer(R"({"verdict": "sideways", "path": "/a.utab"})", 0).baked);
}

TEST_CASE("UTA-0170: a result round-trips and an absent one is nothing", "[client]") {
    const TempDir data;
    const stdfs::path results = data.path() / "results";
    CHECK_FALSE(readResult(results, "MH-A").has_value());

    REQUIRE(writeResult(results, "MH-A", {.failed = true, .failure = "package X is not in the install"}));
    const auto failed = readResult(results, "MH-A");
    REQUIRE(failed.has_value());
    CHECK(failed->failed);
    CHECK(failed->failure == "package X is not in the install");

    REQUIRE(writeResult(results, "MH-A", {}));
    const auto baked = readResult(results, "MH-A");
    REQUIRE(baked.has_value());
    CHECK_FALSE(baked->failed);
}

TEST_CASE("UTA-0170: notes round-trip and empty notes remove the file", "[client]") {
    const TempDir data;
    const stdfs::path notes = data.path() / "notes";
    CHECK(readNotes(notes, "MH-A").empty());

    REQUIRE(writeNotes(notes, "MH-A", "the lift is dark\nsky flickers"));
    CHECK(readNotes(notes, "MH-A") == "the lift is dark\nsky flickers");
    CHECK(stdfs::exists(notes / "MH-A.txt"));

    REQUIRE(writeNotes(notes, "MH-A", ""));
    CHECK_FALSE(stdfs::exists(notes / "MH-A.txt"));
    REQUIRE(writeNotes(notes, "MH-B", "")); // nothing to remove is not an error
}

TEST_CASE("UTA-0170: backspace removes one whole UTF-8 character", "[client]") {
    CHECK(withoutLastCharacter("") == "");
    CHECK(withoutLastCharacter("ab") == "a");
    CHECK(withoutLastCharacter("a\xC3\xA9") == "a");         // é
    CHECK(withoutLastCharacter("a\xE2\x82\xAC") == "a");     // €
    CHECK(withoutLastCharacter("\xF0\x9F\x98\x80") == "");   // an emoji
}

TEST_CASE("UTA-0170: notes wrap at newlines and between words", "[client]") {
    CHECK(wrapText("", 10) == std::vector<std::string>{""});
    CHECK(wrapText("one two three", 7) == std::vector<std::string>{"one two", "three"});
    CHECK(wrapText("a\n\nb", 10) == std::vector<std::string>{"a", "", "b"});
    CHECK(wrapText("abcdefghij", 4) == std::vector<std::string>{"abcd", "efgh", "ij"});
    CHECK(wrapText("end\n", 10) == std::vector<std::string>{"end", ""});
}

TEST_CASE("UTA-0190: a note is appended on a line of its own", "[client]") {
    const TempDir data;
    const stdfs::path file = notesFile(data.path() / "notes", "MH-A");
    const auto contents = [&file] {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };

    REQUIRE(appendNote(file, "first"));
    CHECK(contents() == "first\n");
    REQUIRE(appendNote(file, "second"));
    CHECK(contents() == "first\nsecond\n");

    // Notes typed in the launcher need not end with a newline.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "the lift is dark";
    REQUIRE(appendNote(file, "third"));
    CHECK(contents() == "the lift is dark\nthird\n");
    CHECK(readNotes(data.path() / "notes", "MH-A") == "the lift is dark\nthird\n");
}
