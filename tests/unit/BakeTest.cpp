// UTA-0011's baker cases: one bake, its name, and its cache.
//
// docs/specs/UTA-0011-map-baker.md SS 7: INV-1 to INV-4, INV-7 to INV-10,
// INV-13, INV-14, INV-16 and INV-17. The fixtures are BakeFixture.h's. Cases
// about what a bake makes go through detail::bake with an in-memory resolver;
// cases about the name or the cache use a synthetic install on disk.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "core/FileSystem.h"
#include "core/Jobs.h"
#include "core/Sha256.h"
#include "support/UnrealPackageBuilder.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "umap/Build.h"
#include "umat/Fingerprint.h"
#include "umat/Library.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::bake;
using uta::ErrorCode;
using uta::JobSystem;
using uta::ubake::BakeRequest;
using uta::ubake::BakeResult;
using uta::ubake::Install;
using uta::ubake::NameInputs;
using uta::ubake::Verdict;
using uta::upkg::Package;
using uta::upkg::PackageResolver;
namespace detail = uta::ubake::detail;

namespace {

const detail::CuratedLookup NOTHING_CURATED =
    [](std::uint64_t) -> const uta::umat::CuratedOverride* { return nullptr; };

/// For a map that names no other package's content.
const PackageResolver NOTHING_AVAILABLE =
    [](std::string_view) -> uta::Result<const Package*> { return nullptr; };

uta::Result<BakeResult> bakeBytes(const std::vector<std::uint8_t>& bytes,
                                  const PackageResolver& resolver, JobSystem& jobs,
                                  const detail::CuratedLookup& curated = NOTHING_CURATED) {
    const auto map = Package::open(uta::test::asBytes(bytes));
    REQUIRE(map.has_value());
    return detail::bake(*map, MAP_NAME, resolver, jobs, curated, uta::umat::TEXTURE_BUDGET_BYTES);
}

BakeResult baked(const std::vector<std::uint8_t>& bytes, const PackageResolver& resolver,
                 JobSystem& jobs, const detail::CuratedLookup& curated = NOTHING_CURATED) {
    auto result = bakeBytes(bytes, resolver, jobs, curated);
    if (!result.has_value()) FAIL("the bake was refused: " << result.error().message());
    return std::move(*result);
}

std::vector<std::string> idsOf(const BakeResult& result) {
    std::vector<std::string> ids;
    REQUIRE(result.bundle.materials.has_value());
    for (const auto& record : *result.bundle.materials) ids.push_back(record.id);
    return ids;
}

/// Hex written here rather than borrowed from ubake, so INV-4 compares two
/// implementations of the name's last step and not one with itself.
std::string hexOf(const std::array<std::byte, 32>& digest) {
    std::string out;
    for (const std::byte part : digest) {
        const auto value = std::to_integer<unsigned>(part);
        out += "0123456789abcdef"[value >> 4];
        out += "0123456789abcdef"[value & 0xFU];
    }
    return out;
}

std::string nameIn(const fs::path& root, const fs::path& map) {
    auto install = Install::open(root);
    REQUIRE(install.has_value());
    const auto name = uta::ubake::bakeName(map, *install);
    if (!name.has_value()) FAIL("no name: " << name.error().message());
    return *name;
}

/// Flips one bit in the last byte of the export named `exportName` -- one
/// changed byte, and a package that still opens.
void changeOneByte(const fs::path& file, std::string_view exportName) {
    auto bytes = uta::fs::readFile(file);
    REQUIRE(bytes.has_value());
    std::size_t at = 0;
    {
        const auto package = Package::open(*bytes);
        REQUIRE(package.has_value());
        bool found = false;
        for (const uta::upkg::ExportEntry& entry : package->exports()) {
            if (package->name(entry.objectName).value_or("") == exportName) {
                at = entry.serialOffset + entry.serialSize - 1;
                found = true;
            }
        }
        REQUIRE(found);
    }
    (*bytes)[at] ^= std::byte{0x01};
    REQUIRE(uta::fs::writeFileAtomically(file, *bytes).has_value());
}

BakeRequest requestFor(const fs::path& install, const fs::path& map, const fs::path& outDir) {
    BakeRequest request;
    request.install = install;
    request.map = map;
    request.outDir = outDir;
    return request;
}

} // namespace

