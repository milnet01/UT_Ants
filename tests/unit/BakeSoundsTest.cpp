// UTA-0478: which sounds a map's bake stores, and the weapon set's bundle.
//
// docs/specs/UTA-0478-baked-sounds.md SS 4.3 and SS 4.4, INV-4 to INV-7.
// Fixtures are synthetic packages (BakeFixture.h), so every case runs with
// no Unreal Tournament present.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/FileSystem.h"
#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubake/Sounds.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubundle::BakedSound;
namespace detail = uta::ubake::detail;

namespace {

std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for (const unsigned value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

/// The fixture's Engine, as BakePropsTest builds it.
std::vector<std::uint8_t> engine() {
    Packer packer;
    const std::int32_t brush = packer.addClass("Brush", 0, {boolProperty("bStatic", true)});
    packer.addClass("Mover", brush, {boolProperty("bStatic", false)});
    packer.addExport(0, 0, "Actor", {});
    return packer.build();
}

BakeResult bake(Fixture& fixture, MemoryPackages& packages) {
    packages.add("engine", engine());
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const std::vector<std::byte> bytes = asByteVector(fixture.map.build());
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    auto baked = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                              uta::umat::TEXTURE_BUDGET_BYTES);
    if (!baked.has_value()) FAIL("the bake was refused: " << baked.error().message());
    return std::move(*baked);
}

/// A map with one floor; `place` adds the actors.
Fixture floorOnly() {
    Fixture fixture;
    const std::int32_t floor = fixture.map.addTexture(TextureSpec{"Floor", "", picture(2), false});
    fixture.map.addSurface(floor);
    return fixture;
}

} // namespace

TEST_CASE("UTA-0478 INV-4: a map's SNDS holds each sound its actors and their classes name once", "[ubake]") {
    Fixture fixture = floorOnly();
    MapBuilder& map = fixture.map;
    const std::int32_t hum = map.importSound("Ambience", "Machines", "Hum");
    const std::int32_t click = map.importSound("Ambience", "", "Click");
    const std::int32_t unused = map.importSound("Ambience", "", "Unused");
    static_cast<void>(unused); // imported and named by no property
    const std::int32_t skin = map.addTexture(TextureSpec{"Skin", "", picture(1), false});
    const std::int32_t own = map.addSound(SoundSpec{"Buzz", "", {9, 8, 7, 6}});

    // A class whose default names Click, which no actor names itself.
    const std::int32_t speaker = map.addClass("Speaker", 0, {objectProperty("ClickSound", click)});
    map.addActor("Speaker0", speaker, {objectProperty("AmbientSound", hum)});
    // The same sound again, a null reference, and a texture: none adds a sound.
    map.addActor("Speaker1", speaker,
                 {objectProperty("AmbientSound", hum), objectProperty("OtherSound", 0), objectProperty("Skin", skin)});
    // A sound the map holds itself: its path is headed by the map's name.
    map.addActor("Speaker2", speaker, {objectProperty("LocalSound", own)});

    MemoryPackages packages;
    packages.add("core", tinyPackage("Object"));
    packages.add("ambience", soundPackage({SoundSpec{"Hum", "Machines", {1, 2, 3}},
                                           SoundSpec{"Click", "", {4, 5}},
                                           SoundSpec{"Unused", "", {6}}}));
    const BakeResult result = bake(fixture, packages);

    REQUIRE(result.bundle.sounds.has_value());
    const std::vector<BakedSound>& sounds = *result.bundle.sounds;
    REQUIRE(sounds.size() == 3);
    CHECK(sounds[0].path == "ambience.click");
    CHECK(sounds[0].data == bytesOf({4, 5}));
    CHECK(sounds[1].path == "ambience.machines.hum");
    CHECK(sounds[1].data == bytesOf({1, 2, 3}));
    CHECK(sounds[2].path == "dm-fixture.buzz");
    CHECK(sounds[2].data == bytesOf({9, 8, 7, 6}));
    CHECK(result.skippedSounds.empty());
}

TEST_CASE("UTA-0478 INV-4: a map naming no sound carries an empty SNDS", "[ubake]") {
    Fixture fixture = floorOnly();
    MemoryPackages packages;
    packages.add("core", tinyPackage("Object"));
    const BakeResult result = bake(fixture, packages);
    REQUIRE(result.bundle.sounds.has_value());
    CHECK(result.bundle.sounds->empty());
}

