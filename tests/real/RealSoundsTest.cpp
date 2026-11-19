// UTA-0478 INV-10: the sounds DM-Deck16][ and the stock weapon set carry,
// gathered from the install and decoded as the client decodes them
// (docs/specs/UTA-0478-baked-sounds.md SS 4.3, SS 4.4 and SS 13). It prints
// the figures SS 13 quotes.

#include "RealSupport.h"

#include "core/FileSystem.h"
#include "uaudio/Wav.h"
#include "ubake/Actors.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubake/Sounds.h"
#include "ugame/WeaponSet.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Figures {
    std::size_t sounds = 0, refused = 0;
    std::uint64_t storedBytes = 0, decodedBytes = 0;
};

/// Decodes each sound as uaudio::SoundBank does. One decodeWav refuses must
/// be refused for its format: a payload that is not a WAV at all would mean
/// the baker stored the wrong bytes.
Figures decodeAll(const uta::ubake::FoundSounds& found) {
    Figures figures;
    for (const uta::ubundle::BakedSound& sound : found.sounds) {
        ++figures.sounds;
        figures.storedBytes += sound.data.size();
        REQUIRE(sound.data.size() >= 4);
        const std::string tag(reinterpret_cast<const char*>(sound.data.data()), 4);
        INFO(sound.path);
        CHECK(tag == "RIFF");
        if (const auto clip = uta::uaudio::decodeWav(sound.data); clip.has_value()) {
            figures.decodedBytes += clip->samples.size() * sizeof(float);
        } else {
            ++figures.refused;
            WARN(sound.path << " is refused: " << clip.error().message());
        }
    }
    return figures;
}

} // namespace

TEST_CASE("UTA-0478 INV-10: DM-Deck16][ carries its actors' sounds and each is a WAV", "[real][ubake][sounds]") {
    const fs::path root(UTA_UT_INSTALL_DIR);
    const fs::path mapPath = root / "Maps" / "DM-Deck16][.unr";
    const std::vector<std::byte> raw = uta::test::real::readWhole(mapPath);
    const auto map = uta::upkg::Package::open(raw);
    REQUIRE(map.has_value());
    const std::string mapName = uta::ubake::detail::mapNameOf(mapPath);
    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());
    const auto levelExport = uta::ubake::detail::findLevel(*map, mapName);
    REQUIRE(levelExport.has_value());
    const auto level = uta::upkg::readLevel(*map, **levelExport);
    REQUIRE(level.has_value());
    const auto actors = uta::ubake::buildActors(*map, mapName, *level, install->resolver());
    REQUIRE(actors.has_value());

    const auto found = uta::ubake::mapSounds(actors->placements, *map, mapName, install->resolver());
    if (!found.has_value()) FAIL(found.error().message());
    const Figures figures = decodeAll(*found);
    WARN("DM-Deck16][: " << figures.sounds << " sounds, " << figures.storedBytes << " bytes stored, "
                         << figures.decodedBytes << " decoded, " << figures.refused << " refused, "
                         << found->skipped.size() << " skipped");
    CHECK(figures.sounds >= 1);
    CHECK(found->skipped.empty());
}

TEST_CASE("UTA-0478 INV-10: the stock weapon set carries a sound for every weapon class", "[real][ubake][sounds]") {
    const auto weapons = uta::ugame::load(uta::ugame::shippedDirectory() / "stock.weapons");
    REQUIRE(weapons.has_value());
    auto install = uta::ubake::Install::open(fs::path(UTA_UT_INSTALL_DIR));
    REQUIRE(install.has_value());

    std::vector<std::string> classes;
    for (const uta::ugame::Weapon& weapon : weapons->weapons) {
        classes.push_back(weapon.className);
        // Each class alone, so one silent weapon cannot hide behind the rest.
        const auto own = uta::ubake::detail::soundSetOf({weapon.className}, install->resolver());
        REQUIRE(own.has_value());
        INFO(weapon.className);
        CHECK(own->unresolved.empty());
        CHECK(own->found.sounds.size() >= 1);
    }
    const auto set = uta::ubake::detail::soundSetOf(classes, install->resolver());
    if (!set.has_value()) FAIL(set.error().message());
    const Figures figures = decodeAll(set->found);
    WARN("stock weapon set: " << classes.size() << " classes, " << figures.sounds << " sounds, "
                              << figures.storedBytes << " bytes stored, " << figures.decodedBytes << " decoded, "
                              << figures.refused << " refused, " << set->found.skipped.size() << " skipped");
    CHECK(set->unresolved.empty());
    CHECK(set->found.skipped.empty());
}