TEST_CASE("the same map bakes to the same bytes on any worker count", "[ubake][bake]") {
    // INV-1.
    const TempDir dir;
    const fs::path install = dir.path() / "install";
    const fs::path map = writeInstall(install, standardFixture());

    std::vector<std::string> names;
    std::vector<std::vector<std::byte>> files;
    for (const unsigned workers : {1U, 4U}) {
        JobSystem jobs(workers);
        const auto outcome = uta::ubake::bakeToDirectory(
            requestFor(install, map, dir.path() / ("out-" + std::to_string(workers))), jobs);
        REQUIRE(outcome.has_value());
        REQUIRE(outcome->verdict == Verdict::Written);
        names.push_back(outcome->name);
        auto bytes = uta::fs::readFile(outcome->path);
        REQUIRE(bytes.has_value());
        files.push_back(std::move(*bytes));
    }
    CHECK(names[0] == names[1]);
    CHECK(files[0] == files[1]);

    // Equal bytes prove little if nothing was generated.
    const auto bundle = uta::ubundle::read(files[0]);
    REQUIRE(bundle.has_value());
    REQUIRE(bundle->textures.has_value());
    CHECK_FALSE(bundle->textures->empty());
}

TEST_CASE("the bake asks only for packages in the closure its name covers", "[ubake][bake]") {
    // INV-2. The fixture's texture lives in TexPkg and its actor's class in
    // ActorPkg, so both routes a bake takes into another package are taken.
    const Fixture fixture = standardFixture();
    MemoryPackages packages = memoryPackagesFor(fixture);
    const PackageResolver inner = packages.resolver();
    std::set<std::string> asked;
    const PackageResolver recording = [&](std::string_view name) {
        asked.emplace(name);
        return inner(name);
    };

    const std::vector<std::uint8_t> bytes = fixture.map.build();
    const auto map = Package::open(uta::test::asBytes(bytes));
    REQUIRE(map.has_value());
    const auto closure = detail::closure(*map, inner);
    REQUIRE(closure.has_value());

    JobSystem jobs(2);
    const auto result = detail::bake(*map, MAP_NAME, recording, jobs, NOTHING_CURATED,
                                     uta::umat::TEXTURE_BUDGET_BYTES);
    REQUIRE(result.has_value());
    CHECK(asked.contains("texpkg"));
    CHECK(asked.contains("actorpkg"));
    for (const std::string& name : asked) {
        INFO("asked for: " << name);
        CHECK(std::binary_search(closure->begin(), closure->end(), name));
    }
}

TEST_CASE("the name changes when any one input changes", "[ubake][name]") {
    // INV-3.
    const Fixture fixture = standardFixture();

    SECTION("a byte of the map") {
        const TempDir dir;
        const fs::path map = writeInstall(dir.path(), fixture);
        const std::string before = nameIn(dir.path(), map);
        changeOneByte(map, "WallPal");
        CHECK(nameIn(dir.path(), map) != before);
    }

    SECTION("a byte of a closure package") {
        const TempDir dir;
        const fs::path map = writeInstall(dir.path(), fixture);
        const std::string before = nameIn(dir.path(), map);
        changeOneByte(dir.path() / "Textures" / "TexPkg.utx", "PlatePal");
        CHECK(nameIn(dir.path(), map) != before);
    }

    SECTION("the baker version") {
        NameInputs inputs;
        inputs.bakerVersion = uta::ubake::bakerVersion();
        inputs.mapName = std::string(MAP_NAME);
        NameInputs bumped = inputs;
        bumped.bakerVersion[1] = static_cast<char>(bumped.bakerVersion[1] + 1); // r1 -> r2
        CHECK(detail::nameOf(bumped) != detail::nameOf(inputs));
    }

    SECTION("a closure package going from absent or unopenable to present") {
        const TempDir dir;
        const fs::path map = writeInstall(dir.path(), fixture);
        const fs::path actorPkg = dir.path() / "System" / "ActorPkg.u";
        const std::string present = nameIn(dir.path(), map);
        fs::remove(actorPkg);
        const std::string absent = nameIn(dir.path(), map);
        writeFile(actorPkg, {'n', 'o', 't', ' ', 'a', ' ', 'p', 'k', 'g'});
        const std::string broken = nameIn(dir.path(), map);
        CHECK(absent != present);
        CHECK(broken != present);
        // SS 4.4 item 5: both enter the name as the name and 0x00.
        CHECK(broken == absent);
    }
}