TEST_CASE("UTA-0478 INV-5: a sound that does not read or is empty is left out and reported", "[ubake]") {
    Fixture fixture = floorOnly();
    MapBuilder& map = fixture.map;
    const std::int32_t good = map.importSound("Ambience", "", "Good");
    const std::int32_t torn = map.importSound("Ambience", "", "Torn");
    const std::int32_t empty = map.importSound("Ambience", "", "Empty");
    const std::int32_t gone = map.importSound("Ambience", "", "Gone");
    const std::int32_t lost = map.importSound("NotInstalled", "", "Lost");
    const std::int32_t speaker = map.addClass("Speaker");
    map.addActor("Speaker0", speaker,
                 {objectProperty("A", good), objectProperty("B", torn), objectProperty("C", empty),
                  objectProperty("D", gone), objectProperty("E", lost)});

    MemoryPackages packages;
    packages.add("core", tinyPackage("Object"));
    packages.add("ambience", soundPackage({SoundSpec{"Good", "", {1}},
                                           SoundSpec{"Torn", "", {2, 3}, true},
                                           SoundSpec{"Empty", "", {}}}));
    const BakeResult result = bake(fixture, packages);

    REQUIRE(result.bundle.sounds.has_value());
    REQUIRE(result.bundle.sounds->size() == 1);
    CHECK(result.bundle.sounds->front().path == "ambience.good");
    // Ascending by path. A sound that is not there, and one whose package is
    // not, are not sounds anyone can tell from another object: unreported.
    REQUIRE(result.skippedSounds.size() == 2);
    CHECK(result.skippedSounds[0].path == "ambience.empty");
    CHECK(result.skippedSounds[0].reason == "the sound holds no bytes");
    CHECK(result.skippedSounds[1].path == "ambience.torn");
    CHECK_FALSE(result.skippedSounds[1].reason.empty());
}

namespace {

/// A weapons package: Gun under GunBase, whose default names FireSound; Gun
/// names the class Shell, whose default names HitSound and the class Smoke,
/// whose default names PuffSound.
std::vector<std::uint8_t> weaponsPackage() {
    Packer packer;
    const std::int32_t fire = packer.addSound(SoundSpec{"Fire", "Gun", {1, 1}});
    const std::int32_t hit = packer.addSound(SoundSpec{"Hit", "", {2, 2, 2}});
    const std::int32_t puff = packer.addSound(SoundSpec{"Puff", "", {3}});
    const std::int32_t smoke = packer.addClass("Smoke", 0, {objectProperty("PuffSound", puff)});
    const std::int32_t shell =
        packer.addClass("Shell", 0, {objectProperty("HitSound", hit), classProperty("TrailClass", smoke)});
    const std::int32_t base = packer.addClass("GunBase", 0, {objectProperty("FireSound", fire)});
    packer.addClass("Gun", base, {classProperty("ProjectileClass", shell)});
    return packer.build();
}

MemoryPackages weaponPackages() {
    MemoryPackages packages;
    packages.add("core", tinyPackage("Object"));
    packages.add("engine", engine());
    packages.add("weapons", weaponsPackage());
    return packages;
}

} // namespace

TEST_CASE("UTA-0478 INV-6: a sound set holds its classes' sounds and those one class further", "[ubake]") {
    MemoryPackages packages = weaponPackages();
    const auto set = detail::soundSetOf({"Weapons.Gun", "weapons.nothere", "NoPackage.Gun"}, {}, packages.resolver());
    if (!set.has_value()) FAIL("refused: " << set.error().message());

    // Fire is GunBase's, so only effective defaults find it. Hit is Shell's,
    // one class on. Puff is Smoke's, two on, and is not followed.
    REQUIRE(set->found.sounds.size() == 2);
    CHECK(set->found.sounds[0].path == "weapons.gun.fire");
    CHECK(set->found.sounds[0].data == bytesOf({1, 1}));
    CHECK(set->found.sounds[1].path == "weapons.hit");
    CHECK(set->found.sounds[1].data == bytesOf({2, 2, 2}));
    CHECK(set->unresolved == std::vector<std::string>{"nopackage.gun", "weapons.nothere"});
    CHECK(set->found.skipped.empty());
}

TEST_CASE("UTA-0478 INV-6: a class listed itself gives its sounds however it was reached", "[ubake]") {
    MemoryPackages packages = weaponPackages();
    const auto set = detail::soundSetOf({"weapons.gun", "weapons.smoke"}, {}, packages.resolver());
    REQUIRE(set.has_value());
    REQUIRE(set->found.sounds.size() == 3);
    CHECK(set->found.sounds[2].path == "weapons.puff");
}

