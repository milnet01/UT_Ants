// UTA-0179: the game types an install registers, and each one's MapPrefix.
//
// Every install here is a synthetic one written to a temporary directory.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "ubake/GameTypes.h"
#include "ubake/Install.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using uta::test::bake::Packer;
using uta::test::bake::strProperty;
using uta::test::bake::TempDir;
using uta::test::bake::tinyPackage;
using uta::test::bake::writeFile;
using uta::ubake::GameType;
using uta::ubake::Install;
using uta::ubake::readGameTypes;

namespace {

std::vector<std::uint8_t> bytesOf(std::string_view text) { return {text.begin(), text.end()}; }

/// `text` as UTF-16LE with its byte order mark, as some .int files are saved.
std::vector<std::uint8_t> utf16(std::string_view text) {
    std::vector<std::uint8_t> bytes = {0xFF, 0xFE};
    for (const char ch : text) {
        bytes.push_back(static_cast<std::uint8_t>(ch));
        bytes.push_back(0);
    }
    return bytes;
}

/// An install registering game types every way the reference install does:
/// Botpack's own through SystemLocalized/int, a mod's through System, one
/// inheriting its prefix, one twice, and one whose package is absent. Two
/// entries that are not game types are beside them.
fs::path writeGameTypesInstall(const TempDir& dir) {
    const fs::path root = dir.path();
    writeFile(root / "System" / "Core.u", tinyPackage("Object"));
    writeFile(root / "System" / "Engine.u", tinyPackage("Actor"));

    Packer botpack;
    const std::int32_t tournament = botpack.addClass("TournamentGameInfo");
    const std::int32_t deathmatch = botpack.addClass("DeathMatchPlus", tournament, {strProperty("MapPrefix", "DM")});
    botpack.addClass("TeamGamePlus", deathmatch); // DeathMatchPlus's prefix
    botpack.addClass("CTFGame", tournament, {strProperty("MapPrefix", "CTF")});
    botpack.addClass("Relic");
    writeFile(root / "System" / "Botpack.u", botpack.build());

    Packer mod;
    mod.addClass("HuntGame", mod.importClass("Botpack", "TournamentGameInfo"), {strProperty("MapPrefix", "MH")});
    writeFile(root / "System" / "ModPkg.u", mod.build());

    const std::string_view bom = "\xEF\xBB\xBF";
    writeFile(root / "SystemLocalized" / "int" / "Botpack.int",
              // The mark sits right before an entry, so skipping it is graded.
              bytesOf(std::string(bom) +
                      "Object=(Name=Botpack.TeamGamePlus,Class=Class,MetaClass=Botpack.TournamentGameInfo)\r\n"
                      "Object=(Name=Botpack.DeathMatchPlus,Class=Class,MetaClass=Botpack.TournamentGameInfo)\r\n"
                      "Object=(Name=Botpack.Relic,Class=Class,MetaClass=Engine.Mutator)\r\n"
                      "Object=(Name=Botpack.Relic,Class=Texture,MetaClass=Botpack.TournamentGameInfo)\r\n"
                      "Preferences=(Caption=\"Tournament Game Types\",Class=Botpack.TournamentGameInfo)\r\n"));
    writeFile(root / "System" / "CTF.INT",
              utf16("[Public]\r\nObject=(Name=Botpack.CTFGame,Class=Class,MetaClass=Botpack.TournamentGameInfo)\r\n"));
    writeFile(root / "System" / "ModPkg.int",
              bytesOf("[Public]\n"
                      "object=( name=ModPkg.HuntGame, class=class, metaclass=botpack.tournamentgameinfo )\n"
                      "Object=(Name=Botpack.DeathMatchPlus,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n"
                      "Object=(Name=Gone.GoneGame,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n"
                      "Object=(Name=ModPkg.Missing,Class=Class,MetaClass=Botpack.TournamentGameInfo)\n"));
    return root;
}

} // namespace

TEST_CASE("UTA-0179: game types are read from both .int directories with their prefixes", "[ubake][gametypes]") {
    const TempDir dir;
    auto install = Install::open(writeGameTypesInstall(dir));
    REQUIRE(install.has_value());
    const auto types = readGameTypes(*install);

    std::vector<std::pair<std::string, std::string>> found;
    for (const GameType& type : types.found) found.emplace_back(type.name, type.mapPrefix);
    CHECK(found == std::vector<std::pair<std::string, std::string>>{
                       {"Botpack.CTFGame", "CTF"},
                       {"Botpack.DeathMatchPlus", "DM"},
                       {"Botpack.TeamGamePlus", "DM"},
                       {"ModPkg.HuntGame", "MH"},
                   });
    CHECK(types.unresolved == std::vector<std::string>{"Gone.GoneGame", "ModPkg.Missing"});
}

TEST_CASE("UTA-0179: an install with no .int files registers no game types", "[ubake][gametypes]") {
    const TempDir dir;
    writeFile(dir.path() / "System" / "Core.u", tinyPackage("Object"));
    auto install = Install::open(dir.path());
    REQUIRE(install.has_value());
    const auto types = readGameTypes(*install);
    CHECK(types.found.empty());
    CHECK(types.unresolved.empty());
}