TEST_CASE("nameOf is the SHA-256 of the byte string SS 4.4 defines", "[ubake][name]") {
    // INV-4, assembled literally. The closure is given out of order and one
    // entry is absent.
    std::array<std::byte, 32> mapDigest{};
    std::array<std::byte, 32> abDigest{};
    mapDigest.fill(std::byte{0x11});
    abDigest.fill(std::byte{0x22});

    NameInputs inputs;
    inputs.bakerVersion = "r1-f3-l0123456789abcdef";
    inputs.mapName = "dm-x";
    inputs.mapDigest = mapDigest;
    inputs.closure = {{"c", std::nullopt}, {"ab", abDigest}};

    std::string expected = "uta-bake-name-1\n";
    expected += "r1-f3-l0123456789abcdef\n";
    expected += '\0'; // no recipe
    expected += "dm-x\n";
    for (const std::byte part : mapDigest) expected += static_cast<char>(part);
    expected += "ab\n";
    expected += '\x01';
    for (const std::byte part : abDigest) expected += static_cast<char>(part);
    expected += "c\n";
    expected += '\0';

    const auto digest =
        uta::sha256(std::as_bytes(std::span<const char>(expected.data(), expected.size())));
    CHECK(detail::nameOf(inputs) == hexOf(digest));

    // {a, bc} with the same digests: a dropped separator would make the two
    // closures one string.
    NameInputs other = inputs;
    other.closure = {{"bc", std::nullopt}, {"a", abDigest}};
    CHECK(detail::nameOf(other) != detail::nameOf(inputs));
}

TEST_CASE("bakerVersion names the revision and the format and the library digest",
          "[ubake][name]") {
    // SS 4.3's layout, re-derived here. UTA-0008 SS 10 binds to the format
    // being in it and UTA-0010 SS 4.6 to the library digest being in it.
    std::string digest;
    const std::uint64_t library = uta::umat::libraryDigest();
    for (int shift = 60; shift >= 0; shift -= 4) digest += "0123456789abcdef"[(library >> shift) & 0xFU];
    const std::string expected = "r" + std::to_string(uta::ubake::BAKER_REVISION) + "-f"
                                 + std::to_string(uta::ubundle::FORMAT_VERSION) + "-l" + digest;
    CHECK(uta::ubake::bakerVersion() == expected);
}

TEST_CASE("a material takes the curated entry for its picture and no other", "[ubake][bake]") {
    // INV-7. The lookup answers for the Floor texture's own fingerprint and
    // for nothing else.
    const Fixture fixture = standardFixture();
    const Picture floor = picture(2);
    std::vector<std::byte> indices;
    for (const std::uint8_t index : floor.indices) indices.push_back(static_cast<std::byte>(index));
    uta::upkg::Mip mip;
    mip.pixels = indices;
    mip.width = floor.width;
    mip.height = floor.height;
    uta::upkg::Palette palette;
    for (const auto& colour : floor.palette)
        palette.entries.push_back({colour[0], colour[1], colour[2], colour[3]});
    const auto key = uta::umat::pictureFingerprint(mip, palette);
    REQUIRE(key.has_value());

    uta::umat::CuratedOverride metal;
    metal.metallic = true;
    std::set<std::uint64_t> asked;
    const detail::CuratedLookup lookup =
        [&](std::uint64_t fingerprint) -> const uta::umat::CuratedOverride* {
        asked.insert(fingerprint);
        return fingerprint == *key ? &metal : nullptr;
    };

    JobSystem jobs(2);
    MemoryPackages packages = memoryPackagesFor(fixture);
    const BakeResult curated = baked(fixture.map.build(), packages.resolver(), jobs, lookup);
    CHECK(asked.contains(*key));
    REQUIRE(curated.bundle.materials.has_value());
    REQUIRE(curated.bundle.materials->size() == STANDARD_MATERIALS.size());
    for (const auto& record : *curated.bundle.materials) {
        INFO("material: " << record.id);
        CHECK(record.metallic == (record.id == "dm-fixture.floor"));
    }

    MemoryPackages again = memoryPackagesFor(fixture);
    const BakeResult plain = baked(fixture.map.build(), again.resolver(), jobs);
    for (const auto& record : *plain.bundle.materials) {
        INFO("material: " << record.id);
        CHECK_FALSE(record.metallic);
    }
}