TEST_CASE("UTA-0478 INV-7: the sound set's name follows its classes and its packages' bytes", "[ubake]") {
    const TempDir dir;
    const Fixture fixture = standardFixture();
    writeInstall(dir.path(), fixture);
    writeFile(dir.path() / "System" / "Weapons.u", weaponsPackage());

    const auto nameOf = [&](const std::vector<std::string>& classes, const std::vector<std::string>& sounds = {}) {
        auto install = uta::ubake::Install::open(dir.path());
        REQUIRE(install.has_value());
        const auto name = detail::soundSetName(classes, sounds, *install);
        if (!name.has_value()) FAIL("refused: " << name.error().message());
        return *name;
    };

    const std::string one = nameOf({"weapons.gun"});
    CHECK(one.size() == 64);
    CHECK(nameOf({"weapons.gun", "weapons.shell"}) != one);                // a class added
    CHECK(nameOf({"weapons.shell", "Weapons.Gun"}) == nameOf({"weapons.gun", "weapons.shell"})); // reordered
    CHECK(nameOf({"weapons.gun", "weapons.gun"}) == one);                  // repeated

    // A package Weapons imports -- Engine, for its sounds' class -- changes:
    // the closure must reach it, or the name would not move.
    std::vector<std::uint8_t> otherEngine = engine();
    {
        Packer packer;
        const std::int32_t brush = packer.addClass("Brush", 0, {boolProperty("bStatic", true)});
        packer.addClass("Mover", brush, {boolProperty("bStatic", false)});
        packer.addExport(0, 0, "Actor", {});
        packer.addClass("Extra");
        otherEngine = packer.build();
    }
    writeFile(dir.path() / "System" / "Engine.u", otherEngine);
    CHECK(nameOf({"weapons.gun"}) != one);
}

TEST_CASE("UTA-0467 INV-8: a sound the weapons file names joins the set and its name", "[ubake]") {
    MemoryPackages packages = weaponPackages();
    // Puff is Smoke's, two classes from Gun: no default the set follows
    // reaches it (UTA-0478 INV-6 above), so only naming it stores it.
    const auto set = detail::soundSetOf({"weapons.gun"}, {"Weapons.Puff", "weapons.nosuch", "nopackage.boom", "weapons.gun"},
                                        packages.resolver());
    if (!set.has_value()) FAIL("refused: " << set.error().message());
    REQUIRE(set->found.sounds.size() == 3);
    CHECK(set->found.sounds[0].path == "weapons.gun.fire");
    CHECK(set->found.sounds[1].path == "weapons.hit");
    CHECK(set->found.sounds[2].path == "weapons.puff");
    CHECK(set->found.sounds[2].data == bytesOf({3}));
    // A path that names a class, an object that is not there or a package
    // the install lacks is no sound: reported, and the bake goes on.
    CHECK(set->missing == std::vector<std::string>{"nopackage.boom", "weapons.gun", "weapons.nosuch"});

    // A sound named and reached by a default too is stored once.
    const auto twice = detail::soundSetOf({"weapons.gun"}, {"weapons.hit"}, packages.resolver());
    REQUIRE(twice.has_value());
    CHECK(twice->found.sounds.size() == 2);
    CHECK(twice->missing.empty());
}