TEST_CASE("the surfaces decide which variants a texture gets", "[ubake][bake]") {
    // INV-8: three surfaces over two textures. Beta's surface carries flags
    // other than PF_Masked, so a test of the wrong bit would make it masked.
    MapBuilder map;
    const std::int32_t alpha = map.addTexture(TextureSpec{"Alpha", "", picture(4), false});
    const std::int32_t beta = map.addTexture(TextureSpec{"Beta", "", picture(5), false});
    map.addSurface(alpha, MASKED).addSurface(alpha).addSurface(beta, 0x00000001u | 0x00000004u);

    JobSystem jobs(2);
    const BakeResult result = baked(map.build(), NOTHING_AVAILABLE, jobs);
    CHECK(idsOf(result)
          == std::vector<std::string>{"dm-fixture.alpha", "dm-fixture.alpha#masked",
                                      "dm-fixture.beta"});
    CHECK(result.skipped.empty());
}

TEST_CASE("a texture carrying a Format property is skipped and named", "[ubake][bake]") {
    // INV-9: one texture, baked without the property and with it.
    for (const bool format : {false, true}) {
        INFO("Format property: " << format);
        MapBuilder map;
        map.addSurface(map.addTexture(TextureSpec{"Wall", "", picture(1), format}));
        JobSystem jobs(2);
        const BakeResult result = baked(map.build(), NOTHING_AVAILABLE, jobs);
        if (!format) {
            CHECK(idsOf(result) == std::vector<std::string>{"dm-fixture.wall"});
            CHECK(result.skipped.empty());
        } else {
            CHECK(idsOf(result).empty());
            REQUIRE(result.bundle.textures.has_value());
            CHECK(result.bundle.textures->empty());
            REQUIRE(result.skipped.size() == 1);
            CHECK(result.skipped[0].material == "dm-fixture.wall");
            CHECK(result.skipped[0].reason.find("Format") != std::string::npos);
        }
    }
}

TEST_CASE("a cached bake is served and force or a bad header bakes over it", "[ubake][cache]") {
    // INV-10.
    const TempDir dir;
    const fs::path install = dir.path() / "install";
    const fs::path map = writeInstall(install, standardFixture());
    const fs::path outDir = dir.path() / "out";
    const std::string name = nameIn(install, map);
    const fs::path path = outDir / (name + ".utab");

    // A valid sixteen-byte header and nothing else.
    std::vector<std::byte> header;
    for (const char part : std::string_view("UTAB")) header.push_back(static_cast<std::byte>(part));
    for (int shift = 0; shift < 32; shift += 8)
        header.push_back(static_cast<std::byte>((uta::ubundle::FORMAT_VERSION >> shift) & 0xFFU));
    header.resize(uta::ubundle::HEADER_SIZE, std::byte{0});
    REQUIRE(uta::ubundle::readHeader(header).has_value());
    fs::create_directories(outDir);
    REQUIRE(uta::fs::writeFileAtomically(path, header).has_value());

    JobSystem jobs(2);
    BakeRequest request = requestFor(install, map, outDir);

    const auto cached = uta::ubake::bakeToDirectory(request, jobs);
    REQUIRE(cached.has_value());
    CHECK(cached->verdict == Verdict::Cached);
    CHECK_FALSE(cached->result.has_value());
    CHECK(uta::fs::readFile(path).value() == header);

    request.force = true;
    const auto forced = uta::ubake::bakeToDirectory(request, jobs);
    REQUIRE(forced.has_value());
    CHECK(forced->verdict == Verdict::Written);
    CHECK(uta::ubundle::read(uta::fs::readFile(path).value()).has_value());

    // A file whose header readHeader refuses is baked over without force.
    std::vector<std::byte> badMagic = header;
    badMagic[0] = std::byte{'X'};
    REQUIRE(uta::fs::writeFileAtomically(path, badMagic).has_value());
    request.force = false;
    const auto replaced = uta::ubake::bakeToDirectory(request, jobs);
    REQUIRE(replaced.has_value());
    CHECK(replaced->verdict == Verdict::Written);
    CHECK(uta::ubundle::read(uta::fs::readFile(path).value()).has_value());
}

TEST_CASE("the bake builds ROOM from the Model the level names", "[ubake][bake]") {
    // INV-13. The decoy is larger in every table and yields a different room
    // count, which is checked first, so a baker taking the largest Model is
    // one this fixture can see.
    Fixture fixture = standardFixture();
    fixture.map.addDecoyModel();
    const std::vector<std::uint8_t> bytes = fixture.map.build();
    const auto package = Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());

    std::vector<uta::upkg::Model> models;
    for (const uta::upkg::ExportEntry& entry : package->exports()) {
        if (package->objectName(entry.objectClass).value_or("") != "Model") continue;
        auto model = uta::upkg::readModel(*package, entry);
        REQUIRE(model.has_value());
        models.push_back(std::move(*model));
    }
    REQUIRE(models.size() == 2);
    CHECK(models[1].nodes.size() > models[0].nodes.size());
    CHECK(models[1].surfs.size() > models[0].surfs.size());
    const auto decoyRooms = uta::umap::buildRoomMap(models[1]);
    REQUIRE(decoyRooms.has_value());
    REQUIRE(decoyRooms->map.rooms.size() == MapBuilder::DECOY_ROOMS);

    JobSystem jobs(2);
    MemoryPackages packages = memoryPackagesFor(fixture);
    const BakeResult result = baked(bytes, packages.resolver(), jobs);
    REQUIRE(result.bundle.rooms.has_value());
    CHECK(result.bundle.rooms->rooms.size() == MapBuilder::ROOMS);
}

TEST_CASE("a map with no Level or no Model of its own is refused and named", "[ubake][bake]") {
    // INV-14.
    const auto refusedWith = [](const MapBuilder& map) {
        JobSystem jobs(1);
        const auto result = bakeBytes(map.build(), NOTHING_AVAILABLE, jobs);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        CHECK(result.error().message().find(MAP_NAME) != std::string_view::npos);
        return std::string(result.error().message());
    };

    MapBuilder map;
    map.addSurface(map.addTexture(TextureSpec{"Wall", "", picture(1), false}));
    map.addActorOfClass("ActorPkg", "Lamp");

    SECTION("no Level export") {
        CHECK(refusedWith(MapBuilder(map).setLevelCount(0)).find("no Level") != std::string::npos);
    }
    SECTION("two Level exports") {
        CHECK(refusedWith(MapBuilder(map).setLevelCount(2)).find("2 Level") != std::string::npos);
    }
    SECTION("a level naming no Model") {
        refusedWith(MapBuilder(map).setModelTarget(MapBuilder::ModelTarget::Null));
    }
    SECTION("a level naming an export that is not a Model") {
        refusedWith(MapBuilder(map).setModelTarget(MapBuilder::ModelTarget::NotAModel));
    }
}

TEST_CASE("the written file is the name and .utab in the caller's directory", "[ubake][cache]") {
    // INV-16.
    const TempDir dir;
    const fs::path install = dir.path() / "install";
    const fs::path map = writeInstall(install, standardFixture());
    const fs::path outDir = dir.path() / "out";

    JobSystem jobs(2);
    const auto outcome = uta::ubake::bakeToDirectory(requestFor(install, map, outDir), jobs);
    REQUIRE(outcome.has_value());
    CHECK(outcome->verdict == Verdict::Written);
    CHECK(outcome->path == outDir / (outcome->name + ".utab"));
    CHECK(fs::is_regular_file(outcome->path));
    CHECK(outcome->name.size() == 64);
    CHECK(std::all_of(outcome->name.begin(), outcome->name.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }));
    CHECK(outcome->name == nameIn(install, map));
}