TEST_CASE("UTA-0467 INV-8: the sound set's name follows the sounds the file names", "[ubake]") {
    const std::string version = "r1-f1-l0";
    const std::string bare = detail::soundSetNameOf(version, {"a.b"}, {}, {});
    const std::string named = detail::soundSetNameOf(version, {"a.b"}, {"c.d"}, {});
    CHECK(named != bare);
    CHECK(detail::soundSetNameOf(version, {"a.b"}, {"C.D", "c.d"}, {}) == named);       // folded, each once
    CHECK(detail::soundSetNameOf(version, {"a.b"}, {"c.d", "e.f"}, {}) !=
          detail::soundSetNameOf(version, {"a.b"}, {"c.de.f"}, {}));
    // A class cannot pass for a named sound, nor a sound for a class.
    CHECK(detail::soundSetNameOf(version, {"a.b", "c.d"}, {}, {}) != named);
    CHECK(detail::soundSetNameOf(version, {}, {"a.b", "c.d"}, {}) != named);

    // Over an install: naming a sound moves the name, so a weapons file that
    // gains one does not find the old bundle.
    const TempDir dir;
    const Fixture fixture = standardFixture();
    writeInstall(dir.path() / "install", fixture);
    writeFile(dir.path() / "install" / "System" / "Weapons.u", weaponsPackage());
    uta::ubake::SoundSetRequest request;
    request.install = dir.path() / "install";
    request.classes = {"weapons.gun"};
    request.outDir = dir.path() / "out";
    const auto before = uta::ubake::bakeSoundSet(request);
    REQUIRE(before.has_value());
    CHECK(before->soundCount == 2);
    request.sounds = {"weapons.puff", "weapons.nosuch"};
    const auto after = uta::ubake::bakeSoundSet(request);
    if (!after.has_value()) FAIL("refused: " << after.error().message());
    CHECK(after->name != before->name);
    CHECK(after->verdict == uta::ubake::Verdict::Written);
    CHECK(after->soundCount == 3);
    CHECK(after->missing == std::vector<std::string>{"weapons.nosuch"});

    // A named sound's package is in the closure though no class names it:
    // its bytes changing moves the name.
    const auto nameOfPuff = [&] {
        auto install = uta::ubake::Install::open(dir.path() / "install");
        REQUIRE(install.has_value());
        const auto name = detail::soundSetName({}, {"weapons.puff"}, *install);
        REQUIRE(name.has_value());
        return *name;
    };
    const std::string puff = nameOfPuff();
    std::vector<std::uint8_t> grown = weaponsPackage();
    {
        Packer packer;
        packer.addSound(SoundSpec{"Puff", "", {3, 3}});
        grown = packer.build();
    }
    writeFile(dir.path() / "install" / "System" / "Weapons.u", grown);
    CHECK(nameOfPuff() != puff);
}

TEST_CASE("UTA-0478 INV-7: the name's parts cannot run into each other", "[ubake]") {
    const std::string version = "r1-f1-l0";
    CHECK(detail::soundSetNameOf(version, {"ab.c", "d.e"}, {}, {}) != detail::soundSetNameOf(version, {"ab.cd.e"}, {}, {}));
    CHECK(detail::soundSetNameOf(version, {"a.b"}, {}, {}) != detail::soundSetNameOf("r2-f1-l0", {"a.b"}, {}, {}));
    const uta::ubake::ClosureEntry absent{"weapons", std::nullopt};
    CHECK(detail::soundSetNameOf(version, {"a.b"}, {}, {absent}) != detail::soundSetNameOf(version, {"a.b"}, {}, {}));
}

TEST_CASE("UTA-0478 SS 4.4: bakeSoundSet writes a Sounds bundle and serves it again from the cache", "[ubake]") {
    const TempDir dir;
    const Fixture fixture = standardFixture();
    writeInstall(dir.path() / "install", fixture);
    writeFile(dir.path() / "install" / "System" / "Weapons.u", weaponsPackage());

    uta::ubake::SoundSetRequest request;
    request.install = dir.path() / "install";
    request.classes = {"weapons.gun", "weapons.nothere"};
    request.outDir = dir.path() / "out";

    const auto written = uta::ubake::bakeSoundSet(request);
    if (!written.has_value()) FAIL("refused: " << written.error().message());
    CHECK(written->verdict == uta::ubake::Verdict::Written);
    CHECK(written->soundCount == 2);
    CHECK(written->soundBytes == 5);
    CHECK(written->unresolved == std::vector<std::string>{"weapons.nothere"});
    CHECK(written->path == request.outDir / (written->name + ".utab"));

    const auto bytes = uta::fs::readFile(written->path);
    REQUIRE(bytes.has_value());
    const auto bundle = uta::ubundle::read(*bytes);
    if (!bundle.has_value()) FAIL("the bundle does not read: " << bundle.error().message());
    CHECK(bundle->header.kind == uta::ubundle::BundleKind::Sounds);
    CHECK(bundle->header.origin == uta::ubundle::Origin::Derived);
    CHECK(bundle->baker == uta::ubake::bakerVersion());
    REQUIRE(bundle->sounds.has_value());
    CHECK(bundle->sounds->size() == 2);

    const auto again = uta::ubake::bakeSoundSet(request);
    REQUIRE(again.has_value());
    CHECK(again->verdict == uta::ubake::Verdict::Cached);
    CHECK(again->name == written->name);

    request.force = true;
    const auto forced = uta::ubake::bakeSoundSet(request);
    REQUIRE(forced.has_value());
    CHECK(forced->verdict == uta::ubake::Verdict::Written);
}