TEST_CASE("every MATS record has maps and every map has a record", "[ubake][bake]") {
    // INV-17. The surfaces name the textures in DESCENDING id order, and
    // Gamma is skipped, so TEXS following surface order or a skip leaving a
    // record behind both show.
    MapBuilder map;
    const std::int32_t zeta = map.addTexture(TextureSpec{"Zeta", "", picture(6), false});
    const std::int32_t mu = map.addTexture(TextureSpec{"Mu", "", picture(7), false});
    const std::int32_t gamma = map.addTexture(TextureSpec{"Gamma", "", picture(9), true});
    const std::int32_t beta = map.addTexture(TextureSpec{"Beta", "", picture(8), false});
    map.addSurface(zeta).addSurface(mu).addSurface(gamma).addSurface(beta, MASKED).addSurface(beta);

    JobSystem jobs(2);
    const BakeResult result = baked(map.build(), NOTHING_AVAILABLE, jobs);
    const std::vector<std::string> ids = idsOf(result);
    CHECK(ids
          == std::vector<std::string>{"dm-fixture.beta", "dm-fixture.beta#masked", "dm-fixture.mu",
                                      "dm-fixture.zeta"});
    REQUIRE(result.skipped.size() == 1);
    CHECK(result.skipped[0].material == "dm-fixture.gamma");

    // TEXS: ascending by material, each material's maps in MapKind order.
    const std::vector<std::string_view> kinds = {"base", "normal", "rough", "height", "emit"};
    REQUIRE(result.bundle.textures.has_value());
    std::string previousId;
    std::size_t previousKind = 0;
    std::set<std::string> withBase;
    for (const auto& texture : *result.bundle.textures) {
        INFO("map: " << texture.name);
        const std::size_t colon = texture.name.find(':');
        REQUIRE(colon != std::string::npos);
        const std::string id = texture.name.substr(0, colon);
        const std::string kind = texture.name.substr(colon + 1);
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
        const auto found = std::find(kinds.begin(), kinds.end(), kind);
        REQUIRE(found != kinds.end());
        const auto kindIndex = static_cast<std::size_t>(found - kinds.begin());
        if (id == previousId) {
            CHECK(kindIndex > previousKind);
        } else {
            CHECK(id > previousId);
            CHECK(kind == "base");
        }
        if (kind == "base") withBase.insert(id);
        previousId = id;
        previousKind = kindIndex;
    }
    for (const std::string& id : ids) {
        INFO("material: " << id);
        CHECK(withBase.contains(id));
    }
}

TEST_CASE("every GEOM material is a MATS id and a skipped texture's surface wears none",
          "[ubake][geom]") {
    // UTA-0109 INV-10. The standard fixture's four surfaces each make a
    // material; a fifth names a texture carrying a Format property, which is
    // skipped (UTA-0011 INV-9), so its surface must draw wearing nothing.
    Fixture fixture = standardFixture();
    const std::int32_t odd = fixture.map.addTexture(TextureSpec{"Odd", "", picture(4), true});
    fixture.map.addSurface(odd);
    MemoryPackages packages = memoryPackagesFor(fixture);
    JobSystem jobs(2);
    const BakeResult result = baked(fixture.map.build(), packages.resolver(), jobs);

    REQUIRE(result.skipped.size() == 1);
    const std::vector<std::string> ids = idsOf(result);
    const std::set<std::string> known(ids.begin(), ids.end());
    REQUIRE(result.bundle.geometry.has_value());

    std::size_t wearingNothing = 0;
    std::uint64_t indices = 0;
    for (const auto& batch : result.bundle.geometry->batches) {
        indices += batch.indexCount;
        if (batch.material.empty()) {
            ++wearingNothing;
            continue;
        }
        INFO("batch material " << batch.material);
        CHECK(known.count(batch.material) == 1);
    }
    CHECK(wearingNothing == 1);
    CHECK(indices == 5 * 6); // every surface drew its square: two triangles each
}

TEST_CASE("a texture's DrawScale sets how far one repeat of it spans", "[ubake][geom]") {
    // UTA-0109 INV-11. The floor texture is four texels wide and its square 64
    // units, so the far corner lands at u = 64 / (4 * scale). A DrawScale that
    // is not a finite positive number counts as 1.
    const auto farU = [](float drawScale) {
        Fixture fixture;
        TextureSpec floor{"Floor", "", picture(2), false};
        floor.drawScale = drawScale;
        fixture.map.addSurface(fixture.map.addTexture(floor));
        MemoryPackages packages = memoryPackagesFor(fixture);
        JobSystem jobs(1);
        const BakeResult result = baked(fixture.map.build(), packages.resolver(), jobs);
        REQUIRE(result.bundle.geometry.has_value());
        REQUIRE_FALSE(result.bundle.geometry->vertices.empty());
        float most = 0;
        for (const auto& vertex : result.bundle.geometry->vertices) most = std::max(most, vertex.u);
        return most;
    };
    CHECK(farU(0) == 16.0F); // no DrawScale property at all
    CHECK(farU(2) == 8.0F);
    CHECK(farU(-1) == 16.0F);
    CHECK(farU(std::numeric_limits<float>::quiet_NaN()) == 16.0F);
}
